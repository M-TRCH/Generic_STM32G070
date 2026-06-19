#include "Storage.h"
#include "Hardware.h"
#include "RtcManager.h"

uint32_t deviceRuntimeSeconds = 0;
uint32_t lastRuntimeSeconds = 0;

namespace {

constexpr uint8_t kAt24c32Address = 0x50;
constexpr uint16_t kAt24c32RuntimeAddr = 0x0000;
constexpr uint32_t kRuntimeMarker = 0x52554E54u; // "RUNT"
constexpr uint8_t kRuntimeRecordSize = 12;       // marker(4) + seconds(4) + ~seconds(4)
constexpr uint16_t kAt24c32SocAddr = 0x0010;
constexpr uint32_t kSocMarker = 0x534F4321u;     // "SOC!"
constexpr uint8_t kSocRecordSize = 8;            // marker(4) + soc_x100(2) + ~soc_x100(2)

// runtime ใช้ RTC เป็นฐานเวลา (อิสระจาก SysTick) เพราะ NeoPixel.show() ปิด
// interrupt นานต่อเฟรมทำให้ millis() เดินช้า → runtime คลาดเคลื่อน
constexpr bool kRuntimeUseTestSavePeriod = false;
constexpr uint32_t kRuntimeSaveIntervalTestSec = 10u;
constexpr uint32_t kRuntimeSaveIntervalNormalDays = 0u;
constexpr uint32_t kRuntimeSaveIntervalNormalHours = 0u;
constexpr uint32_t kRuntimeSaveIntervalNormalMinutes = 10u;
constexpr uint32_t kRuntimeSaveIntervalNormalSec =
  (kRuntimeSaveIntervalNormalDays * kSecondsPerDay)
  + (kRuntimeSaveIntervalNormalHours * kSecondsPerHour)
  + (kRuntimeSaveIntervalNormalMinutes * kSecondsPerMinute);
constexpr uint32_t kRuntimeSaveIntervalSec =
  kRuntimeUseTestSavePeriod ? kRuntimeSaveIntervalTestSec : kRuntimeSaveIntervalNormalSec;

uint32_t unpackUint32Le(const uint8_t *p)
{
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void packUint32Le(uint8_t *p, uint32_t value)
{
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint16_t unpackUint16Le(const uint8_t *p)
{
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

void packUint16Le(uint8_t *p, uint16_t value)
{
  p[0] = static_cast<uint8_t>(value & 0xFF);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

} // namespace

bool eepromWriteBytes(uint16_t memAddr, const uint8_t *data, size_t length)
{
  Wire1.beginTransmission(kAt24c32Address);
  Wire1.write(static_cast<uint8_t>(memAddr >> 8));
  Wire1.write(static_cast<uint8_t>(memAddr & 0xFF));
  for (size_t i = 0; i < length; ++i) {
    Wire1.write(data[i]);
  }
  if (Wire1.endTransmission() != 0) {
    return false;
  }

  delay(6); // write cycle time ~5ms
  return true;
}

bool eepromReadBytes(uint16_t memAddr, uint8_t *data, size_t length)
{
  Wire1.beginTransmission(kAt24c32Address);
  Wire1.write(static_cast<uint8_t>(memAddr >> 8));
  Wire1.write(static_cast<uint8_t>(memAddr & 0xFF));
  if (Wire1.endTransmission(false) != 0) { // repeated start
    return false;
  }

  if (Wire1.requestFrom(static_cast<int>(kAt24c32Address), static_cast<int>(length)) != static_cast<int>(length)) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    if (!Wire1.available()) {
      return false;
    }
    data[i] = static_cast<uint8_t>(Wire1.read());
  }

  return true;
}

// อ่าน runtime สะสมจาก EEPROM; ตรวจ marker + complement ของค่า เพื่อกันข้อมูลเสีย
bool loadRuntimeSeconds(uint32_t &outSeconds)
{
  uint8_t buffer[kRuntimeRecordSize] = {};
  if (!eepromReadBytes(kAt24c32RuntimeAddr, buffer, sizeof(buffer))) {
    return false;
  }

  uint32_t marker = unpackUint32Le(buffer);
  uint32_t seconds = unpackUint32Le(buffer + 4);
  uint32_t check = unpackUint32Le(buffer + 8);

  if (marker != kRuntimeMarker || check != ~seconds) {
    return false;
  }

  outSeconds = seconds;
  return true;
}

bool saveRuntimeSeconds(uint32_t seconds)
{
  uint8_t buffer[kRuntimeRecordSize] = {};
  packUint32Le(buffer, kRuntimeMarker);
  packUint32Le(buffer + 4, seconds);
  packUint32Le(buffer + 8, ~seconds);
  return eepromWriteBytes(kAt24c32RuntimeAddr, buffer, sizeof(buffer));
}

void initRuntimeCounter()
{
  if (loadRuntimeSeconds(deviceRuntimeSeconds)) {
    lastRuntimeSeconds = deviceRuntimeSeconds;
    Serial.print(F("Runtime loaded from EEPROM: "));
    Serial.print(deviceRuntimeSeconds);
    Serial.println(F(" s"));
  } else {
    deviceRuntimeSeconds = 0;
    lastRuntimeSeconds = 0;
    Serial.println(F("Runtime EEPROM empty/invalid, starting at 0"));
    if (saveRuntimeSeconds(deviceRuntimeSeconds)) {
      lastRuntimeSeconds = deviceRuntimeSeconds;
    }
  }

  Serial.print(F("Runtime save period = "));
  Serial.print(kRuntimeSaveIntervalSec);
  Serial.print(F(" s ("));
  Serial.print(kRuntimeSaveIntervalSec / kSecondsPerDay);
  Serial.print(F("d "));
  Serial.print((kRuntimeSaveIntervalSec % kSecondsPerDay) / kSecondsPerHour);
  Serial.print(F("h "));
  Serial.print((kRuntimeSaveIntervalSec % kSecondsPerHour) / kSecondsPerMinute);
  Serial.println(F("m)"));
}

void serviceRuntimeCounter()
{
  static bool initialized = false;
  static uint64_t lastEpoch = 0;
  static uint32_t lastSavedSeconds = 0;

  const uint64_t nowEpoch = rtcEpochSeconds();
  if (nowEpoch == 0) {
    return; // RTC ยังไม่พร้อม ข้ามรอบนี้ไปก่อน
  }

  if (!initialized) {
    lastEpoch = nowEpoch;
    lastSavedSeconds = lastRuntimeSeconds;
    initialized = true;
    return;
  }

  if (nowEpoch > lastEpoch) {
    const uint64_t delta = nowEpoch - lastEpoch;
    // กันค่ากระโดดผิดปกติ (เช่น RTC ถูกตั้งเวลาใหม่) ไม่ให้บวกทีละมาก ๆ
    if (delta < 86400ull * 3650ull) {
      deviceRuntimeSeconds += static_cast<uint32_t>(delta);
    }
    lastEpoch = nowEpoch;
  } else if (nowEpoch < lastEpoch) {
    lastEpoch = nowEpoch; // RTC เดินถอยหลัง (ถูกตั้งใหม่) → sync เฉย ๆ ไม่นับ
  }

  if (deviceRuntimeSeconds - lastSavedSeconds >= kRuntimeSaveIntervalSec) {
    if (saveRuntimeSeconds(deviceRuntimeSeconds)) {
      lastSavedSeconds = deviceRuntimeSeconds;
      lastRuntimeSeconds = deviceRuntimeSeconds;
      Serial.print(F("Runtime saved to EEPROM: "));
      Serial.print(deviceRuntimeSeconds);
      Serial.println(F(" s"));
    } else {
      Serial.println(F("Runtime EEPROM write failed"));
    }
  }
}

bool loadSavedSocPercent(float &outSocPercent)
{
  uint8_t buffer[kSocRecordSize] = {};
  if (!eepromReadBytes(kAt24c32SocAddr, buffer, sizeof(buffer))) {
    return false;
  }

  const uint32_t marker = unpackUint32Le(buffer);
  const uint16_t socX100 = unpackUint16Le(buffer + 4);
  const uint16_t check = unpackUint16Le(buffer + 6);

  if (marker != kSocMarker || check != static_cast<uint16_t>(~socX100)) {
    return false;
  }

  outSocPercent = static_cast<float>(socX100) / 100.0f;
  return true;
}

bool saveSavedSocPercent(float socPercent)
{
  if (!isfinite(socPercent)) {
    return false;
  }

  const float clampedSocPercent = constrain(socPercent, 0.0f, 100.0f);
  const uint16_t socX100 = static_cast<uint16_t>(lroundf(clampedSocPercent * 100.0f));

  uint8_t buffer[kSocRecordSize] = {};
  packUint32Le(buffer, kSocMarker);
  packUint16Le(buffer + 4, socX100);
  packUint16Le(buffer + 6, static_cast<uint16_t>(~socX100));
  return eepromWriteBytes(kAt24c32SocAddr, buffer, sizeof(buffer));
}
