#include "OledDisplay.h"

#include <U8g2lib.h>
#include <Wire.h>

// ---------------------------------------------------------------------------
// จอ OLED 0.96" (SSD1306) หรือ 1.30" (SH1106) เลือกตาม OLED_PANEL_TYPE
// ---------------------------------------------------------------------------
#define OLED_PANEL_096  0
#define OLED_PANEL_130  1
#define OLED_PANEL_TYPE OLED_PANEL_130
#define OLED_ADDR_096   0x3C
#define OLED_ADDR_130   0x78

namespace {

constexpr uint8_t kDisplayWidth = 128;
constexpr uint8_t kDisplayHeight = 64;
constexpr const uint8_t *kOledCompactFont = u8g2_font_helvR08_tr;
constexpr const uint8_t *kOledWideFont = u8g2_font_helvR10_tr;

// U8g2 ใช้ที่อยู่ I2C แบบ 8-bit; SSD1306 (7-bit 0x3C) จึงถูกเลื่อนเป็น 0x78
constexpr uint8_t kDisplayI2cAddress =
  (OLED_PANEL_TYPE == OLED_PANEL_130) ? OLED_ADDR_130 : static_cast<uint8_t>(OLED_ADDR_096 << 1);

constexpr int16_t kOledLine1Y = 11;
constexpr int16_t kOledLineSpacing = 12;
constexpr int16_t kOledWideLine1Y = 13;
constexpr int16_t kOledWideLineSpacing = 18;
constexpr int16_t kTimeLineTextX = 2;
constexpr int16_t kTimeLineBoxPaddingX = 1;
constexpr int16_t kTimeLineBoxPaddingY = 1;

#if OLED_PANEL_TYPE == OLED_PANEL_130
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#else
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
#endif

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

void printFloatOrNa(float value, uint8_t decimals)
{
  if (isfinite(value)) {
    display.print(value, decimals);
  } else {
    display.print(F("N/A"));
  }
}

void printDurationOrNa(float hoursValue)
{
  if (!isfinite(hoursValue) || hoursValue < 0.0f) {
    display.print(F("N/A"));
    return;
  }

  const uint32_t totalMinutes = static_cast<uint32_t>(hoursValue * 60.0f);
  const uint32_t hours = totalMinutes / 60u;
  const uint32_t minutes = totalMinutes % 60u;

  display.print(hours);
  display.print(F("h"));
  if (minutes < 10u) {
    display.print('0');
  }
  display.print(minutes);
  display.print(F("m"));
}

void formatDurationOrNa(float hoursValue, char *buffer, size_t bufferSize)
{
  if (buffer == nullptr || bufferSize == 0u) {
    return;
  }

  if (!isfinite(hoursValue) || hoursValue < 0.0f) {
    snprintf(buffer, bufferSize, "N/A");
    return;
  }

  const uint32_t totalMinutes = static_cast<uint32_t>(hoursValue * 60.0f);
  const uint32_t hours = totalMinutes / 60u;
  const uint32_t minutes = totalMinutes % 60u;
  snprintf(buffer, bufferSize, "%luh%02lum", hours, minutes);
}

} // namespace

void initOledDisplay()
{
  display.setI2CAddress(kDisplayI2cAddress);
  display.begin();

#if !ENABLE_PYTHON_JSON_OUTPUT
#if OLED_PANEL_TYPE == OLED_PANEL_130
  Serial.println(F("OLED driver: SH1106"));
#else
  Serial.println(F("OLED driver: SSD1306"));
#endif
  Serial.print(F("OLED I2C addr: 0x"));
  Serial.println(kDisplayI2cAddress, HEX);
#endif
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

void updateOledDisplay(const Pzem017Reading &chargeReading, const Pzem017Reading &dischargeReading,
                       float socPercent, float remainingCapacityAh,
                       float timeRemainingHours, float timeToFullHours,
                       bool chargeAvailable, bool dischargeAvailable,
                       bool chargeStale, bool dischargeStale,
                       bool degradedMode)
{
  display.clearBuffer();
  display.setFont(kOledCompactFont);
  display.setCursor(0, oledLineY(0));
  if (!chargeAvailable) {
    display.print(F("CH FAIL"));
  } else {
    display.print(chargeStale ? F("CH*: ") : F("CH V:"));
    printFloatOrNa(chargeReading.voltage, 2);
    display.print(F(" I:"));
    printFloatOrNa(chargeReading.current, 2);
  }

  display.setCursor(0, oledLineY(1));
  if (!dischargeAvailable) {
    display.print(F("DS FAIL"));
  } else {
    display.print(dischargeStale ? F("DS*: ") : F("DS V:"));
    printFloatOrNa(dischargeReading.voltage, 2);
    display.print(F(" I:"));
    printFloatOrNa(dischargeReading.current, 2);
  }

  display.setCursor(0, oledLineY(2));
  display.print(degradedMode ? F("SoC*: ") : F("SoC: "));
  if (isfinite(socPercent)) {
    display.print(socPercent, 1);
    display.print(F(" %  Ah: "));
    display.print(remainingCapacityAh, 1);
  } else {
    display.print(F("N/A"));
  }

  display.setCursor(0, oledLineY(3));
  if (degradedMode) {
    display.print(F("STATE: DEGRADED"));
  } else if (chargeStale || dischargeStale) {
    display.print(F("STATE: STALE"));
  } else {
    display.print(F("STATE: OK"));
  }

  char timeRemainingText[16] = {};
  char timeToFullText[16] = {};
  char timeLineText[40] = {};
  formatDurationOrNa(timeRemainingHours, timeRemainingText, sizeof(timeRemainingText));
  formatDurationOrNa(timeToFullHours, timeToFullText, sizeof(timeToFullText));
  snprintf(timeLineText, sizeof(timeLineText), "Trem:%s Tf:%s", timeRemainingText, timeToFullText);

  const int16_t timeLineBaselineY = oledLineY(4);
  const int16_t timeLineTextWidth = static_cast<int16_t>(display.getStrWidth(timeLineText));
  const int16_t fontAscent = display.getAscent();
  const int16_t fontDescent = display.getDescent();
  const int16_t timeLineTop = max<int16_t>(0, timeLineBaselineY - fontAscent - kTimeLineBoxPaddingY);
  const int16_t timeLineBottom = min<int16_t>(kDisplayHeight - 1,
                                               timeLineBaselineY - fontDescent + kTimeLineBoxPaddingY);
  const int16_t timeLineHeight = max<int16_t>(1, timeLineBottom - timeLineTop + 1);
  const int16_t timeLineBoxWidth = min<int16_t>(kDisplayWidth - (kTimeLineTextX * 2),
                                                timeLineTextWidth + (kTimeLineBoxPaddingX * 2));
  display.drawBox(kTimeLineTextX - kTimeLineBoxPaddingX, timeLineTop, timeLineBoxWidth, timeLineHeight);
  display.setDrawColor(0);
  display.setCursor(kTimeLineTextX, timeLineBaselineY);
  display.print(timeLineText);
  display.setDrawColor(1);

  display.sendBuffer();
}
