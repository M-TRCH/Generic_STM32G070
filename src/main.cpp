#include <Arduino.h>
#include "Config.h"

// ---------------------------------------------------------------------------
// เลือกโหมดการทำงานที่ include/Config.h (OPERATING_MODE) แล้ว dispatch ที่นี่
// ---------------------------------------------------------------------------
#if OPERATING_MODE == MODE_FRIDGE_MONITOR
#include "ModeFridgeMonitor.h"
#elif OPERATING_MODE == MODE_BATTERY_SOC
#include "ModeBatterySoc.h"
#elif OPERATING_MODE == MODE_HEAD_NODE
#include "ModeHeadNode.h"
#elif OPERATING_MODE == MODE_TCP_SOCKET_SERVER
#include "ModeTcpSocketServer.h"
#else
#error "OPERATING_MODE ไม่ถูกต้อง: กรุณาเลือก MODE_FRIDGE_MONITOR, MODE_BATTERY_SOC, MODE_HEAD_NODE หรือ MODE_TCP_SOCKET_SERVER ใน include/Config.h"
#endif

void setup()
{
#if OPERATING_MODE == MODE_FRIDGE_MONITOR
  fridgeMonitorSetup();
#elif OPERATING_MODE == MODE_BATTERY_SOC
  batterySocSetup();
#elif OPERATING_MODE == MODE_HEAD_NODE
  headNodeSetup();
#elif OPERATING_MODE == MODE_TCP_SOCKET_SERVER
  tcpSocketServerSetup();
#endif
}

void loop()
{
#if OPERATING_MODE == MODE_FRIDGE_MONITOR
  fridgeMonitorLoop();
#elif OPERATING_MODE == MODE_BATTERY_SOC
  batterySocLoop();
#elif OPERATING_MODE == MODE_HEAD_NODE
  headNodeLoop();
#elif OPERATING_MODE == MODE_TCP_SOCKET_SERVER
  tcpSocketServerLoop();
#endif
}

