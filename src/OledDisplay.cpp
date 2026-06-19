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
constexpr const uint8_t *kOledCompactFont = u8g2_font_helvR08_tr;
constexpr const uint8_t *kOledWideFont = u8g2_font_helvR10_tr;

// U8g2 ใช้ที่อยู่ I2C แบบ 8-bit; SSD1306 (7-bit 0x3C) จึงถูกเลื่อนเป็น 0x78
constexpr uint8_t kDisplayI2cAddress =
  (OLED_PANEL_TYPE == OLED_PANEL_130) ? OLED_ADDR_130 : static_cast<uint8_t>(OLED_ADDR_096 << 1);

constexpr int16_t kOledLine1Y = 11;
constexpr int16_t kOledLineSpacing = 12;
constexpr int16_t kOledWideLine1Y = 13;
constexpr int16_t kOledWideLineSpacing = 18;

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

} // namespace

void initOledDisplay()
{
  display.setI2CAddress(kDisplayI2cAddress);
  display.begin();

#if OLED_PANEL_TYPE == OLED_PANEL_130
  Serial.println(F("OLED driver: SH1106"));
#else
  Serial.println(F("OLED driver: SSD1306"));
#endif
  Serial.print(F("OLED I2C addr: 0x"));
  Serial.println(kDisplayI2cAddress, HEX);
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
