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

  // จอ OLED บนบัส Wire
  Wire.setSDA(kWireSdaPin);
  Wire.setSCL(kWireSclPin);
  Wire.begin();
  initOledDisplay();
  showOledMessage(F("PZEM-017 OLED"), F("Display ready"));
}

void batterySocLoop()
{
  static uint32_t lastReadMs = 0;

  if (millis() - lastReadMs >= 1000U) {
    lastReadMs = millis();

    Pzem017Reading reading;
    if (readPzem017(reading)) {
      float socPercent = updateSocState(reading);
      float remainingCapacityAh = estimateRemainingCapacityAh(socPercent);
      printPzem017Reading(reading, socPercent, remainingCapacityAh);
      updateOledDisplay(reading, socPercent, remainingCapacityAh);
    } else {
      printPzem017ReadError();
      showOledMessage(F("PZEM-017"), F("Read failed"));
    }
  }
}
