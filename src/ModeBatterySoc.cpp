#include "ModeBatterySoc.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Hardware.h"
#include "Sensors.h"
#include "SocEstimator.h"
#include "OledDisplay.h"
#include "Storage.h"

namespace {

constexpr uint8_t kSensorStaleThreshold = 2;
constexpr uint8_t kSensorFaultThreshold = 3;
constexpr uint8_t kSensorRecoverThreshold = 2;
constexpr float kBatteryCapacityAh = 18.0f;
constexpr size_t kTimeEstimateAverageWindow = 900;

struct MovingAverageFilter {
  float samples[kTimeEstimateAverageWindow] = {};
  size_t nextIndex = 0;
  size_t count = 0;
  float sum = 0.0f;

  void reset()
  {
    nextIndex = 0;
    count = 0;
    sum = 0.0f;
  }

  float update(float sample)
  {
    if (!isfinite(sample)) {
      reset();
      return NAN;
    }

    if (count < kTimeEstimateAverageWindow) {
      samples[nextIndex] = sample;
      sum += sample;
      ++count;
    } else {
      sum -= samples[nextIndex];
      samples[nextIndex] = sample;
      sum += sample;
    }

    nextIndex = (nextIndex + 1u) % kTimeEstimateAverageWindow;
    return sum / static_cast<float>(count);
  }
};

void printBatteryJsonEvent(const __FlashStringHelper *eventName,
                           const __FlashStringHelper *statusText,
                           const __FlashStringHelper *messageText = nullptr)
{
#if ENABLE_PYTHON_JSON_OUTPUT
  Serial.print(F("{\"type\":\"battery_soc_event\",\"millis\":"));
  Serial.print(millis());
  Serial.print(F(",\"event\":\""));
  Serial.print(eventName);
  Serial.print(F("\",\"status\":\""));
  Serial.print(statusText);
  Serial.print(F("\""));
  if (messageText != nullptr) {
    Serial.print(F(",\"message\":\""));
    Serial.print(messageText);
    Serial.print(F("\""));
  }
  Serial.println(F("}"));
#else
  (void)eventName;
  (void)statusText;
  (void)messageText;
#endif
}

void printBatteryJsonAddressEvent(const __FlashStringHelper *eventName,
                                  const __FlashStringHelper *statusText,
                                  uint8_t address)
{
#if ENABLE_PYTHON_JSON_OUTPUT
  Serial.print(F("{\"type\":\"battery_soc_event\",\"millis\":"));
  Serial.print(millis());
  Serial.print(F(",\"event\":\""));
  Serial.print(eventName);
  Serial.print(F("\",\"status\":\""));
  Serial.print(statusText);
  Serial.print(F("\",\"address\":"));
  Serial.print(address);
  Serial.println(F("}"));
#else
  (void)eventName;
  (void)statusText;
  (void)address;
#endif
}

void printBatteryJsonSocEvent(const __FlashStringHelper *eventName,
                              const __FlashStringHelper *statusText,
                              float socPercent)
{
#if ENABLE_PYTHON_JSON_OUTPUT
  Serial.print(F("{\"type\":\"battery_soc_event\",\"millis\":"));
  Serial.print(millis());
  Serial.print(F(",\"event\":\""));
  Serial.print(eventName);
  Serial.print(F("\",\"status\":\""));
  Serial.print(statusText);
  Serial.print(F("\",\"soc_percent\":"));
  if (isfinite(socPercent)) {
    Serial.print(socPercent, 2);
  } else {
    Serial.print(F("null"));
  }
  Serial.println(F("}"));
#else
  (void)eventName;
  (void)statusText;
  (void)socPercent;
#endif
}

void printBatteryJsonReadFailure(const __FlashStringHelper *sensorName,
                                 uint8_t address,
                                 PzemReadStatus status)
{
#if ENABLE_PYTHON_JSON_OUTPUT
  Serial.print(F("{\"type\":\"pzem017_read_error\",\"ok\":false,\"millis\":"));
  Serial.print(millis());
  Serial.print(F(",\"sensor\":\""));
  Serial.print(sensorName);
  Serial.print(F("\",\"address\":"));
  Serial.print(address);
  Serial.print(F(",\"reason\":\""));
  Serial.print(pzemReadStatusText(status));
  Serial.println(F("\"}"));
#else
  (void)sensorName;
  (void)address;
  (void)status;
#endif
}

bool isSensorFaultLatched(bool hasValidReading, uint8_t failCount, bool faultLatched)
{
  return !hasValidReading || faultLatched || failCount >= kSensorFaultThreshold;
}

} // namespace

