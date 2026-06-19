#pragma once

#include <Arduino.h>

// CRC16 (Modbus-RTU) สำหรับ PZEM-017
uint16_t crc16Modbus(const uint8_t *data, size_t length);

// CRC8 (Sensirion 0x31) สำหรับเซ็นเซอร์ตระกูล SHT
uint8_t crc8Sensirion(const uint8_t *data, size_t length);
