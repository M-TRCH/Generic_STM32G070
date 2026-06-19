#include "Sensors.h"
#include "Config.h"
#include "Crc.h"
#include "Hardware.h"

#include <Wire.h>

HardwareSerial Serial1(PB7, PA9); // RX, TX สำหรับ PZEM-017

float ina180ZeroOffsetVoltage = 0.0f;

namespace {

// ---- SHT40 / SHT31 ----
constexpr uint8_t kSht40Address = 0x44;
constexpr uint8_t kSht40MeasureHighPrecision = 0xFD;
constexpr uint8_t kSht31Address = 0x44;
constexpr uint16_t kSht31MeasureHighRepeatability = 0x2400;

// ---- INA180 ----
constexpr uint8_t kIna180AnalogPin = PA6;
constexpr uint16_t kIna180AdcMaxCount = 4095;
constexpr uint8_t kIna180SampleCount = 16;
constexpr float kIna180AdcReferenceVoltage = 3.3f;
constexpr float kIna180Gain = 50.0f;
constexpr float kIna180ShuntResistanceOhms = 0.010f;
constexpr float kIna180NoiseFloorAmps = 0.02f;

// ---- PZEM-017 / PZEM-003 (โปรโตคอลเดียวกัน) ----
constexpr uint8_t kPzem017DefaultAddress = kPzemChargeAddress;
constexpr uint32_t kPzem017BaudRate = 9600;
constexpr uint8_t kPzem017RegisterCount = 8;
constexpr uint16_t kPzem017ResponseSize = 3 + (kPzem017RegisterCount * 2) + 2;
// holding register / general address สำหรับอ่าน-แก้ ID (Modbus slave address)
constexpr uint16_t kPzemRegSlaveAddress = 0x0002; // รีจิสเตอร์เก็บ ID ของอุปกรณ์
constexpr uint8_t kPzemGeneralAddress = 0xF8;     // general address (ใช้ได้เมื่อมีอุปกรณ์ตัวเดียวบนบัส)
constexpr uint8_t kPzemAddressMin = 0x01;
constexpr uint8_t kPzemAddressMax = 0xF7;
// ตั้งเป็นขา GPIO ที่ถูกต้องหาก RS485 transceiver ต้องการ DE/RE control
constexpr int8_t kRs485DirectionPin = -1;

void setRs485Transmit(bool enabled)
{
  if (kRs485DirectionPin < 0) {
    return;
  }
  digitalWrite(static_cast<uint8_t>(kRs485DirectionPin), enabled ? HIGH : LOW);
}

uint16_t readAveragedAdc(uint8_t pin, uint8_t sampleCount)
{
  uint32_t total = 0;
  for (uint8_t index = 0; index < sampleCount; ++index) {
    total += analogRead(pin);
  }
  return static_cast<uint16_t>(total / sampleCount);
}

float adcCountToVoltage(uint16_t rawAdc)
{
  return (static_cast<float>(rawAdc) * kIna180AdcReferenceVoltage)
         / static_cast<float>(kIna180AdcMaxCount);
}

} // namespace

// ---------------------------------------------------------------------------
// SHT40 (บัส Wire1)
// ---------------------------------------------------------------------------
bool readSht40(Sht40Reading &reading)
{
  uint8_t command = kSht40MeasureHighPrecision;
  Wire1.beginTransmission(kSht40Address);
  Wire1.write(&command, 1);
  if (Wire1.endTransmission() != 0) {
    return false;
  }

  delay(10);

  constexpr uint8_t kResponseSize = 6;
  uint8_t response[kResponseSize] = {};
  if (Wire1.requestFrom(static_cast<int>(kSht40Address), static_cast<int>(kResponseSize)) != kResponseSize) {
    return false;
  }

  for (uint8_t i = 0; i < kResponseSize; ++i) {
    if (!Wire1.available()) {
      return false;
    }
    response[i] = static_cast<uint8_t>(Wire1.read());
  }

  if (crc8Sensirion(response, 2) != response[2]) {
    return false;
  }
  if (crc8Sensirion(response + 3, 2) != response[5]) {
    return false;
  }

  uint16_t rawTemperature = static_cast<uint16_t>(response[0] << 8) | response[1];
  uint16_t rawHumidity = static_cast<uint16_t>(response[3] << 8) | response[4];

  reading.temperatureC = -45.0f + (175.0f * static_cast<float>(rawTemperature) / 65535.0f);
  reading.humidityPercent = -6.0f + (125.0f * static_cast<float>(rawHumidity) / 65535.0f);
  reading.humidityPercent = constrain(reading.humidityPercent, 0.0f, 100.0f);
  return true;
}

