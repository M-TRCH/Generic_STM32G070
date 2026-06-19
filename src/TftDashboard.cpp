#include "TftDashboard.h"
#include "Storage.h"

#include <SPI.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
// จอ TFT 3.5" ILI9488 + ทัช XPT2046 (บัส SPI1 ร่วมกัน)
// กำหนดขา/ไดรเวอร์ทั้งหมดผ่าน build_flags ใน platformio.ini
//
//   TFT_SCK / T_CLK  PA5   |  TFT_MOSI / T_DIN PB5  |  T_DO (MISO) PB4
//   TFT_CS  PB14  |  TFT_DC/RS PC7  |  TFT_RESET PC6
//   TFT_LED PB0   |  T_CS PB15
// ---------------------------------------------------------------------------
namespace {

TFT_eSPI tft = TFT_eSPI();

constexpr uint8_t kTftBacklightPin = PB0;

struct DashboardField
{
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
  int16_t labelX;
  int16_t labelY;
  int16_t valueX;
  int16_t valueY;
  uint16_t accentColor;
  const char *label;
};

constexpr uint16_t kBgColor = TFT_BLACK;
constexpr uint16_t kPanelFill = 0x1082;
constexpr uint16_t kPanelBorder = 0x31A6;
constexpr uint16_t kHeaderFill = 0x0106;
constexpr uint16_t kHeaderBorder = 0x2A69;
constexpr uint16_t kHeaderValue = TFT_SKYBLUE;
constexpr int16_t kHeaderRuntimeLabelY = 48;
constexpr int16_t kHeaderRuntimeValueY = 46;

constexpr DashboardField kRoomTempField = {18, 86, 216, 64, 32, 96, 220, 128, TFT_YELLOW, "ROOM TEMP"};
constexpr DashboardField kRoomHumField = {18, 160, 216, 64, 32, 170, 220, 202, TFT_GOLD, "ROOM HUM"};
constexpr DashboardField kRefrigTempField = {246, 86, 216, 64, 260, 96, 448, 128, TFT_GREENYELLOW, "REFRIG TEMP"};
constexpr DashboardField kRefrigHumField = {246, 160, 216, 64, 260, 170, 448, 202, TFT_CYAN, "REFRIG HUM"};
constexpr DashboardField kCurrentField = {18, 234, 216, 64, 32, 244, 220, 276, TFT_ORANGE, "CURRENT"};
constexpr DashboardField kLastRuntimeField = {246, 234, 216, 64, 260, 244, 448, 276, TFT_MAGENTA, "LAST RUNTIME"};

// format runtime เช่น 1d 02h 15m หรือ 03h 12m 09s
void formatRuntime(uint32_t seconds, char *buffer, size_t bufferSize)
{
  const uint32_t days = seconds / kSecondsPerDay;
  const uint32_t hours = (seconds % kSecondsPerDay) / kSecondsPerHour;
  const uint32_t minutes = (seconds % kSecondsPerHour) / kSecondsPerMinute;
  const uint32_t secs = seconds % kSecondsPerMinute;

  if (days > 0U) {
    snprintf(buffer, bufferSize, "%lud %02luh %02lum",
             static_cast<unsigned long>(days), static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  } else {
    snprintf(buffer, bufferSize, "%02luh %02lum %02lus",
             static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes),
             static_cast<unsigned long>(secs));
  }
}

uint8_t chooseFontForWidth(const char *text, int16_t maxWidth, uint8_t preferredFont, uint8_t fallbackFont)
{
  if (tft.textWidth(text, preferredFont) <= maxWidth) {
    return preferredFont;
  }
  return fallbackFont;
}

void drawPanel(const DashboardField &field)
{
  tft.fillRoundRect(field.x, field.y, field.w, field.h, 10, kPanelFill);
  tft.drawRoundRect(field.x, field.y, field.w, field.h, 10, kPanelBorder);
  tft.fillRect(field.x + 1, field.y + 1, 5, field.h - 2, field.accentColor);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, kPanelFill);
  tft.drawString(field.label, field.labelX, field.labelY, 2);
}

void drawFieldValue(const DashboardField &field, const char *value, uint16_t color)
{
  const int16_t clearX = field.x + 7;
  const int16_t clearY = field.y + 2;
  const int16_t clearW = field.w - 10;
  const int16_t clearH = field.h - 4;
  const int16_t valueMaxWidth = field.w - 26;
  const uint8_t valueFont = chooseFontForWidth(value, clearW, 4, 2);
  const int16_t valueY = (valueFont == 4) ? field.valueY : static_cast<int16_t>(field.valueY - 2);

  tft.fillRect(clearX, clearY, clearW, clearH, kPanelFill);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_LIGHTGREY, kPanelFill);
  tft.drawString(field.label, field.labelX, field.labelY, 2);

  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(color, kPanelFill);
  tft.drawString(value, field.valueX, valueY,
                 chooseFontForWidth(value, valueMaxWidth, 4, 2));
}

