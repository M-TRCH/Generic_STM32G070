#pragma once

#include <Arduino.h>

#pragma pack(push, 1)
struct BatteryTelemetryTcpPacket
{
  float voltageV = NAN;
  float currentA = NAN;
  float batteryPercent = NAN;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  char batState[12] = {};
  float remainingUsageTimeHours = NAN;
  float fullChargeEstimateTimeHours = NAN;
};
#pragma pack(pop)

static_assert(sizeof(BatteryTelemetryTcpPacket) == 40u,
              "BatteryTelemetryTcpPacket size changed; update host parser");

void tcpSocketServerSetup();
void tcpSocketServerLoop();