void printSht40Reading(const Sht40Reading &reading)
{
  Serial.print(F("SHT40 T="));
  Serial.print(reading.temperatureC, 2);
  Serial.print(F("C RH="));
  Serial.print(reading.humidityPercent, 2);
  Serial.println(F("%"));
}

// ---------------------------------------------------------------------------
// SHT31 (บัส Wire ช่องเดียวกับ OLED)
// ---------------------------------------------------------------------------
bool readSht31(Sht40Reading &reading)
{
  Wire.beginTransmission(kSht31Address);
  Wire.write(static_cast<uint8_t>(kSht31MeasureHighRepeatability >> 8));
  Wire.write(static_cast<uint8_t>(kSht31MeasureHighRepeatability & 0xFF));
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(20); // high repeatability ใช้เวลาวัดสูงสุด ~15ms

  constexpr uint8_t kResponseSize = 6;
  uint8_t response[kResponseSize] = {};
  if (Wire.requestFrom(static_cast<int>(kSht31Address), static_cast<int>(kResponseSize)) != kResponseSize) {
    return false;
  }

  for (uint8_t i = 0; i < kResponseSize; ++i) {
    if (!Wire.available()) {
      return false;
    }
    response[i] = static_cast<uint8_t>(Wire.read());
  }

  if (crc8Sensirion(response, 2) != response[2]) {
    return false;
  }
  if (crc8Sensirion(response + 3, 2) != response[5]) {
    return false;
  }

  uint16_t rawTemperature = static_cast<uint16_t>(response[0] << 8) | response[1];
  uint16_t rawHumidity = static_cast<uint16_t>(response[3] << 8) | response[4];

  reading.temperatureC = -45.0f + (175.0f * static_cast<float>(rawTemperature) / 65535.0f);
  reading.humidityPercent = 100.0f * static_cast<float>(rawHumidity) / 65535.0f;
  reading.humidityPercent = constrain(reading.humidityPercent, 0.0f, 100.0f);
  return true;
}

void printSht31Reading(const Sht40Reading &reading)
{
  Serial.print(F("SHT31 T="));
  Serial.print(reading.temperatureC, 2);
  Serial.print(F("C RH="));
  Serial.print(reading.humidityPercent, 2);
  Serial.println(F("%"));
}

// ---------------------------------------------------------------------------
// INA180 (analog current sensor)
// ---------------------------------------------------------------------------
void calibrateIna180ZeroOffset()
{
  constexpr uint8_t kCalibrationSamples = 64;
  uint16_t rawAdc = readAveragedAdc(kIna180AnalogPin, kCalibrationSamples);
  ina180ZeroOffsetVoltage = adcCountToVoltage(rawAdc);

  Serial.print(F("INA180 zero offset = "));
  Serial.print(ina180ZeroOffsetVoltage, 4);
  Serial.println(F(" V"));
}

void initIna180()
{
  analogReadResolution(12);
  pinMode(kIna180AnalogPin, INPUT_ANALOG);
  calibrateIna180ZeroOffset();
}

