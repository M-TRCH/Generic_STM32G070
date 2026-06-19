#pragma once

#include <Arduino.h>
#include "Sensors.h"

// เริ่มต้นฮาร์ดแวร์จอ TFT 3.5" + วาด layout คงที่
void initTftDashboard();

// อัปเดตค่าทั้งหมดบน dashboard (วาดเฉพาะค่าที่เปลี่ยนเพื่อกันกระพริบ)
void updateTftDashboard(const Sht40Reading &sht, const Sht40Reading &sht31,
                        const Ina180Reading &ina, uint32_t runtimeSeconds,
                        uint32_t savedRuntimeSeconds);
