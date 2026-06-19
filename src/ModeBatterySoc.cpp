#include "ModeBatterySoc.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Hardware.h"
#include "Sensors.h"
#include "SocEstimator.h"
#include "OledDisplay.h"
#include "Storage.h"

void batterySocSetup()
{
  initBoard();
  Wire1.begin();

  // PZEM-017 บนพอร์ต RS485
  initPzem017();

#if PZEM_SET_ADDRESS_ON_BOOT
  Serial.print(F("PZEM set address -> 0x"));
  Serial.println(kPzemTargetAddress, HEX);
  if (setPzem017Address(kPzemTargetAddress)) {
    Serial.println(F("PZEM set address OK"));
  } else {
    Serial.println(F("PZEM set address FAILED"));
  }

  uint8_t currentAddress = 0;
  if (readPzem017Address(currentAddress)) {
    Serial.print(F("PZEM current address = 0x"));
    Serial.println(currentAddress, HEX);
  } else {
    Serial.println(F("PZEM read address FAILED"));
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
    Serial.print(F("SoC loaded from EEPROM: "));
    Serial.print(savedSocPercent, 2);
    Serial.println(F(" %"));
  } else {
    initializeSocEstimator();
    Serial.println(F("SoC EEPROM empty/invalid, estimator starts from live data"));
  }
}

void batterySocLoop()
{
  static uint32_t lastReadMs = 0;
  static bool readChargeNext = true;
  static Pzem017Reading chargeReading;
  static Pzem017Reading dischargeReading;
  static bool chargeOk = false;
  static bool dischargeOk = false;
  static PzemReadStatus chargeStatus = PzemReadStatus::Timeout;
  static PzemReadStatus dischargeStatus = PzemReadStatus::Timeout;
  static uint32_t lastSocSaveMs = 0;
  static float lastSavedSocPercent = NAN;

  if (millis() - lastReadMs >= 500U) {
    lastReadMs = millis();

    if (readChargeNext) {
      chargeOk = readPzem017AtAddress(kPzemChargeAddress, chargeReading, &chargeStatus);
      if (!chargeOk) {
        Serial.print(F("PZEM charge read failed addr=0x"));
        Serial.print(kPzemChargeAddress, HEX);
        Serial.print(F(" reason="));
        Serial.println(pzemReadStatusText(chargeStatus));
      }
    } else {
      dischargeOk = readPzem017AtAddress(kPzemDischargeAddress, dischargeReading, &dischargeStatus);
      if (!dischargeOk) {
        Serial.print(F("PZEM discharge read failed addr=0x"));
        Serial.print(kPzemDischargeAddress, HEX);
        Serial.print(F(" reason="));
        Serial.println(pzemReadStatusText(dischargeStatus));
      }

      if (chargeOk || dischargeOk) {
        const float socPercent = updateSocState(chargeReading, chargeOk, dischargeReading, dischargeOk);
        const float remainingCapacityAh = estimateRemainingCapacityAh(socPercent);
        Serial.print(F("SoC boot="));
        Serial.print(socSourceText(getSocBootSource()));
        Serial.print(F(" live="));
        Serial.print(socSourceText(getSocSource()));
        Serial.print(F(" Vflt="));
        Serial.print(getSocFilteredVoltage(), 3);
        Serial.println(F("V"));
        printPzem017Readings(chargeReading, dischargeReading, socPercent, remainingCapacityAh);
        updateOledDisplay(chargeReading, dischargeReading, socPercent, remainingCapacityAh);

        const uint32_t nowMs = millis();
        const uint32_t saveIntervalMs = kSocSaveIntervalMinutes * kSecondsPerMinute * 1000u;
        const bool intervalReached = (lastSocSaveMs == 0u) || ((nowMs - lastSocSaveMs) >= saveIntervalMs);
        const bool deltaReached = !isfinite(lastSavedSocPercent)
                                  || (fabsf(socPercent - lastSavedSocPercent) >= kSocSaveDeltaPercent);
        const bool anchoredState = isfinite(socPercent) && (socPercent <= 0.05f || socPercent >= 99.95f);

        if (isfinite(socPercent) && deltaReached && (anchoredState || intervalReached)) {
          if (saveSavedSocPercent(socPercent)) {
            lastSocSaveMs = nowMs;
            lastSavedSocPercent = socPercent;
            Serial.print(F("SoC saved to EEPROM: "));
            Serial.print(socPercent, 2);
            Serial.println(F(" %"));
          } else {
            Serial.println(F("SoC EEPROM write failed"));
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