bool readIna180(Ina180Reading &reading)
{
  reading.rawAdc = readAveragedAdc(kIna180AnalogPin, kIna180SampleCount);
  reading.outputVoltage = adcCountToVoltage(reading.rawAdc);

  reading.sensedVoltage = reading.outputVoltage - ina180ZeroOffsetVoltage;
  reading.shuntVoltage = reading.sensedVoltage / kIna180Gain;
  reading.currentAmps = reading.shuntVoltage / kIna180ShuntResistanceOhms;

  if (fabsf(reading.currentAmps) < kIna180NoiseFloorAmps) {
    reading.currentAmps = 0.0f;
    reading.shuntVoltage = 0.0f;
    reading.sensedVoltage = 0.0f;
  }

  return true;
}

void printIna180Reading(const Ina180Reading &reading)
{
  char currentValue[12] = {};
  char shuntValue[12] = {};

  dtostrf(reading.currentAmps, 0, 3, currentValue);
  dtostrf(reading.shuntVoltage * 1000.0f, 0, 3, shuntValue);

  Serial.print(F("INA180 I="));
  Serial.print(currentValue);
  Serial.print(F("A Vsh="));
  Serial.print(shuntValue);
  Serial.print(F("mV ADC="));
  Serial.print(reading.rawAdc);
  Serial.print(F(" Vout="));
  Serial.print(reading.outputVoltage, 4);
  Serial.print(F("V dV="));
  Serial.print(reading.sensedVoltage * 1000.0f, 3);
  Serial.println(F("mV"));
}

// ---------------------------------------------------------------------------
// PZEM-017 (RS485 / Modbus-RTU)
// ---------------------------------------------------------------------------
void initPzem017()
{
  if (kRs485DirectionPin >= 0) {
    pinMode(static_cast<uint8_t>(kRs485DirectionPin), OUTPUT);
    setRs485Transmit(false);
  }
  Serial1.begin(kPzem017BaudRate, SERIAL_8N2);
}

bool readPzem017AtAddress(uint8_t address, Pzem017Reading &reading)
{
  HardwareSerial &pzemPort = Serial1;

  if (address < kPzemAddressMin || address > kPzemAddressMax) {
    return false;
  }

  while (pzemPort.available() > 0) {
    (void)pzemPort.read();
  }

  uint8_t request[] = {address, 0x04, 0x00, 0x00, 0x00, kPzem017RegisterCount, 0x00, 0x00};
  uint16_t requestCrc = crc16Modbus(request, sizeof(request) - 2);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFF);
  request[7] = static_cast<uint8_t>(requestCrc >> 8);

  setRs485Transmit(true);
  pzemPort.write(request, sizeof(request));
  pzemPort.flush();
  setRs485Transmit(false);

  uint8_t response[kPzem017ResponseSize] = {};
  uint32_t start = millis();
  size_t index = 0;

  while (index < sizeof(response) && (millis() - start) < 200U) {
    if (pzemPort.available() > 0) {
      response[index++] = static_cast<uint8_t>(pzemPort.read());
    }
  }

  if (index != sizeof(response)) {
    return false;
  }

  uint16_t responseCrc = crc16Modbus(response, sizeof(response) - 2);
  uint16_t receivedCrc = static_cast<uint16_t>(response[sizeof(response) - 2]) |
                         static_cast<uint16_t>(response[sizeof(response) - 1] << 8);
  if (responseCrc != receivedCrc) {
    return false;
  }

  if (response[0] != address || response[1] != 0x04 || response[2] != (kPzem017RegisterCount * 2)) {
    return false;
  }

  auto readRegister = [&](size_t offset) -> uint16_t {
    return static_cast<uint16_t>(response[offset] << 8) | response[offset + 1];
  };

  reading.rawVoltage = readRegister(3);
  reading.rawCurrent = readRegister(5);
  reading.rawPower = (static_cast<uint32_t>(readRegister(9)) << 16) | readRegister(7);
  reading.rawEnergy = (static_cast<uint32_t>(readRegister(13)) << 16) | readRegister(11);
  reading.rawHighVoltageAlarm = readRegister(15);
  reading.rawLowVoltageAlarm = readRegister(17);

  reading.voltage = static_cast<float>(reading.rawVoltage) / 100.0f;
  reading.current = static_cast<float>(reading.rawCurrent) / 100.0f;
  reading.power = static_cast<float>(reading.rawPower) / 10.0f;
  reading.energy = static_cast<float>(reading.rawEnergy);
  reading.highVoltageAlarm = reading.rawHighVoltageAlarm;
  reading.lowVoltageAlarm = reading.rawLowVoltageAlarm;
  return true;
}

