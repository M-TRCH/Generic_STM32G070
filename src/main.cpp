#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SSD1306.h>
#include <HardwareSerial.h>
#include <Wire.h>

// Pin definitions and constants
HardwareSerial Serial1(PB7, PA9); // RX, TX
#define LED_BUILTIN     PA10
#define LATCH_TRIG_PIN  PB3   // Latch trigger pin
#define LATCH_ULK_PIN   PA3   // Latch unlock pin (active LOW)

constexpr uint16_t kPixelCount = uint16_t(144 * 3); // 144 pixels per strip
constexpr uint8_t kPixelPin = PA8;
constexpr uint8_t kDefaultBrightness = 40;
constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr int8_t kDisplayResetPin = -1;
constexpr uint8_t kDisplayAddress = 0x3C;

#define ENABLE_PZEM017_SOC          0
#define ENABLE_SOLID_COLOR_TEST     0
#define ENABLE_RAINBOW_ANIMATION    0
#define ENABLE_PYTHON_JSON_OUTPUT   0
#define ENABLE_LATCH_CONTROL_TEST   1

#define SOC_BATTERY_CAPACITY_AH   18.0f
#define SOC_CHARGE_MAX_VOLTAGE    29.2f
#define SOC_FULL_REST_VOLTAGE     28.7f
#define SOC_EMPTY_VOLTAGE         20.5f

// SOC_CURRENT_SIGN: -1.0 = PZEM วัดกระแส discharge (SoC ลด),
//                  +1.0 = PZEM วัดกระแส charge (SoC เพิ่ม)
#define SOC_CURRENT_SIGN          -1.0f
// กระแสต่ำกว่าค่านี้ (A) = แบตพัก → ใช้ OCV table ปรับ SoC
#define SOC_RESTING_THRESHOLD_A   0.10f
// ชาร์จเต็มเมื่อแรงดัน >= SOC_CHARGE_MAX_VOLTAGE และกระแส < ค่านี้ (A)
#define SOC_CHARGE_COMPLETE_A     0.20f

constexpr uint8_t kPzem017Address = 0x01;
constexpr uint32_t kPzem017BaudRate = 9600;
constexpr uint8_t kPzem017RegisterCount = 8;
constexpr uint16_t kPzem017ResponseSize = 3 + (kPzem017RegisterCount * 2) + 2;

// Set to a valid GPIO pin if your RS485 transceiver needs DE/RE control.
constexpr int8_t kRs485DirectionPin = -1;

Adafruit_NeoPixel strip(kPixelCount, kPixelPin, NEO_GRB + NEO_KHZ800);
Adafruit_SSD1306 display(kDisplayWidth, kDisplayHeight, &Wire, kDisplayResetPin);

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

// OCV table สำหรับ LiFePO4 8S (แรงดันพักเทียบกับ SoC)
// ที่มา: ข้อมูลมาตรฐาน LiFePO4 × 8 เซล
struct OcvPoint
{
  float voltage;
  float soc;
};

static const OcvPoint kOcvTable[] = {
  { 29.20f, 100.0f },  // 3.650 V/cell - cutoff ชาร์จ
  { 27.20f,  95.0f },  // 3.400 V/cell - พักหลังชาร์จเต็ม
  { 26.80f,  90.0f },  // 3.350 V/cell
  { 26.64f,  80.0f },  // 3.330 V/cell
  { 26.48f,  70.0f },  // 3.310 V/cell
  { 26.32f,  60.0f },  // 3.290 V/cell
  { 26.16f,  50.0f },  // 3.270 V/cell (โซนแบน LiFePO4)
  { 26.00f,  40.0f },  // 3.250 V/cell
  { 25.76f,  30.0f },  // 3.220 V/cell
  { 25.36f,  20.0f },  // 3.170 V/cell
  { 24.24f,  10.0f },  // 3.030 V/cell
  { 22.40f,   5.0f },  // 2.800 V/cell
  { 20.50f,   0.0f },  // 2.563 V/cell - cutoff ว่าง (= SOC_EMPTY_VOLTAGE)
};

static const uint8_t kOcvTableSize = sizeof(kOcvTable) / sizeof(kOcvTable[0]);

float lookupSocFromOcv(float voltage)
{
  if (voltage >= kOcvTable[0].voltage) {
    return 100.0f;
  }

  if (voltage <= kOcvTable[kOcvTableSize - 1].voltage) {
    return 0.0f;
  }

  for (uint8_t i = 0; i < kOcvTableSize - 1; ++i) {
    if (voltage <= kOcvTable[i].voltage && voltage >= kOcvTable[i + 1].voltage) {
      float ratio = (voltage - kOcvTable[i + 1].voltage)
                    / (kOcvTable[i].voltage - kOcvTable[i + 1].voltage);
      return kOcvTable[i + 1].soc + ratio * (kOcvTable[i].soc - kOcvTable[i + 1].soc);
    }
  }

  return NAN;
}

