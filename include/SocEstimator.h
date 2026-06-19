#pragma once

#include "Sensors.h"

enum class SocSource : uint8_t
{
	Unknown = 0,
	EepromRestore,
	OcvInit,
	CoulombCount,
	RestOcvCorrection,
	FullAnchor,
	EmptyAnchor,
};

float getSocFilteredVoltage();
SocSource getSocBootSource();
SocSource getSocSource();
const __FlashStringHelper *socSourceText(SocSource source);

// อัปเดตสถานะ SoC แบบ hybrid (Coulomb counting + OCV calibration + anchor)
void initializeSocEstimator();
void initializeSocEstimator(float savedSocPercent);
float updateSocState(const Pzem017Reading &reading);
float updateSocState(float voltageV, float currentA);
float updateSocState(const Pzem017Reading &chargeReading, bool chargeValid,
					 const Pzem017Reading &dischargeReading, bool dischargeValid);

// แปลง SoC (%) เป็นความจุคงเหลือ (Ah)
float estimateRemainingCapacityAh(float socPercent);
