#include <Arduino.h>

#include "Config.h"

// ---------------------------------------------------------------------------
// เลือกโหมดการทำงานที่ include/Config.h (OPERATING_MODE) แล้ว dispatch ที่นี่
// ---------------------------------------------------------------------------
#if OPERATING_MODE == MODE_FRIDGE_MONITOR
#include "ModeFridgeMonitor.h"
#elif OPERATING_MODE == MODE_BATTERY_SOC
#include "ModeBatterySoc.h"
#else
#error "OPERATING_MODE ไม่ถูกต้อง: กรุณาเลือก MODE_FRIDGE_MONITOR หรือ MODE_BATTERY_SOC ใน include/Config.h"
#endif

void setup()
{
#if OPERATING_MODE == MODE_FRIDGE_MONITOR
  fridgeMonitorSetup();
#elif OPERATING_MODE == MODE_BATTERY_SOC
  batterySocSetup();
#endif
}

void loop()
{
#if OPERATING_MODE == MODE_FRIDGE_MONITOR
  fridgeMonitorLoop();
#elif OPERATING_MODE == MODE_BATTERY_SOC
  batterySocLoop();
#endif
}