// วาดค่าเฉพาะเมื่อข้อความเปลี่ยน (ลดทราฟิก SPI + กันกระพริบ)
void drawValueIfChanged(const DashboardField &field, const char *value,
                        uint16_t color, char *cache, size_t cacheSize)
{
  if (strncmp(cache, value, cacheSize) == 0) {
    return;
  }
  snprintf(cache, cacheSize, "%s", value);
  drawFieldValue(field, value, color);
}

// แปลงค่าเซ็นเซอร์เป็นข้อความ (รองรับ NaN → "--")
void formatSensorValue(float value, uint8_t decimals, const char *unit,
                       char *buffer, size_t bufferSize)
{
  if (isfinite(value)) {
    char number[16] = {};
    dtostrf(value, 0, decimals, number);
    snprintf(buffer, bufferSize, "%s %s", number, unit);
  } else {
    snprintf(buffer, bufferSize, "-- %s", unit);
  }
}

} // namespace

void initTftDashboard()
{
  SPI.setSCLK(PA5);
  SPI.setMOSI(PB5);
  SPI.setMISO(PB4);

  pinMode(kTftBacklightPin, OUTPUT);
  digitalWrite(kTftBacklightPin, HIGH);

  tft.init();
  tft.setRotation(1); // แนวนอน (480 x 320)

  tft.fillScreen(kBgColor);

  tft.fillRoundRect(12, 10, tft.width() - 24, 62, 14, kHeaderFill);
  tft.drawRoundRect(12, 10, tft.width() - 24, 62, 14, kHeaderBorder);
  tft.drawFastHLine(18, 78, tft.width() - 36, TFT_DARKGREY);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, kHeaderFill);
  tft.drawString("LGS TS Cool", 28, 18, 4);
  tft.setTextColor(TFT_DARKGREY, kHeaderFill);
  tft.drawString("CURRENT RUNTIME", 30, kHeaderRuntimeLabelY, 2);

  drawPanel(kRoomTempField);
  drawPanel(kRoomHumField);
  drawPanel(kRefrigTempField);
  drawPanel(kRefrigHumField);
  drawPanel(kCurrentField);
  drawPanel(kLastRuntimeField);
}

void updateTftDashboard(const Sht40Reading &sht, const Sht40Reading &sht31,
                        const Ina180Reading &ina, uint32_t runtimeSeconds,
                        uint32_t savedRuntimeSeconds)
{
  char buffer[24] = {};
  static char headerRuntimeCache[16] = {};
  static char tempCache[16] = {};
  static char humCache[16] = {};
  static char temp31Cache[16] = {};
  static char hum31Cache[16] = {};
  static char currentCache[16] = {};
  static char lastRuntimeCache[16] = {};

  formatRuntime(runtimeSeconds, buffer, sizeof(buffer));
  if (strncmp(headerRuntimeCache, buffer, sizeof(headerRuntimeCache)) != 0) {
    const int16_t headerRuntimeX = 438;
    const int16_t headerRuntimeAreaX = 220;
    const int16_t headerRuntimeAreaW = 220;
    const uint8_t headerRuntimeFont = chooseFontForWidth(buffer, headerRuntimeAreaW, 4, 2);
    snprintf(headerRuntimeCache, sizeof(headerRuntimeCache), "%s", buffer);
    tft.fillRect(headerRuntimeAreaX, 38, headerRuntimeAreaW, 18, kHeaderFill);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(kHeaderValue, kHeaderFill);
    tft.drawString(buffer, headerRuntimeX, kHeaderRuntimeValueY, headerRuntimeFont);
  }

  formatSensorValue(sht.temperatureC, 2, "C", buffer, sizeof(buffer));
  drawValueIfChanged(kRoomTempField, buffer, kRoomTempField.accentColor, tempCache, sizeof(tempCache));

  formatSensorValue(sht.humidityPercent, 2, "%", buffer, sizeof(buffer));
  drawValueIfChanged(kRoomHumField, buffer, kRoomHumField.accentColor, humCache, sizeof(humCache));

  formatSensorValue(sht31.temperatureC, 2, "C", buffer, sizeof(buffer));
  drawValueIfChanged(kRefrigTempField, buffer, kRefrigTempField.accentColor, temp31Cache, sizeof(temp31Cache));

  formatSensorValue(sht31.humidityPercent, 2, "%", buffer, sizeof(buffer));
  drawValueIfChanged(kRefrigHumField, buffer, kRefrigHumField.accentColor, hum31Cache, sizeof(hum31Cache));

  formatSensorValue(ina.currentAmps, 3, "A", buffer, sizeof(buffer));
  drawValueIfChanged(kCurrentField, buffer, kCurrentField.accentColor, currentCache, sizeof(currentCache));

  formatRuntime(savedRuntimeSeconds, buffer, sizeof(buffer));
  drawValueIfChanged(kLastRuntimeField, buffer, kLastRuntimeField.accentColor, lastRuntimeCache, sizeof(lastRuntimeCache));
}
