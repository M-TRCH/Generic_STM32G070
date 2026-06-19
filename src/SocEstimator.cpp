#include "SocEstimator.h"

namespace {

// ---- พารามิเตอร์แบตเตอรี่ LiFePO4 8S ----
constexpr float kSocBatteryCapacityAh = 18.0f;
constexpr float kSocChargeMaxVoltage = 29.2f;
constexpr float kSocEmptyVoltage = 20.5f;
// -1.0 = PZEM วัดกระแส discharge (SoC ลด), +1.0 = charge (SoC เพิ่ม)
constexpr float kSocCurrentSign = -1.0f;
// กระแสต่ำกว่าค่านี้ (A) = แบตพัก → ใช้ OCV table ปรับ SoC
constexpr float kSocRestingThresholdA = 0.10f;
// ชาร์จเต็มเมื่อแรงดัน >= kSocChargeMaxVoltage และกระแส < ค่านี้ (A)
constexpr float kSocChargeCompleteA = 0.20f;

// OCV table สำหรับ LiFePO4 8S (แรงดันพักเทียบกับ SoC)
struct OcvPoint
{
  float voltage;
  float soc;
};

const OcvPoint kOcvTable[] = {
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
  { 20.50f,   0.0f },  // 2.563 V/cell - cutoff ว่าง
};

constexpr uint8_t kOcvTableSize = sizeof(kOcvTable) / sizeof(kOcvTable[0]);

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

} // namespace

float updateSocState(float voltageV, float currentA);

float updateSocState(const Pzem017Reading &reading)
{
  return updateSocState(reading.voltage, reading.current);
}

float updateSocState(float voltageV, float currentA)
{
  static float socPercent = NAN;
  static uint32_t lastMs = 0;

  const uint32_t nowMs = millis();

  if (!isfinite(voltageV) || !isfinite(currentA)) {
    return socPercent;
  }

  // Anchor: ชาร์จเต็ม → reset เป็น 100%
  if (voltageV >= kSocChargeMaxVoltage && fabsf(currentA) < kSocChargeCompleteA) {
    socPercent = 100.0f;
    lastMs = nowMs;
    return socPercent;
  }

  // Anchor: หมด → reset เป็น 0%
  if (voltageV <= kSocEmptyVoltage) {
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
  if (fabsf(currentA) < kSocRestingThresholdA) {
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
    const float deltaSoc = (currentA * dtHours / kSocBatteryCapacityAh) * 100.0f;
    socPercent += kSocCurrentSign * deltaSoc;
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
  return (kSocBatteryCapacityAh * socPercent) / 100.0f;
}