// Hybrid SoC: Coulomb counting หลัก + OCV calibration + voltage anchor
float updateSocState(const Pzem017Reading &reading)
{
  static float socPercent = NAN;
  static uint32_t lastMs = 0;

  const uint32_t nowMs = millis();
  const float voltageV = reading.voltage;
  const float currentA = reading.current;

  if (!isfinite(voltageV) || !isfinite(currentA)) {
    return socPercent;
  }

  // Anchor: ชาร์จเต็ม → reset เป็น 100%
  if (voltageV >= SOC_CHARGE_MAX_VOLTAGE && currentA < SOC_CHARGE_COMPLETE_A) {
    socPercent = 100.0f;
    lastMs = nowMs;
    return socPercent;
  }

  // Anchor: หมด → reset เป็น 0%
  if (voltageV <= SOC_EMPTY_VOLTAGE) {
    socPercent = 0.0f;
    lastMs = nowMs;
    return socPercent;
  }

  // ยังไม่มีค่าเริ่มต้น → ดึงจาก OCV table
  if (!isfinite(socPercent)) {
    socPercent = lookupSocFromOcv(voltageV);
    lastMs = nowMs;
    return socPercent;
  }

  // แบตพัก (กระแสต่ำมาก) → ค่อยๆ drift เข้าหา OCV
  if (currentA < SOC_RESTING_THRESHOLD_A) {
    float ocvSoc = lookupSocFromOcv(voltageV);
    if (isfinite(ocvSoc)) {
      socPercent = socPercent * 0.98f + ocvSoc * 0.02f;
    }
    lastMs = nowMs;
    return socPercent;
  }

  // Coulomb counting (กระแสมีนัยสำคัญ)
  if (lastMs != 0U) {
    const float dtHours = static_cast<float>(nowMs - lastMs) / 3600000.0f;
    const float deltaSoc = (currentA * dtHours / SOC_BATTERY_CAPACITY_AH) * 100.0f;
    socPercent += SOC_CURRENT_SIGN * deltaSoc;
    socPercent = constrain(socPercent, 0.0f, 100.0f);
  }

  lastMs = nowMs;
  return socPercent;
}

float estimateRemainingCapacityAh(float socPercent)
{
  if (!isfinite(socPercent)) {
    return NAN;
  }

  return (SOC_BATTERY_CAPACITY_AH * socPercent) / 100.0f;
}

void showOledMessage(const __FlashStringHelper *line1, const __FlashStringHelper *line2)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.println();
  display.println(line2);
  display.display();
}

void updateOledDisplay(const Pzem017Reading &reading, float socPercent, float remainingCapacityAh)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.print(F("V: "));
  display.print(reading.voltage, 2);
  display.println(F(" V"));

  display.print(F("I: "));
  display.print(reading.current, 2);
  display.println(F(" A"));

  display.print(F("P: "));
  display.print(reading.power, 1);
  display.println(F(" W"));

  display.print(F("E: "));
  display.print(reading.energy, 0);
  display.println(F(" Wh"));

  display.print(F("SoC: "));
  if (isfinite(socPercent)) {
    display.print(socPercent, 1);
    display.print(F(" %  Ah: "));
    display.println(remainingCapacityAh, 1);
  } else {
    display.println(F("N/A"));
  }

  display.display();
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

void latchControlTest()
{
  while(1)
  {
    uint8_t latchState = digitalRead(LATCH_ULK_PIN);
    Serial.print(F("Latch unlock pin state: "));
    Serial.println(latchState == LOW ? F("LOW (locked)") : F("HIGH (unlocked)"));

    if (latchState == HIGH) 
    {
      delay(5000);
    }
    else 
    {
      digitalWrite(LATCH_TRIG_PIN, HIGH);
      delay(450);
      digitalWrite(LATCH_TRIG_PIN, LOW);
      delay(100);
    }
  }
}

void setup()
{
  // Initialize the built-in LED pin as an output
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LATCH_TRIG_PIN, OUTPUT);
  pinMode(LATCH_ULK_PIN, INPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // debug serial port
  Serial.setRx(PA15);
  Serial.setTx(PA2);
  Serial.begin(115200);

  // rs485 serial port for PZEM-017
  Serial1.begin(kPzem017BaudRate, SERIAL_8N2);
  
  // neopixel setup
  strip.begin();
  strip.setBrightness(kDefaultBrightness);
  strip.show();

#if ENABLE_PZEM017_SOC 
  Wire.setSDA(PA12);
  Wire.setSCL(PB13);
  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, kDisplayAddress)) {
    Serial.println(F("SSD1306 init failed"));
  } else {
    showOledMessage(F("PZEM-017 OLED"), F("Display ready"));
  }
#endif
}

void loop()
{
#if ENABLE_PZEM017_SOC
  static uint32_t lastReadMs = 0;

  if (millis() - lastReadMs >= 1000U) {
    lastReadMs = millis();

    Pzem017Reading reading;
    if (readPzem017(reading)) {
      float socPercent = updateSocState(reading);
      float remainingCapacityAh = estimateRemainingCapacityAh(socPercent);
      printPzem017Reading(reading, socPercent, remainingCapacityAh);
      updateOledDisplay(reading, socPercent, remainingCapacityAh);
    } else {
      printPzem017ReadError();
      showOledMessage(F("PZEM-017"), F("Read failed"));
    }
  }
#elif ENABLE_SOLID_COLOR_TEST
  showSolidColor(255, 0, 0);
  delay(1000);
#elif ENABLE_RAINBOW_ANIMATION
  showRainbowAnimation();
#elif ENABLE_LATCH_CONTROL_TEST
  latchControlTest();
#else
  delay(1000);
#endif
}
