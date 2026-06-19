#pragma once

#include <Arduino.h>

extern RTC_HandleTypeDef rtcHandle;
extern uint32_t rtcBootCount;

// เริ่มต้น RTC ภายใน (clock จาก LSI ~32 kHz) ตั้งเวลา default ครั้งแรก
bool initRtc();
bool readRtc(RTC_TimeTypeDef &time, RTC_DateTypeDef &date);
bool setRtcDateTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hours, uint8_t minutes, uint8_t seconds);
uint32_t incrementRtcBootCount();
void printRtcReading(const RTC_TimeTypeDef &time, const RTC_DateTypeDef &date);

// แปลงเวลา RTC ปัจจุบันเป็นจำนวนวินาทีสะสม (epoch ภายใน); คืน 0 เมื่อ RTC ยังอ่านไม่ได้
uint64_t rtcEpochSeconds();
