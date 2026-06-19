#pragma once

#include <Arduino.h>
#include "Sensors.h"

// เริ่มต้นจอ OLED (เลือกไดรเวอร์ตาม OLED_PANEL_TYPE ภายในไฟล์ .cpp)
void initOledDisplay();

// แสดงข้อความ 2 บรรทัดกลางจอ (เช่น สถานะ/ข้อผิดพลาด)
void showOledMessage(const __FlashStringHelper *line1, const __FlashStringHelper *line2);

// แสดงค่าจาก PZEM-017 + SoC (โหมด BATTERY_SOC)
void updateOledDisplay(const Pzem017Reading &reading, float socPercent, float remainingCapacityAh);
