#pragma once

#include "Sensors.h"

// อัปเดตสถานะ SoC แบบ hybrid (Coulomb counting + OCV calibration + anchor)
float updateSocState(const Pzem017Reading &reading);

// แปลง SoC (%) เป็นความจุคงเหลือ (Ah)
float estimateRemainingCapacityAh(float socPercent);