void batterySocSetup()
{
  initBoard();
  Wire1.begin();

  // PZEM-017 บนพอร์ต RS485
  initPzem017();

#if PZEM_SET_ADDRESS_ON_BOOT
#if ENABLE_PYTHON_JSON_OUTPUT
  printBatteryJsonAddressEvent(F("pzem_set_address"), F("request"), kPzemTargetAddress);
#else
  Serial.print(F("PZEM set address -> 0x"));
  Serial.println(kPzemTargetAddress, HEX);
#endif
  if (setPzem017Address(kPzemTargetAddress)) {
#if ENABLE_PYTHON_JSON_OUTPUT
    printBatteryJsonAddressEvent(F("pzem_set_address"), F("ok"), kPzemTargetAddress);
#else
    Serial.println(F("PZEM set address OK"));
#endif
  } else {
#if ENABLE_PYTHON_JSON_OUTPUT
    printBatteryJsonAddressEvent(F("pzem_set_address"), F("failed"), kPzemTargetAddress);
#else
    Serial.println(F("PZEM set address FAILED"));
#endif
  }

  uint8_t currentAddress = 0;
  if (readPzem017Address(currentAddress)) {
#if ENABLE_PYTHON_JSON_OUTPUT
    printBatteryJsonAddressEvent(F("pzem_read_address"), F("ok"), currentAddress);
#else
    Serial.print(F("PZEM current address = 0x"));
    Serial.println(currentAddress, HEX);
#endif
  } else {
#if ENABLE_PYTHON_JSON_OUTPUT
    printBatteryJsonEvent(F("pzem_read_address"), F("failed"));
#else
    Serial.println(F("PZEM read address FAILED"));
#endif
  }
#endif

  // จอ OLED บนบัส Wire
  Wire.setSDA(kWireSdaPin);
  Wire.setSCL(kWireSclPin);
  Wire.begin();
  initOledDisplay();
  showOledMessage(F("PZEM-003 x2"), F("Display ready"));

  float savedSocPercent = NAN;
  if (loadSavedSocPercent(savedSocPercent)) {
    initializeSocEstimator(savedSocPercent);
#if ENABLE_PYTHON_JSON_OUTPUT
    printBatteryJsonSocEvent(F("soc_restore"), F("ok"), savedSocPercent);
#else
    Serial.print(F("SoC loaded from EEPROM: "));
    Serial.print(savedSocPercent, 2);
    Serial.println(F(" %"));
#endif
  } else {
    initializeSocEstimator();
#if ENABLE_PYTHON_JSON_OUTPUT
    printBatteryJsonEvent(F("soc_restore"), F("empty_invalid"), F("estimator_starts_from_live_data"));
#else
    Serial.println(F("SoC EEPROM empty/invalid, estimator starts from live data"));
#endif
  }
}

