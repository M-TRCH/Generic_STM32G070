#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <HardwareSerial.h>
#include <U8g2lib.h>
#include <Wire.h>

// Pin definitions and constants
HardwareSerial Serial1(PB7, PA9); // RX, TX
TwoWire Wire1(PB9, PB6);          // SDA, SCL for SHT40

#define LED_BUILTIN     PA10
#define LATCH_TRIG_PIN  PB3   // Latch trigger pin
#define LATCH_ULK_PIN   PA3   // Latch unlock pin (active LOW)

constexpr uint16_t kPixelCount = uint16_t(144 * 3); // 144 pixels per strip
constexpr uint8_t kPixelPin = PA8;
constexpr uint8_t kDefaultBrightness = 40;

#define OLED_PANEL_096            0
#define OLED_PANEL_130            1
#define OLED_PANEL_TYPE           OLED_PANEL_130
#define OLED_ADDR_096             0x3C
#define OLED_ADDR_130             0x78

constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr int8_t kDisplayResetPin = -1;
constexpr const uint8_t *kOledCompactFont = u8g2_font_helvR08_tr;
constexpr const uint8_t *kOledWideFont = u8g2_font_helvR10_tr;
// U8g2 uses the 8-bit I2C address form. The 0.96-inch SSD1306 display is
// typically documented as 7-bit 0x3C, so it is shifted to 0x78 here.
constexpr uint8_t kDisplayI2cAddress =
  (OLED_PANEL_TYPE == OLED_PANEL_130) ? OLED_ADDR_130 : static_cast<uint8_t>(OLED_ADDR_096 << 1);

#define ENABLE_PZEM017_SOC          0
#define ENABLE_SHT40_TEST           1
#define ENABLE_SOLID_COLOR_TEST     0
#define ENABLE_RAINBOW_ANIMATION    0
#define ENABLE_PYTHON_JSON_OUTPUT   0
#define ENABLE_LATCH_CONTROL_TEST   0

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
constexpr uint8_t kSht40Address = 0x44;
constexpr uint8_t kSht40MeasureHighPrecision = 0xFD;

// Set to a valid GPIO pin if your RS485 transceiver needs DE/RE control.
constexpr int8_t kRs485DirectionPin = -1;

Adafruit_NeoPixel strip(kPixelCount, kPixelPin, NEO_GRB + NEO_KHZ800);
#if OLED_PANEL_TYPE == OLED_PANEL_130
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#endif

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

struct Sht40Reading
{
  float temperatureC = NAN;
  float humidityPercent = NAN;
};

// ---------------------------------------------------------------------------
// RTC (internal RTC of STM32G070CBT6, clocked from LSI ~32 kHz)
// ---------------------------------------------------------------------------
void printRtcReading(const RTC_TimeTypeDef &time, const RTC_DateTypeDef &date);

RTC_HandleTypeDef rtcHandle = {};

constexpr uint32_t kRtcInitMarker = 0xA5A55A5Au;
constexpr uint32_t kRtcDefaultYear = 24; // 2-digit year (2000 + value)
constexpr uint32_t kRtcDefaultMonth = RTC_MONTH_JANUARY;
constexpr uint32_t kRtcDefaultDate = 1;
constexpr uint32_t kRtcDefaultWeekDay = RTC_WEEKDAY_MONDAY;
constexpr uint32_t kRtcDefaultHours = 0;
constexpr uint32_t kRtcDefaultMinutes = 0;
constexpr uint32_t kRtcDefaultSeconds = 0;

uint32_t rtcBootCount = 0;

// Set true once, with the values below, to set the clock from code on boot.
// After uploading once, set back to false so the RTC keeps running.
constexpr bool kRtcSetFromCodeOnBoot = false;
constexpr uint16_t kRtcSetYear = 2026;
constexpr uint8_t kRtcSetMonth = 6;
constexpr uint8_t kRtcSetDate = 11;
constexpr uint8_t kRtcSetHours = 15;
constexpr uint8_t kRtcSetMinutes = 42;
constexpr uint8_t kRtcSetSeconds = 0;

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

uint8_t crc8Sensirion(const uint8_t *data, size_t length)
{
  uint8_t crc = 0xFF;

  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x80) {
        crc = static_cast<uint8_t>((crc << 1) ^ 0x31);
      } else {
        crc <<= 1;
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

// Override the weak HAL_RTC_MspInit. All clock setup is done manually in
// initRtc() before HAL_RTC_Init is called, so nothing extra is needed here.
extern "C" void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc)
{
  (void)hrtc;
}

