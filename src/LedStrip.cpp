#include "LedStrip.h"

#include <Adafruit_NeoPixel.h>

namespace {

constexpr uint16_t kPixelCount = uint16_t(144 * 3); // 144 pixels per strip
constexpr uint8_t kPixelPin = PA8;
constexpr uint8_t kDefaultBrightness = 40;
constexpr uint16_t kRainbowHueStep = 256;
constexpr uint32_t kRainbowFrameIntervalMs = 20;

Adafruit_NeoPixel strip(kPixelCount, kPixelPin, NEO_GRB + NEO_KHZ800);

} // namespace

void initLedStrip()
{
  strip.begin();
  strip.setBrightness(kDefaultBrightness);
  strip.show();
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
  static uint16_t offset = 0;
  static uint32_t lastFrameMs = 0;

  const uint32_t nowMs = millis();
  if (nowMs - lastFrameMs < kRainbowFrameIntervalMs) {
    return;
  }

  lastFrameMs = nowMs;
  strip.clear();

  for (uint16_t i = 0; i < kPixelCount; ++i) {
    uint16_t hue = static_cast<uint16_t>(offset + (i * 65535UL / kPixelCount));
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue)));
  }

  strip.show();
  offset = static_cast<uint16_t>(offset + kRainbowHueStep);
}
