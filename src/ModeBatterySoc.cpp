#include "ModeBatterySoc.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Hardware.h"
#include "Sensors.h"
#include "SocEstimator.h"
#include "OledDisplay.h"

void batterySocSetup()
{
  initBoard();

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
}

void batterySocLoop()
{
  static uint32_t lastReadMs = 0;

  if (millis() - lastReadMs >= 1000U) {
    lastReadMs = millis();

    Pzem017Reading chargeReading;
    Pzem017Reading dischargeReading;

    const bool chargeOk = readPzem017AtAddress(kPzemChargeAddress, chargeReading);
    const bool dischargeOk = readPzem017AtAddress(kPzemDischargeAddress, dischargeReading);

    if (chargeOk) {
      float remainingAh = NAN;
      printPzem017Reading(chargeReading, NAN, remainingAh);
    }
    if (dischargeOk) {
      float remainingAh = NAN;
      printPzem017Reading(dischargeReading, NAN, remainingAh);
    }

    if (chargeOk || dischargeOk) {
      const float netVoltage = chargeOk ? chargeReading.voltage : dischargeReading.voltage;
      const float netCurrent = (dischargeOk ? dischargeReading.current : 0.0f)
                               - (chargeOk ? chargeReading.current : 0.0f);
      const float socPercent = updateSocState(netVoltage, netCurrent);
      const float remainingCapacityAh = estimateRemainingCapacityAh(socPercent);
      updateOledDisplay(chargeReading, dischargeReading, socPercent, remainingCapacityAh);
    } else {
      printPzem017ReadError();
      showOledMessage(F("PZEM-003 x2"), F("Read failed"));
    }
  }
}
