#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <HardwareSerial.h>

HardwareSerial Serial1(PB7, PA9); // RX, TX

#define LED_BUILTIN PA10

constexpr uint16_t kPixelCount = 144;
constexpr uint8_t kPixelPin = PA8;
constexpr uint8_t kDefaultBrightness = 40;

#define ENABLE_PZEM017_SOC 1
#define ENABLE_SOLID_COLOR_TEST 0
#define ENABLE_RAINBOW_ANIMATION 0

constexpr uint8_t kPzem017Address = 0x01;
constexpr uint32_t kPzem017BaudRate = 9600;
constexpr uint8_t kPzem017RegisterCount = 8;
constexpr uint16_t kPzem017ResponseSize = 3 + (kPzem017RegisterCount * 2) + 2;

constexpr float kBatteryEmptyVoltage = 10.5f;
constexpr float kBatteryFullVoltage = 12.8f;

// Set to a valid GPIO pin if your RS485 transceiver needs DE/RE control.
constexpr int8_t kRs485DirectionPin = -1;

Adafruit_NeoPixel strip(kPixelCount, kPixelPin, NEO_GRB + NEO_KHZ800);

struct Pzem017Reading
{
  uint16_t rawVoltage = 0;
  uint16_t rawCurrent = 0;
  uint32_t rawPower = 0;
  uint32_t rawEnergy = 0;
  uint16_t rawHighVoltageAlarm = 0;
  uint16_t rawLowVoltageAlarm = 0;
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  uint16_t highVoltageAlarm = 0;
  uint16_t lowVoltageAlarm = 0;
};

uint16_t crc16Modbus(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFF;

  while (length--) {
    crc ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}
void setRs485Transmit(bool enabled)
{
  if (kRs485DirectionPin < 0) {
    return;
  }

  digitalWrite(static_cast<uint8_t>(kRs485DirectionPin), enabled ? HIGH : LOW);
}

bool readPzem017(Pzem017Reading &reading)
{
  HardwareSerial &pzemPort = Serial1;
  static bool initialized = false;

  if (!initialized) {
    if (kRs485DirectionPin >= 0) {
      pinMode(static_cast<uint8_t>(kRs485DirectionPin), OUTPUT);
      setRs485Transmit(false);
    }

    pzemPort.begin(kPzem017BaudRate, SERIAL_8N2);
    initialized = true;
  }

  while (pzemPort.available() > 0) {
    (void)pzemPort.read();
  }

  uint8_t request[] = {kPzem017Address, 0x04, 0x00, 0x00, 0x00, kPzem017RegisterCount, 0x00, 0x00};
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

  if (response[0] != kPzem017Address || response[1] != 0x04 || response[2] != (kPzem017RegisterCount * 2)) {
    return false;
  }

  auto readRegister = [&](size_t offset) -> uint16_t {
    return static_cast<uint16_t>(response[offset] << 8) | response[offset + 1];
  };

  reading.rawVoltage = readRegister(3);
  reading.rawCurrent = readRegister(5);
  reading.rawPower = (static_cast<uint32_t>(readRegister(7)) << 16) | readRegister(9);
  reading.rawEnergy = (static_cast<uint32_t>(readRegister(11)) << 16) | readRegister(13);
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

float estimateSocFromVoltage(float voltage)
{
  if (!isfinite(voltage)) {
    return NAN;
  }

  if (voltage <= kBatteryEmptyVoltage) {
    return 0.0f;
  }

  if (voltage >= kBatteryFullVoltage) {
    return 100.0f;
  }

  return ((voltage - kBatteryEmptyVoltage) * 100.0f) / (kBatteryFullVoltage - kBatteryEmptyVoltage);
}

void printPzem017Reading(const Pzem017Reading &reading, float socPercent)
{
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
    Serial.println(F("%"));
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
}

void showSolidColor(uint8_t red, uint8_t green, uint8_t blue)
{
  for (uint16_t i = 0; i < kPixelCount; ++i) {
    strip.setPixelColor(i, strip.Color(red, green, blue));
  }
  strip.show();
}

void showRainbowAnimation()
{
  for (uint16_t offset = 0; offset < 65535; offset += 256) {
    strip.clear();

    for (uint16_t i = 0; i < kPixelCount; ++i) {
      uint16_t hue = static_cast<uint16_t>(offset + (i * 65535UL / kPixelCount));
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
    }

    strip.show();
    delay(20);
  }
}

void setup()
{

  
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.setRx(PA15);
  Serial.setTx(PA2);
  Serial.begin(115200);

  // for (uint8_t i = 0; i < 1000; ++i) 
  // {
  //   Serial.println(F("go..."));
  //   Serial.println();
    
  //   digitalWrite(LED_BUILTIN, HIGH);
  //   delay(200);
  //   digitalWrite(LED_BUILTIN, LOW);
  //   delay(200);
  // }
  
  Serial1.begin(9600);
  
  strip.begin();
  strip.setBrightness(kDefaultBrightness);
  strip.show();
}

void loop()
{
#if ENABLE_PZEM017_SOC
  static uint32_t lastReadMs = 0;

  if (millis() - lastReadMs >= 1000U) {
    lastReadMs = millis();

    Pzem017Reading reading;
    if (readPzem017(reading)) {
      float socPercent = estimateSocFromVoltage(reading.voltage);
      printPzem017Reading(reading, socPercent);
    } else {
      Serial.println(F("PZEM-017 read failed"));
    }
  }
#elif ENABLE_SOLID_COLOR_TEST
  showSolidColor(255, 0, 0);
  delay(1000);
#elif ENABLE_RAINBOW_ANIMATION
  showRainbowAnimation();
#else
  delay(1000);
#endif
}
