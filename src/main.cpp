#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

constexpr uint16_t kPixelCount = 144;
constexpr uint8_t kPixelPin = PA8;
constexpr uint8_t kDefaultBrightness = 40;

#define ENABLE_SOLID_COLOR_TEST
#define ENABLE_RAINBOW_ANIMATION 

Adafruit_NeoPixel strip(kPixelCount, kPixelPin, NEO_GRB + NEO_KHZ800);

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
  strip.begin();
  strip.setBrightness(kDefaultBrightness);
  strip.show();
}

void loop()
{
#ifdef ENABLE_SOLID_COLOR_TEST
  showSolidColor(255, 0, 0);
  delay(1000);
#endif

#ifdef ENABLE_RAINBOW_ANIMATION
  showRainbowAnimation();
#endif

}