bool readPzem017(Pzem017Reading &reading)
{
  return readPzem017AtAddress(kPzem017DefaultAddress, reading);
}

void printPzem017Reading(const Pzem017Reading &reading, float socPercent, float remainingCapacityAh)
{
#if ENABLE_PYTHON_JSON_OUTPUT
  Serial.print(F("{\"type\":\"pzem017\",\"ok\":true,\"millis\":"));
  Serial.print(millis());
  Serial.print(F(",\"voltage\":"));
  Serial.print(reading.voltage, 2);
  Serial.print(F(",\"current\":"));
  Serial.print(reading.current, 2);
  Serial.print(F(",\"power\":"));
  Serial.print(reading.power, 1);
  Serial.print(F(",\"energy_wh\":"));
  Serial.print(reading.energy, 0);
  Serial.print(F(",\"soc_percent\":"));
  if (isfinite(socPercent)) {
    Serial.print(socPercent, 1);
  } else {
    Serial.print(F("null"));
  }
  Serial.print(F(",\"remaining_ah\":"));
  if (isfinite(remainingCapacityAh)) {
    Serial.print(remainingCapacityAh, 1);
  } else {
    Serial.print(F("null"));
  }
  Serial.print(F(",\"raw_voltage\":"));
  Serial.print(reading.rawVoltage);
  Serial.print(F(",\"raw_current\":"));
  Serial.print(reading.rawCurrent);
  Serial.print(F(",\"raw_power\":"));
  Serial.print(reading.rawPower);
  Serial.print(F(",\"raw_energy\":"));
  Serial.print(reading.rawEnergy);
  Serial.print(F(",\"high_voltage_alarm\":"));
  Serial.print(reading.highVoltageAlarm);
  Serial.print(F(",\"low_voltage_alarm\":"));
  Serial.print(reading.lowVoltageAlarm);
  Serial.println(F("}"));
#else
  Serial.print(F("PZEM-017 V="));
  Serial.print(reading.voltage, 2);
  Serial.print(F("V I="));
  Serial.print(reading.current, 2);
  Serial.print(F("A P="));
  Serial.print(reading.power, 1);
  Serial.print(F("W E="));
  Serial.print(reading.energy, 0);
  Serial.print(F("Wh SoC="));

  if (isfinite(socPercent)) {
    Serial.print(socPercent, 1);
    Serial.print(F("% RemAh="));
    Serial.println(remainingCapacityAh, 1);
  } else {
    Serial.println(F("N/A"));
  }

  Serial.print(F("RAW V="));
  Serial.print(reading.rawVoltage);
  Serial.print(F(" I="));
  Serial.print(reading.rawCurrent);
  Serial.print(F(" P="));
  Serial.print(reading.rawPower);
  Serial.print(F(" E="));
  Serial.print(reading.rawEnergy);
  Serial.print(F(" HV="));
  Serial.print(reading.rawHighVoltageAlarm);
  Serial.print(F(" LV="));
  Serial.println(reading.rawLowVoltageAlarm);
#endif
}

void printPzem017ReadError()
{
#if ENABLE_PYTHON_JSON_OUTPUT
  Serial.print(F("{\"type\":\"pzem017\",\"ok\":false,\"millis\":"));
  Serial.print(millis());
  Serial.println(F(",\"error\":\"read_failed\"}"));
#else
  Serial.println(F("PZEM-017 read failed"));
#endif
}

