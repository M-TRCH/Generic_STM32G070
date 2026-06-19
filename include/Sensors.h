#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

// พอร์ต RS485 สำหรับ PZEM-017 (RX=PB7, TX=PA9)
extern HardwareSerial Serial1;

// ---------------------------------------------------------------------------
// โครงสร้างข้อมูลของเซ็นเซอร์
// ---------------------------------------------------------------------------
struct Pzem017Reading
{
  uint16_t rawVoltage = 0;
  uint16_t rawCurrent = 0;
  uint32_t rawPower = 0;
  uint32_t rawEnergy = 0;
  uint16_t rawHighVoltageAlarm = 0;
  uint16_t rawLowVoltageAlarm = 0;
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  uint16_t highVoltageAlarm = 0;
  uint16_t lowVoltageAlarm = 0;
};

// ใช้ร่วมกันทั้ง SHT40 และ SHT31 (ค่าเหมือนกัน)
struct Sht40Reading
{
  float temperatureC = NAN;
  float humidityPercent = NAN;
};

struct Ina180Reading
{
  uint16_t rawAdc = 0;
  float outputVoltage = NAN;
  float sensedVoltage = NAN;
  float shuntVoltage = NAN;
  float currentAmps = NAN;
};

// ค่า zero-offset ของ INA180 (คาลิเบรตตอนบูต) เปิดให้เข้าถึงเพื่อ debug
extern float ina180ZeroOffsetVoltage;

// ---- SHT40 (บัส Wire1) / SHT31 (บัส Wire) ----
bool readSht40(Sht40Reading &reading);
bool readSht31(Sht40Reading &reading);
void printSht40Reading(const Sht40Reading &reading);
void printSht31Reading(const Sht40Reading &reading);

// ---- INA180 (analog current sensor) ----
void initIna180();
void calibrateIna180ZeroOffset();
bool readIna180(Ina180Reading &reading);
void printIna180Reading(const Ina180Reading &reading);

// ---- PZEM-017 / PZEM-003 (RS485 / Modbus-RTU, โปรโตคอลเดียวกัน) ----
enum class PzemReadStatus : uint8_t
{
  Ok = 0,
  InvalidAddress,
  Timeout,
  CrcMismatch,
  FrameMismatch,
};

const __FlashStringHelper *pzemReadStatusText(PzemReadStatus status);

void initPzem017();
bool readPzem017AtAddress(uint8_t address, Pzem017Reading &reading, PzemReadStatus *status = nullptr);
bool readPzem017(Pzem017Reading &reading);
void printPzem017Readings(const Pzem017Reading &chargeReading, const Pzem017Reading &dischargeReading,
                          float socPercent, float remainingCapacityAh);
void printPzem017ReadError();

// แก้ไข/อ่าน ID (Modbus slave address) ของ PZEM-003/017
// ใช้ general address 0xF8 จึงทำงานได้แม้ไม่ทราบ ID เดิม (ต้องมีอุปกรณ์ตัวเดียวบนบัส)
bool setPzem017Address(uint8_t newAddress);
bool readPzem017Address(uint8_t &outAddress);
