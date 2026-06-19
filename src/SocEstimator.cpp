#include "SocEstimator.h"

namespace {

// ---- พารามิเตอร์แบตเตอรี่ LiFePO4 8S ----
constexpr float kSocBatteryCapacityAh = 18.0f;
constexpr float kSocChargeMaxVoltage = 28.8f;
constexpr float kSocFullAnchorVoltage = 28.6f;
constexpr float kSocEmptyVoltage = 20.5f;
constexpr float kSocRestingThresholdA = 0.10f;        // แบตพัก
constexpr float kSocChargeCompleteA = 0.20f;          // tail current near full
constexpr float kSocChargeDetectA = 0.30f;            // เริ่มถือว่ากำลังชาร์จจริง
constexpr float kSocChargeCoulombicEfficiency = 0.995f;
constexpr float kSocDischargeCoulombicEfficiency = 1.0f;
constexpr float kSocVoltageFilterAlpha = 0.20f;
constexpr uint32_t kSocRestConfirmMs = 120000u;       // พัก 2 นาทีจึงเริ่มใช้ OCV ปรับ
constexpr uint32_t kSocFullConfirmMs = 90000u;        // จับ full tail 90 วินาที
constexpr uint32_t kSocEmptyConfirmMs = 60000u;       // จับ empty rest 60 วินาที

// OCV table สำหรับ LiFePO4 8S (แรงดันพักเทียบกับ SoC)
struct OcvPoint
{
  float voltage;
  float soc;
};

const OcvPoint kOcvTable[] = {
  { 28.80f, 100.0f },  // 3.600 V/cell - cutoff ชาร์จ
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

struct SocEstimatorState
{
  float socPercent = NAN;
  float filteredVoltageV = NAN;
  SocSource bootSource = SocSource::Unknown;
  SocSource source = SocSource::Unknown;
  uint32_t lastUpdateMs = 0;
  uint32_t restStartMs = 0;
  uint32_t fullStartMs = 0;
  uint32_t emptyStartMs = 0;
};

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

float lowPassValue(float previousValue, float nextValue, float alpha)
{
  if (!isfinite(previousValue)) {
    return nextValue;
  }

  return previousValue + ((nextValue - previousValue) * alpha);
}

float clampSoc(float socPercent)
{
  return constrain(socPercent, 0.0f, 100.0f);
}

float blendToward(float currentValue, float targetValue, float blend)
{
  return currentValue + ((targetValue - currentValue) * blend);
}

SocEstimatorState state;

} // namespace

float getSocFilteredVoltage()
{
  return state.filteredVoltageV;
}

SocSource getSocBootSource()
{
  return state.bootSource;
}

SocSource getSocSource()
{
  return state.source;
}

const __FlashStringHelper *socSourceText(SocSource source)
{
  switch (source) {
    case SocSource::Unknown:
      return F("unknown");
    case SocSource::EepromRestore:
      return F("eeprom_restore");
    case SocSource::OcvInit:
      return F("ocv_init");
    case SocSource::CoulombCount:
      return F("coulomb_count");
    case SocSource::RestOcvCorrection:
      return F("rest_ocv_correction");
    case SocSource::FullAnchor:
      return F("full_anchor");
    case SocSource::EmptyAnchor:
      return F("empty_anchor");
  }

  return F("unknown");
}

void initializeSocEstimator()
{
  state = {};
}

void initializeSocEstimator(float savedSocPercent)
{
  state = {};
  if (isfinite(savedSocPercent)) {
    state.socPercent = clampSoc(savedSocPercent);
    state.bootSource = SocSource::EepromRestore;
    state.source = SocSource::EepromRestore;
  }
}

float updateSocState(float voltageV, float currentA);

float updateSocState(const Pzem017Reading &reading)
{
  return updateSocState(reading.voltage, reading.current);
}

float updateSocState(const Pzem017Reading &chargeReading, bool chargeValid,
                     const Pzem017Reading &dischargeReading, bool dischargeValid)
{
  const float chargeCurrentA = (chargeValid && isfinite(chargeReading.current))
                                 ? max(0.0f, chargeReading.current)
                                 : 0.0f;
  const float dischargeCurrentA = (dischargeValid && isfinite(dischargeReading.current))
                                    ? max(0.0f, dischargeReading.current)
                                    : 0.0f;

  float voltageSum = 0.0f;
  uint8_t voltageCount = 0;
  if (chargeValid && isfinite(chargeReading.voltage)) {
    voltageSum += chargeReading.voltage;
    ++voltageCount;
  }
  if (dischargeValid && isfinite(dischargeReading.voltage)) {
    voltageSum += dischargeReading.voltage;
    ++voltageCount;
  }

  const float fusedVoltageV = (voltageCount > 0)
                                ? (voltageSum / static_cast<float>(voltageCount))
                                : NAN;
  const float netDischargeCurrentA = dischargeCurrentA - chargeCurrentA;

  if (!isfinite(fusedVoltageV) && chargeCurrentA <= 0.0f && dischargeCurrentA <= 0.0f) {
    return state.socPercent;
  }

  const uint32_t nowMs = millis();
  if (isfinite(fusedVoltageV)) {
    state.filteredVoltageV = lowPassValue(state.filteredVoltageV, fusedVoltageV, kSocVoltageFilterAlpha);
  }

  if (!isfinite(state.socPercent)) {
    if (!isfinite(state.filteredVoltageV)) {
      return state.socPercent;
    }

    state.socPercent = lookupSocFromOcv(state.filteredVoltageV);
    if (state.bootSource == SocSource::Unknown) {
      state.bootSource = SocSource::OcvInit;
    }
    state.source = SocSource::OcvInit;
    state.lastUpdateMs = nowMs;
    return state.socPercent;
  }

  if (state.lastUpdateMs != 0U) {
    const float dtHours = static_cast<float>(nowMs - state.lastUpdateMs) / 3600000.0f;
    if (dtHours > 0.0f) {
      const float deltaAh = (chargeCurrentA * kSocChargeCoulombicEfficiency * dtHours)
                            - ((dischargeCurrentA / kSocDischargeCoulombicEfficiency) * dtHours);
      state.socPercent = clampSoc(state.socPercent + ((deltaAh / kSocBatteryCapacityAh) * 100.0f));
      state.source = SocSource::CoulombCount;
    }
  }
  state.lastUpdateMs = nowMs;

  const bool resting = (chargeCurrentA < kSocRestingThresholdA) && (dischargeCurrentA < kSocRestingThresholdA);
  if (resting) {
    if (state.restStartMs == 0U) {
      state.restStartMs = nowMs;
    }
  } else {
    state.restStartMs = 0U;
  }

  const bool nearFull = isfinite(state.filteredVoltageV)
                        && state.filteredVoltageV >= kSocFullAnchorVoltage
                        && chargeCurrentA <= kSocChargeCompleteA
                        && dischargeCurrentA < kSocRestingThresholdA;
  if (nearFull) {
    if (state.fullStartMs == 0U) {
      state.fullStartMs = nowMs;
    } else if ((nowMs - state.fullStartMs) >= kSocFullConfirmMs) {
      state.socPercent = 100.0f;
      state.source = SocSource::FullAnchor;
    }
  } else {
    state.fullStartMs = 0U;
  }

  const bool nearEmpty = isfinite(state.filteredVoltageV)
                         && state.filteredVoltageV <= kSocEmptyVoltage
                         && resting;
  if (nearEmpty) {
    if (state.emptyStartMs == 0U) {
      state.emptyStartMs = nowMs;
    } else if ((nowMs - state.emptyStartMs) >= kSocEmptyConfirmMs) {
      state.socPercent = 0.0f;
      state.source = SocSource::EmptyAnchor;
    }
  } else {
    state.emptyStartMs = 0U;
  }

  if (resting && state.restStartMs != 0U && isfinite(state.filteredVoltageV)) {
    const uint32_t restDurationMs = nowMs - state.restStartMs;
    if (restDurationMs >= kSocRestConfirmMs) {
      const float ocvSoc = lookupSocFromOcv(state.filteredVoltageV);
      if (isfinite(ocvSoc)) {
        const float restBlend = constrain(0.05f + (static_cast<float>(restDurationMs - kSocRestConfirmMs) / 900000.0f),
                                          0.05f, 0.30f);
        state.socPercent = clampSoc(blendToward(state.socPercent, ocvSoc, restBlend));
        state.source = SocSource::RestOcvCorrection;
      }
    }
  }

  if (isfinite(state.filteredVoltageV)
      && state.filteredVoltageV >= kSocChargeMaxVoltage
      && chargeCurrentA < kSocChargeDetectA
      && dischargeCurrentA < kSocRestingThresholdA) {
    state.socPercent = max(state.socPercent, 99.0f);
  }

  if (!resting && fabsf(netDischargeCurrentA) < kSocRestingThresholdA) {
    // มีทั้ง charge/discharge พร้อมกันจน net current เกือบศูนย์ หลีกเลี่ยงการ drift จาก OCV
    state.restStartMs = 0U;
  }

  return clampSoc(state.socPercent);
}

float updateSocState(float voltageV, float currentA)
{
  Pzem017Reading dischargeReading = {};
  Pzem017Reading chargeReading = {};

  dischargeReading.voltage = voltageV;
  dischargeReading.current = max(0.0f, currentA);
  chargeReading.voltage = voltageV;
  chargeReading.current = max(0.0f, -currentA);

  return updateSocState(chargeReading, true, dischargeReading, true);
}

float estimateRemainingCapacityAh(float socPercent)
{
  if (!isfinite(socPercent)) {
    return NAN;
  }
  return (kSocBatteryCapacityAh * socPercent) / 100.0f;
}