void batterySocLoop()
{
  static uint32_t lastReadMs = 0;
  static bool readChargeNext = true;
  static Pzem017Reading chargeReading;
  static Pzem017Reading dischargeReading;
  static bool chargeHasValidReading = false;
  static bool dischargeHasValidReading = false;
  static bool chargeOk = false;
  static bool dischargeOk = false;
  static PzemReadStatus chargeStatus = PzemReadStatus::Timeout;
  static PzemReadStatus dischargeStatus = PzemReadStatus::Timeout;
  static uint8_t chargeFailCount = 0;
  static uint8_t dischargeFailCount = 0;
  static uint8_t chargeRecoverCount = 0;
  static uint8_t dischargeRecoverCount = 0;
  static bool chargeFaultLatched = false;
  static bool dischargeFaultLatched = false;
  static uint32_t lastSocSaveMs = 0;
  static float lastSavedSocPercent = NAN;
  static float heldSocPercent = NAN;
  static float heldRemainingCapacityAh = NAN;
  static MovingAverageFilter timeRemainingAverage;
  static MovingAverageFilter timeToFullAverage;

  if (millis() - lastReadMs >= 500U) {
    lastReadMs = millis();

    if (readChargeNext) {
      chargeOk = readPzem017AtAddress(kPzemChargeAddress, chargeReading, &chargeStatus);
      if (chargeOk) {
        chargeHasValidReading = true;
        chargeFailCount = 0;
        if (chargeFaultLatched) {
          if (chargeRecoverCount < 255u) {
            ++chargeRecoverCount;
          }
          if (chargeRecoverCount >= kSensorRecoverThreshold) {
            chargeFaultLatched = false;
          }
        }
      } else {
        chargeRecoverCount = 0;
        if (chargeFailCount < 255u) {
          ++chargeFailCount;
        }
        if (chargeFailCount >= kSensorFaultThreshold) {
          chargeFaultLatched = true;
        }
#if ENABLE_PYTHON_JSON_OUTPUT
        printBatteryJsonReadFailure(F("charge"), kPzemChargeAddress, chargeStatus);
#else
        Serial.print(F("PZEM charge read failed addr=0x"));
        Serial.print(kPzemChargeAddress, HEX);
        Serial.print(F(" reason="));
        Serial.println(pzemReadStatusText(chargeStatus));
#endif
      }
    } else {
      dischargeOk = readPzem017AtAddress(kPzemDischargeAddress, dischargeReading, &dischargeStatus);
      if (dischargeOk) {
        dischargeHasValidReading = true;
        dischargeFailCount = 0;
        if (dischargeFaultLatched) {
          if (dischargeRecoverCount < 255u) {
            ++dischargeRecoverCount;
          }
          if (dischargeRecoverCount >= kSensorRecoverThreshold) {
            dischargeFaultLatched = false;
          }
        }
      } else {
        dischargeRecoverCount = 0;
        if (dischargeFailCount < 255u) {
          ++dischargeFailCount;
        }
        if (dischargeFailCount >= kSensorFaultThreshold) {
          dischargeFaultLatched = true;
        }
#if ENABLE_PYTHON_JSON_OUTPUT
        printBatteryJsonReadFailure(F("discharge"), kPzemDischargeAddress, dischargeStatus);
#else
        Serial.print(F("PZEM discharge read failed addr=0x"));
        Serial.print(kPzemDischargeAddress, HEX);
        Serial.print(F(" reason="));
        Serial.println(pzemReadStatusText(dischargeStatus));
#endif
      }

      const bool chargeAvailable = chargeHasValidReading;
      const bool dischargeAvailable = dischargeHasValidReading;
      const bool chargeStale = chargeAvailable && !chargeOk && !chargeFaultLatched
                               && (chargeFailCount <= kSensorStaleThreshold);
      const bool dischargeStale = dischargeAvailable && !dischargeOk && !dischargeFaultLatched
                                  && (dischargeFailCount <= kSensorStaleThreshold);
      const bool degradedMode = isSensorFaultLatched(chargeHasValidReading, chargeFailCount, chargeFaultLatched)
                                || isSensorFaultLatched(dischargeHasValidReading, dischargeFailCount, dischargeFaultLatched);
      const bool canUpdateSoc = chargeOk && dischargeOk && !degradedMode;

      if (chargeAvailable || dischargeAvailable) {
        float socPercent = heldSocPercent;
        float remainingCapacityAh = heldRemainingCapacityAh;
        float timeRemainingHours = NAN;
        float timeToFullHours = NAN;
        float rawTimeRemainingHours = NAN;
        float rawTimeToFullHours = NAN;
        const __FlashStringHelper *liveSourceText = nullptr;
        const __FlashStringHelper *chargeStateText = nullptr;
        const __FlashStringHelper *dischargeStateText = nullptr;

        if (canUpdateSoc) {
          socPercent = updateSocState(chargeReading, chargeOk, dischargeReading, dischargeOk);
          remainingCapacityAh = estimateRemainingCapacityAh(socPercent);
          heldSocPercent = socPercent;
          heldRemainingCapacityAh = remainingCapacityAh;
        }

        const float chargeCurrentA = chargeOk && isfinite(chargeReading.current) ? chargeReading.current : 0.0f;
        const float dischargeCurrentA = dischargeOk && isfinite(dischargeReading.current) ? dischargeReading.current : 0.0f;
        const float netDischargeCurrentA = dischargeCurrentA - chargeCurrentA;
        const float missingCapacityAh = isfinite(remainingCapacityAh) ? max(0.0f, kBatteryCapacityAh - remainingCapacityAh) : NAN;

        if (canUpdateSoc && isfinite(remainingCapacityAh) && netDischargeCurrentA > 0.05f) {
          rawTimeRemainingHours = remainingCapacityAh / netDischargeCurrentA;
        }

        if (canUpdateSoc && isfinite(missingCapacityAh) && netDischargeCurrentA < -0.05f) {
          rawTimeToFullHours = missingCapacityAh / (-netDischargeCurrentA);
        }

        timeRemainingHours = timeRemainingAverage.update(rawTimeRemainingHours);
        timeToFullHours = timeToFullAverage.update(rawTimeToFullHours);

        if (degradedMode) {
          liveSourceText = F("sensor_degraded");
        } else if (chargeStale || dischargeStale || !canUpdateSoc) {
          liveSourceText = F("sensor_stale_hold");
        } else {
          liveSourceText = socSourceText(getSocSource());
        }

        chargeStateText = !chargeAvailable ? F("fail") : (chargeStale ? F("stale") : F("ok"));
        dischargeStateText = !dischargeAvailable ? F("fail") : (dischargeStale ? F("stale") : F("ok"));

      #if !ENABLE_PYTHON_JSON_OUTPUT
        Serial.print(F("\nSoC boot="));
        Serial.print(socSourceText(getSocBootSource()));
        Serial.print(F(" live="));
        Serial.print(liveSourceText);
        Serial.print(F(" Vflt="));
        Serial.print(getSocFilteredVoltage(), 3);
        Serial.print(F("V CH="));
        Serial.print(chargeStateText);
        Serial.print(F(" DS="));
        Serial.println(dischargeStateText);
      #endif
        printPzem017Readings(chargeReading, dischargeReading, socPercent, remainingCapacityAh,
                             timeRemainingHours, timeToFullHours,
                             socSourceText(getSocBootSource()), liveSourceText,
                             getSocFilteredVoltage(),
                             chargeStateText, dischargeStateText,
                             degradedMode);
        updateOledDisplay(chargeReading, dischargeReading, socPercent, remainingCapacityAh,
                          timeRemainingHours, timeToFullHours,
                          chargeAvailable, dischargeAvailable,
                          chargeStale, dischargeStale,
                          degradedMode);

        const uint32_t nowMs = millis();
        const uint32_t saveIntervalMs = kSocSaveIntervalMinutes * kSecondsPerMinute * 1000u;
        const bool intervalReached = (lastSocSaveMs == 0u) || ((nowMs - lastSocSaveMs) >= saveIntervalMs);
        const bool deltaReached = !isfinite(lastSavedSocPercent)
                                  || (fabsf(socPercent - lastSavedSocPercent) >= kSocSaveDeltaPercent);
        const bool anchoredState = isfinite(socPercent) && (socPercent <= 0.05f || socPercent >= 99.95f);

        if (canUpdateSoc && isfinite(socPercent) && deltaReached && (anchoredState || intervalReached)) {
          if (saveSavedSocPercent(socPercent)) {
            lastSocSaveMs = nowMs;
            lastSavedSocPercent = socPercent;
#if ENABLE_PYTHON_JSON_OUTPUT
            printBatteryJsonSocEvent(F("soc_save"), F("ok"), socPercent);
#else
            Serial.print(F("SoC saved to EEPROM: "));
            Serial.print(socPercent, 2);
            Serial.println(F(" %"));
#endif
          } else {
#if ENABLE_PYTHON_JSON_OUTPUT
            printBatteryJsonSocEvent(F("soc_save"), F("write_failed"), socPercent);
#else
            Serial.println(F("SoC EEPROM write failed"));
#endif
          }
        }
      } else {
        printPzem017ReadError();
        showOledMessage(F("PZEM-003 x2"), F("Read failed"));
      }
    }

    readChargeNext = !readChargeNext;
  }
}
