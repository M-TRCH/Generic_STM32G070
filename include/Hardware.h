#pragma once

#include <Arduino.h>
#include <Wire.h>

// บัส I2C ตัวที่สอง (SHT40 + EEPROM AT24C32) : SDA=PB9, SCL=PB6
extern TwoWire Wire1;

// ตั้งค่า GPIO พื้นฐาน (status LED, latch) + เปิด debug serial
void initBoard();
