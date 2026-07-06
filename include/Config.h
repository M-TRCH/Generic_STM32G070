#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// เลือกโหมดการทำงาน (ครั้งละ 1 โหมดเท่านั้น ไม่สามารถเปิดพร้อมกันได้)
//   MODE_FRIDGE_MONITOR : แสดง dashboard ตู้เย็นบนจอ TFT 3.5"
//                         (SHT40 + SHT31 + INA180 + runtime สะสม)
//   MODE_BATTERY_SOC    : คำนวณ SoC แบตเตอรี่ (PZEM-017) แสดงบนจอ OLED
//   MODE_HEAD_NODE      : หัวโหนดสำหรับงาน Node + Gateway
//   MODE_TCP_SOCKET_SERVER : ทดสอบส่งข้อมูลแบตเตอรี่ผ่าน Raw TCP server (W5500)
//
// วิธีเพิ่มโหมดในอนาคต: เพิ่ม #define MODE_xxx ค่าใหม่ + ไฟล์ Mode<xxx>.{h,cpp}
// แล้ว dispatch เพิ่มใน main.cpp
// ---------------------------------------------------------------------------
#define MODE_FRIDGE_MONITOR 1
#define MODE_BATTERY_SOC    2
#define MODE_HEAD_NODE      3
#define MODE_TCP_SOCKET_SERVER 4

#ifndef OPERATING_MODE
#define OPERATING_MODE MODE_TCP_SOCKET_SERVER
#endif

// เปิด/ปิดรูปแบบ output แบบ JSON สำหรับ python logger (ใช้ในโหมด BATTERY_SOC)
#ifndef ENABLE_PYTHON_JSON_OUTPUT
#define ENABLE_PYTHON_JSON_OUTPUT 1
#endif

// ตั้ง 1 เพื่อแก้ ID (Modbus address) ของ PZEM-003/017 เป็น kPzemTargetAddress
// ครั้งเดียวตอนบูต (เฉพาะโหมด BATTERY_SOC) จากนั้นตั้งกลับเป็น 0 แล้วอัปโหลดใหม่
#ifndef PZEM_SET_ADDRESS_ON_BOOT
#define PZEM_SET_ADDRESS_ON_BOOT 0
#endif
constexpr uint8_t kPzemChargeAddress = 0x01;    // PZEM ฝั่ง charge
constexpr uint8_t kPzemDischargeAddress = 0x02; // PZEM ฝั่ง discharge
constexpr uint8_t kPzemTargetAddress = 0x02;    // ใช้ตอนตั้ง ID ครั้งเดียว

// ---- การบันทึก SoC ลง AT24C32 (ใช้ในโหมด BATTERY_SOC) ----
constexpr uint32_t kSocSaveIntervalMinutes = 10u;
constexpr float kSocSaveDeltaPercent = 1.0f;

// ---- ขา GPIO ทั่วไป ----
constexpr uint8_t kStatusLedPin = PA10;   // LED on-board (active HIGH)
constexpr uint8_t kLatchTrigPin = PB3;    // Latch trigger
constexpr uint8_t kLatchUnlockPin = PA3;  // Latch unlock (active LOW)

// ---- ขา/ความเร็ว Debug serial ----
constexpr uint8_t kDebugSerialRxPin = PA15;
constexpr uint8_t kDebugSerialTxPin = PA2;
constexpr uint32_t kDebugSerialBaud = 115200;

// ---- บัส I2C ที่ใช้ร่วม (OLED + SHT31) : Wire ----
constexpr uint8_t kWireSdaPin = PA12;
constexpr uint8_t kWireSclPin = PB13;

// ---- SPI / Ethernet (W5500) สำหรับโหมด HEAD_NODE ----
constexpr uint8_t kSpiSckPin = PA5;
constexpr uint8_t kSpiMisoPin = PB4;
constexpr uint8_t kSpiMosiPin = PB5;
constexpr uint8_t kW5500CsPin = PB15;

constexpr uint8_t kHeadNodeMacAddress[6] = {0x02, 0x47, 0x07, 0x00, 0x00, 0x10};
constexpr uint32_t kHeadNodeMqttReconnectDelayMs = 5000UL;
constexpr uint32_t kHeadNodeMqttPublishIntervalMs = 10000UL;
constexpr uint16_t kHeadNodeMqttBrokerPort = 1883;
constexpr char kHeadNodeMqttBrokerHost[] = "192.168.0.99";
constexpr char kHeadNodeMqttClientId[] = "stm32g070-head-node";
constexpr char kHeadNodeMqttPublishTopic[] = "cab01/row01/col01/test/pub";
constexpr char kHeadNodeMqttSubscribeTopic[] = "cab01/row01/col01/test/sub";

// ---- Raw TCP server (W5500) สำหรับโหมด TCP_SOCKET_SERVER ----
constexpr uint16_t kTcpSocketServerPort = 5000;
constexpr uint32_t kTcpSocketSendIntervalMs = 1000UL;
constexpr bool kTcpSocketUseStaticIp = true;
constexpr uint8_t kTcpSocketStaticIp[4] = {192, 168, 0, 99};
constexpr uint8_t kTcpSocketSubnetMask[4] = {255, 255, 255, 0};
constexpr uint8_t kTcpSocketGateway[4] = {192, 168, 0, 1};
constexpr uint8_t kTcpSocketDnsServer[4] = {192, 168, 0, 1};
