#pragma once

#include <Arduino.h>

// ค่าคงที่หน่วยเวลาเป็นวินาที (ใช้ร่วมกับ TFT dashboard ในการ format runtime)
constexpr uint32_t kSecondsPerMinute = 60u;
constexpr uint32_t kSecondsPerHour = 60u * kSecondsPerMinute;
constexpr uint32_t kSecondsPerDay = 24u * kSecondsPerHour;

// เวลาใช้งานสะสม (วินาที) โหลดจาก EEPROM ตอนบูต และค่าที่บันทึกสำเร็จล่าสุด
extern uint32_t deviceRuntimeSeconds;
extern uint32_t lastRuntimeSeconds;

// ---- AT24C32D EEPROM (บัส Wire1) ----
bool eepromWriteBytes(uint16_t memAddr, const uint8_t *data, size_t length);
bool eepromReadBytes(uint16_t memAddr, uint8_t *data, size_t length);

// ---- runtime counter ----
bool loadRuntimeSeconds(uint32_t &outSeconds);
bool saveRuntimeSeconds(uint32_t seconds);
void initRuntimeCounter();    // โหลดค่าจาก EEPROM + log ช่วงเวลาบันทึก
void serviceRuntimeCounter(); // นับเวลาจาก RTC + เขียนลง EEPROM ตามรอบ
