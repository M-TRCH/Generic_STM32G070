#include "ModeFridgeMonitor.h"

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Hardware.h"
#include "Sensors.h"
#include "RtcManager.h"
#include "Storage.h"
#include "TftDashboard.h"
#include "LedStrip.h"

void fridgeMonitorSetup()
{
  initBoard();
  initLedStrip();

  // เซ็นเซอร์: SHT40 (Wire1), SHT31 (Wire), INA180 (analog), RTC ภายใน
  Wire1.begin();
  Wire.setSDA(kWireSdaPin);
  Wire.setSCL(kWireSclPin);
  Wire.begin();
  initIna180();

  // โหลดเวลาใช้งานสะสมจาก EEPROM AT24C32D เพื่อนับต่อจากของเดิม
  initRuntimeCounter();

  if (initRtc()) {
    rtcBootCount = incrementRtcBootCount();
    Serial.print(F("RTC boot count: "));
    Serial.println(rtcBootCount);
  } else {
    Serial.println(F("RTC init failed"));
  }

  initTftDashboard();
}

void fridgeMonitorLoop()
{
  // จังหวะเร็ว: กระแส + รีเฟรชจอ | จังหวะช้า: SHT40/SHT31 (มี delay ภายใน)
  constexpr uint32_t kFastIntervalMs = 150U;
  constexpr uint32_t kSlowIntervalMs = 1000U;

  static uint32_t lastFastMs = 0;
  static uint32_t lastSlowMs = 0;
  static Sht40Reading sht;   // SHT40 (Wire1) เก็บค่าล่าสุดไว้แสดงระหว่างรอบช้า
  static Sht40Reading sht31; // SHT31 (Wire) เก็บค่าล่าสุดไว้แสดงระหว่างรอบช้า

  const uint32_t nowMs = millis();

  serviceRuntimeCounter(); // นับเวลาใช้งานสะสม + บันทึก EEPROM ตามรอบ

  if (nowMs - lastSlowMs >= kSlowIntervalMs) {
    lastSlowMs = nowMs;

    if (readSht40(sht)) {
      printSht40Reading(sht);
    } else {
      sht.temperatureC = NAN;
      sht.humidityPercent = NAN;
      Serial.println(F("SHT40 read failed"));
    }

    if (readSht31(sht31)) {
      printSht31Reading(sht31);
    } else {
      sht31.temperatureC = NAN;
      sht31.humidityPercent = NAN;
      Serial.println(F("SHT31 read failed"));
    }
  }

  if (nowMs - lastFastMs >= kFastIntervalMs) {
    lastFastMs = nowMs;

    Ina180Reading ina;
    readIna180(ina);

    updateTftDashboard(sht, sht31, ina, deviceRuntimeSeconds, lastRuntimeSeconds);
  }

  showRainbowAnimation();
}
