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
  static bool readChargeNext = true;
  static Pzem017Reading chargeReading;
  static Pzem017Reading dischargeReading;
  static bool chargeOk = false;
  static bool dischargeOk = false;
  static PzemReadStatus chargeStatus = PzemReadStatus::Timeout;
  static PzemReadStatus dischargeStatus = PzemReadStatus::Timeout;

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
        const float netVoltage = chargeOk ? chargeReading.voltage : dischargeReading.voltage;
        const float netCurrent = (dischargeOk ? dischargeReading.current : 0.0f)
                                 - (chargeOk ? chargeReading.current : 0.0f);
        const float socPercent = updateSocState(netVoltage, netCurrent);
        const float remainingCapacityAh = estimateRemainingCapacityAh(socPercent);
        printPzem017Readings(chargeReading, dischargeReading, socPercent, remainingCapacityAh);
        updateOledDisplay(chargeReading, dischargeReading, socPercent, remainingCapacityAh);
      } else {
        printPzem017ReadError();
        showOledMessage(F("PZEM-003 x2"), F("Read failed"));
      }
    }

    readChargeNext = !readChargeNext;
  }
}