bool initRtc()
{
  // STM32G0: enable the PWR peripheral clock, then unlock backup domain access
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  // Enable LSI and wait until it is ready
  __HAL_RCC_LSI_ENABLE();
  uint32_t lsiTimeout = HAL_GetTick() + 500u;
  while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {
    if (HAL_GetTick() > lsiTimeout) {
      Serial.println(F("RTC: LSI timeout"));
      return false;
    }
  }
  Serial.println(F("RTC: LSI ready"));

  // If the RTC clock source is not already LSI, reset the backup domain so the
  // RTCSEL bits can be changed. This also clears the backup registers, so the
  // init marker disappears and the default time is written below.
  if ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) != RCC_RTCCLKSOURCE_LSI) {
    Serial.println(F("RTC: resetting backup domain"));
    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();
    HAL_PWR_EnableBkUpAccess();
  }

  // Select LSI as the RTC kernel clock and enable it
  __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
  __HAL_RCC_RTC_ENABLE();

  // STM32G0 has a SEPARATE RTC register-interface (APB) clock. Without it the
  // RTC registers cannot be accessed and HAL_RTC_Init() fails. This is the key
  // difference from F1/F4 families.
  __HAL_RCC_RTCAPB_CLK_ENABLE();

  rtcHandle.Instance = RTC;
  rtcHandle.Init.HourFormat = RTC_HOURFORMAT_24;
  // LSI ~32 kHz: 32000 / (127+1) / (249+1) = 1 Hz
  rtcHandle.Init.AsynchPrediv = 127;
  rtcHandle.Init.SynchPrediv = 249;
  rtcHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
  rtcHandle.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  rtcHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  rtcHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

  if (HAL_RTC_Init(&rtcHandle) != HAL_OK) {
    Serial.println(F("RTC: HAL_RTC_Init failed"));
    return false;
  }
  Serial.println(F("RTC: HAL_RTC_Init OK"));

  if (HAL_RTCEx_BKUPRead(&rtcHandle, RTC_BKP_DR0) != kRtcInitMarker) {
    Serial.println(F("RTC: first init, setting default time"));
    RTC_TimeTypeDef time = {};
    RTC_DateTypeDef date = {};

    time.Hours = kRtcDefaultHours;
    time.Minutes = kRtcDefaultMinutes;
    time.Seconds = kRtcDefaultSeconds;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;

    date.Year = kRtcDefaultYear;
    date.Month = kRtcDefaultMonth;
    date.Date = kRtcDefaultDate;
    date.WeekDay = kRtcDefaultWeekDay;

    if (HAL_RTC_SetTime(&rtcHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
      Serial.println(F("RTC: SetTime failed"));
      return false;
    }

    if (HAL_RTC_SetDate(&rtcHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
      Serial.println(F("RTC: SetDate failed"));
      return false;
    }

    HAL_RTCEx_BKUPWrite(&rtcHandle, RTC_BKP_DR0, kRtcInitMarker);
    HAL_RTCEx_BKUPWrite(&rtcHandle, RTC_BKP_DR1, 0);
  } else {
    Serial.println(F("RTC: already initialized, keeping time"));
  }

  return true;
}

bool readRtc(RTC_TimeTypeDef &time, RTC_DateTypeDef &date)
{
  // HAL_RTC_GetTime must be called before HAL_RTC_GetDate: reading the time
  // locks the shadow registers, and reading the date unlocks them.
  if (HAL_RTC_GetTime(&rtcHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }

  if (HAL_RTC_GetDate(&rtcHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }

  return true;
}

// Sakamoto's algorithm: returns 1=Monday .. 7=Sunday (STM32 RTC_WEEKDAY_*)
uint8_t calculateRtcWeekDay(uint16_t year, uint8_t month, uint8_t day)
{
  static const uint8_t kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

  if (month < 3) {
    --year;
  }

  uint16_t weekDay = static_cast<uint16_t>(year + (year / 4) - (year / 100) + (year / 400)
                                           + kMonthOffsets[month - 1] + day);
  weekDay %= 7; // 0 = Sunday

  return static_cast<uint8_t>(weekDay == 0 ? RTC_WEEKDAY_SUNDAY : weekDay);
}

bool setRtcDateTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hours, uint8_t minutes, uint8_t seconds)
{
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31
      || hours > 23 || minutes > 59 || seconds > 59) {
    return false;
  }

  RTC_TimeTypeDef time = {};
  RTC_DateTypeDef date = {};

  time.Hours = hours;
  time.Minutes = minutes;
  time.Seconds = seconds;
  time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time.StoreOperation = RTC_STOREOPERATION_RESET;

  date.Year = static_cast<uint8_t>(year - 2000);
  date.Month = month;
  date.Date = day;
  date.WeekDay = calculateRtcWeekDay(year, month, day);

  if (HAL_RTC_SetTime(&rtcHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }

  if (HAL_RTC_SetDate(&rtcHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }

  return true;
}

uint32_t incrementRtcBootCount()
{
  uint32_t bootCount = HAL_RTCEx_BKUPRead(&rtcHandle, RTC_BKP_DR1);
  ++bootCount;
  HAL_RTCEx_BKUPWrite(&rtcHandle, RTC_BKP_DR1, bootCount);
  return bootCount;
}

void printRtcReading(const RTC_TimeTypeDef &time, const RTC_DateTypeDef &date)
{
  char buffer[40] = {};
  snprintf(buffer, sizeof(buffer), "RTC %02u:%02u:%02u  Date %02u/%02u/20%02u",
           static_cast<unsigned>(time.Hours), static_cast<unsigned>(time.Minutes),
           static_cast<unsigned>(time.Seconds), static_cast<unsigned>(date.Date),
           static_cast<unsigned>(date.Month), static_cast<unsigned>(date.Year));
  Serial.println(buffer);
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

constexpr int16_t kOledLine1Y = 11;
constexpr int16_t kOledLineSpacing = 12;
constexpr int16_t kOledWideLine1Y = 13;
constexpr int16_t kOledWideLineSpacing = 18;
constexpr int16_t kOledLeftPadding = 4;
constexpr int16_t kOledHeaderHeight = 16;
constexpr int16_t kOledSensorBodyOffsetY = 2;

int16_t oledLineY(uint8_t lineIndex)
{
  return static_cast<int16_t>(kOledLine1Y + (lineIndex * kOledLineSpacing));
}

int16_t oledWideLineY(uint8_t lineIndex)
{
  return static_cast<int16_t>(kOledWideLine1Y + (lineIndex * kOledWideLineSpacing));
}

void drawCenteredText(int16_t y, const char *text)
{
  const int16_t textWidth = static_cast<int16_t>(display.getStrWidth(text));
  int16_t x = static_cast<int16_t>((kDisplayWidth - textWidth) / 2);
  if (x < 0) {
    x = 0;
  }

  display.setCursor(x, y);
  display.print(text);
}

void formatFloatText(float value, uint8_t decimals, char *buffer, size_t bufferSize)
{
  dtostrf(value, 0, decimals, buffer);

  while (*buffer == ' ') {
    ++buffer;
  }
}

void showOledMessage(const __FlashStringHelper *line1, const __FlashStringHelper *line2)
{
  char line1Buffer[24] = {};
  char line2Buffer[24] = {};

  snprintf(line1Buffer, sizeof(line1Buffer), "%s", reinterpret_cast<const char *>(line1));
  snprintf(line2Buffer, sizeof(line2Buffer), "%s", reinterpret_cast<const char *>(line2));

  display.clearBuffer();
  display.setFont(kOledWideFont);
  drawCenteredText(oledWideLineY(0), line1Buffer);
  drawCenteredText(oledWideLineY(2), line2Buffer);
  display.sendBuffer();
}

void updateOledDisplay(const Sht40Reading &reading, const RTC_TimeTypeDef &time,
                       const RTC_DateTypeDef &date)
{
  char timeText[12] = {};
  char dateLine[24] = {};
  char temperatureValue[12] = {};
  char humidityValue[12] = {};
  char temperatureLine[24] = {};
  char humidityLine[24] = {};

  snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u",
           static_cast<unsigned>(time.Hours), static_cast<unsigned>(time.Minutes),
           static_cast<unsigned>(time.Seconds));
  snprintf(dateLine, sizeof(dateLine), "Date: %02u/%02u/20%02u",
           static_cast<unsigned>(date.Date), static_cast<unsigned>(date.Month),
           static_cast<unsigned>(date.Year));

  dtostrf(reading.temperatureC, 0, 2, temperatureValue);
  dtostrf(reading.humidityPercent, 0, 2, humidityValue);
  snprintf(temperatureLine, sizeof(temperatureLine), "Temp: %s C", temperatureValue);
  snprintf(humidityLine, sizeof(humidityLine), "Hum : %s %%RH", humidityValue);

  display.clearBuffer();

  // Header bar shows the current time (inverted text on filled box)
  display.setFont(kOledWideFont);
  display.drawBox(0, 0, kDisplayWidth, kOledHeaderHeight);
  display.setDrawColor(0);
  drawCenteredText(oledWideLineY(0), timeText);
  display.setDrawColor(1);

  // Body: date, temperature, humidity
  display.setFont(kOledCompactFont);
  display.setCursor(kOledLeftPadding, oledLineY(2));
  display.print(dateLine);
  display.setCursor(kOledLeftPadding, oledLineY(3));
  display.print(temperatureLine);
  display.setCursor(kOledLeftPadding, oledLineY(4));
  display.print(humidityLine);

  display.sendBuffer();
}

void updateOledDisplay(const Pzem017Reading &reading, float socPercent, float remainingCapacityAh)
{
  display.clearBuffer();
  display.setFont(kOledCompactFont);
  display.setCursor(0, oledLineY(0));
  display.print(F("V: "));
  display.print(reading.voltage, 2);
  display.print(F(" V"));

  display.setCursor(0, oledLineY(1));
  display.print(F("I: "));
  display.print(reading.current, 2);
  display.print(F(" A"));

  display.setCursor(0, oledLineY(2));
  display.print(F("P: "));
  display.print(reading.power, 1);
  display.print(F(" W"));

  display.setCursor(0, oledLineY(3));
  display.print(F("E: "));
  display.print(reading.energy, 0);
  display.print(F(" Wh"));

  display.setCursor(0, oledLineY(4));
  display.print(F("SoC: "));
  if (isfinite(socPercent)) {
    display.print(socPercent, 1);
    display.print(F(" %  Ah: "));
    display.print(remainingCapacityAh, 1);
  } else {
    display.print(F("N/A"));
  }

  display.sendBuffer();
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

#if ENABLE_PZEM017_SOC || ENABLE_SHT40_TEST
  Wire.setSDA(PA12);
  Wire.setSCL(PB13);
  Wire.begin();

  display.setI2CAddress(kDisplayI2cAddress);
  display.begin();

#if OLED_PANEL_TYPE == OLED_PANEL_130
  Serial.println(F("OLED driver: SH1106"));
#else
  Serial.println(F("OLED driver: SSD1306"));
#endif

  Serial.print(F("OLED I2C addr: 0x"));
  Serial.println(kDisplayI2cAddress, HEX);

#if ENABLE_SHT40_TEST
  if (ENABLE_SHT40_TEST) {
    showOledMessage(F("SHT40 OLED"), F("Display ready"));
  } else {
    showOledMessage(F("PZEM-017 OLED"), F("Display ready"));
  }
#else
  showOledMessage(F("PZEM-017 OLED"), F("Display ready"));
#endif
#endif

#if ENABLE_SHT40_TEST
  Wire1.begin();
#endif

#if ENABLE_SHT40_TEST
  if (initRtc()) {
    if (kRtcSetFromCodeOnBoot) {
      if (setRtcDateTime(kRtcSetYear, kRtcSetMonth, kRtcSetDate,
                         kRtcSetHours, kRtcSetMinutes, kRtcSetSeconds)) {
        Serial.println(F("RTC set from code"));
      } else {
        Serial.println(F("RTC set from code failed"));
      }
    }

    rtcBootCount = incrementRtcBootCount();
    Serial.print(F("RTC boot count: "));
    Serial.println(rtcBootCount);
  } else {
    Serial.println(F("RTC init failed"));
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
#elif ENABLE_SHT40_TEST
  static uint32_t lastReadMs = 0;

  if (millis() - lastReadMs >= 1000U) {
    lastReadMs = millis();

    RTC_TimeTypeDef time = {};
    RTC_DateTypeDef date = {};
    bool rtcOk = readRtc(time, date);
    if (rtcOk) {
      printRtcReading(time, date);
    } else {
      Serial.println(F("RTC read failed"));
    }

    Sht40Reading reading;
    if (readSht40(reading)) {
      printSht40Reading(reading);
      if (rtcOk) {
        updateOledDisplay(reading, time, date);
      }
    } else {
      Serial.println(F("SHT40 read failed"));
      showOledMessage(F("SHT40"), F("Read failed"));
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