// ---------------------------------------------------------------------------
// แก้ไข / อ่าน ID (Modbus slave address) ของ PZEM-003/017
// ---------------------------------------------------------------------------
// เขียนค่า ID ใหม่ลง holding register 0x0002 ด้วย function 0x06 (write single
// register) ผ่าน general address 0xF8 เพื่อให้ทำงานได้แม้ไม่ทราบ ID เดิม
// (ใช้ได้เฉพาะเมื่อมีอุปกรณ์ตัวเดียวบนบัส RS485)
bool setPzem017Address(uint8_t newAddress)
{
  if (newAddress < kPzemAddressMin || newAddress > kPzemAddressMax) {
    return false;
  }

  HardwareSerial &pzemPort = Serial1;

  while (pzemPort.available() > 0) {
    (void)pzemPort.read();
  }

  uint8_t request[8] = {
    kPzemGeneralAddress, 0x06,
    static_cast<uint8_t>(kPzemRegSlaveAddress >> 8),
    static_cast<uint8_t>(kPzemRegSlaveAddress & 0xFF),
    0x00, newAddress, 0x00, 0x00};
  uint16_t requestCrc = crc16Modbus(request, sizeof(request) - 2);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFF);
  request[7] = static_cast<uint8_t>(requestCrc >> 8);

  setRs485Transmit(true);
  pzemPort.write(request, sizeof(request));
  pzemPort.flush();
  setRs485Transmit(false);

  // function 0x06 ตอบกลับเป็น echo ของคำสั่งทั้งเฟรม
  uint8_t response[8] = {};
  uint32_t start = millis();
  size_t index = 0;
  while (index < sizeof(response) && (millis() - start) < 200U) {
    if (pzemPort.available() > 0) {
      response[index++] = static_cast<uint8_t>(pzemPort.read());
    }
  }

  if (index != sizeof(response)) {
    return false;
  }

  for (uint8_t i = 0; i < 6; ++i) {
    if (response[i] != request[i]) {
      return false;
    }
  }

  uint16_t responseCrc = crc16Modbus(response, sizeof(response) - 2);
  uint16_t receivedCrc = static_cast<uint16_t>(response[6]) |
                         static_cast<uint16_t>(response[7] << 8);
  return responseCrc == receivedCrc;
}

// อ่าน ID ปัจจุบันจาก holding register 0x0002 ด้วย function 0x03 (ผ่าน 0xF8)
bool readPzem017Address(uint8_t &outAddress)
{
  HardwareSerial &pzemPort = Serial1;

  while (pzemPort.available() > 0) {
    (void)pzemPort.read();
  }

  uint8_t request[8] = {
    kPzemGeneralAddress, 0x03,
    static_cast<uint8_t>(kPzemRegSlaveAddress >> 8),
    static_cast<uint8_t>(kPzemRegSlaveAddress & 0xFF),
    0x00, 0x01, 0x00, 0x00};
  uint16_t requestCrc = crc16Modbus(request, sizeof(request) - 2);
  request[6] = static_cast<uint8_t>(requestCrc & 0xFF);
  request[7] = static_cast<uint8_t>(requestCrc >> 8);

  setRs485Transmit(true);
  pzemPort.write(request, sizeof(request));
  pzemPort.flush();
  setRs485Transmit(false);

  // ตอบกลับ: addr, 0x03, byteCount(2), valHi, valLo, crcLo, crcHi
  uint8_t response[7] = {};
  uint32_t start = millis();
  size_t index = 0;
  while (index < sizeof(response) && (millis() - start) < 200U) {
    if (pzemPort.available() > 0) {
      response[index++] = static_cast<uint8_t>(pzemPort.read());
    }
  }

  if (index != sizeof(response)) {
    return false;
  }

  if (response[1] != 0x03 || response[2] != 0x02) {
    return false;
  }

  uint16_t responseCrc = crc16Modbus(response, sizeof(response) - 2);
  uint16_t receivedCrc = static_cast<uint16_t>(response[5]) |
                         static_cast<uint16_t>(response[6] << 8);
  if (responseCrc != receivedCrc) {
    return false;
  }

  outAddress = response[4]; // low byte ของค่า register = ID
  return true;
}
