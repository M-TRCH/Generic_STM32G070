#include "RtcManager.h"

RTC_HandleTypeDef rtcHandle = {};
uint32_t rtcBootCount = 0;

namespace {

constexpr uint32_t kRtcInitMarker = 0xA5A55A5Au;
constexpr uint32_t kRtcDefaultYear = 24; // 2-digit year (2000 + value)
constexpr uint32_t kRtcDefaultMonth = RTC_MONTH_JANUARY;
constexpr uint32_t kRtcDefaultDate = 1;
constexpr uint32_t kRtcDefaultWeekDay = RTC_WEEKDAY_MONDAY;
constexpr uint32_t kRtcDefaultHours = 0;
constexpr uint32_t kRtcDefaultMinutes = 0;
constexpr uint32_t kRtcDefaultSeconds = 0;

// Sakamoto's algorithm: returns 1=Monday .. 7=Sunday (STM32 RTC_WEEKDAY_*)
uint8_t calculateRtcWeekDay(uint16_t year, uint8_t month, uint8_t day)
{
  static const uint8_t kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

  if (month < 3) {
    --year;
  }

  uint16_t weekDay = static_cast<uint16_t>(year + (year / 4) - (year / 100) + (year / 400)
                                           + kMonthOffsets[month - 1] + day);
  weekDay %= 7; // 0 = Sunday

  return static_cast<uint8_t>(weekDay == 0 ? RTC_WEEKDAY_SUNDAY : weekDay);
}

} // namespace

// Override the weak HAL_RTC_MspInit. All clock setup is done manually in
// initRtc() before HAL_RTC_Init is called, so nothing extra is needed here.
extern "C" void HAL_RTC_MspInit(RTC_HandleTypeDef *hrtc)
{
  (void)hrtc;
}

bool initRtc()
{
  // STM32G0: enable the PWR peripheral clock, then unlock backup domain access
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  // Enable LSI and wait until it is ready
  __HAL_RCC_LSI_ENABLE();
  uint32_t lsiTimeout = HAL_GetTick() + 500u;
  while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {
    if (HAL_GetTick() > lsiTimeout) {
      Serial.println(F("RTC: LSI timeout"));
      return false;
    }
  }
  Serial.println(F("RTC: LSI ready"));

  // If the RTC clock source is not already LSI, reset the backup domain so the
  // RTCSEL bits can be changed. This also clears the backup registers, so the
  // init marker disappears and the default time is written below.
  if ((RCC->BDCR & RCC_BDCR_RTCSEL_Msk) != RCC_RTCCLKSOURCE_LSI) {
    Serial.println(F("RTC: resetting backup domain"));
    __HAL_RCC_BACKUPRESET_FORCE();
    __HAL_RCC_BACKUPRESET_RELEASE();
    HAL_PWR_EnableBkUpAccess();
  }

  // Select LSI as the RTC kernel clock and enable it
  __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSI);
  __HAL_RCC_RTC_ENABLE();

  // STM32G0 has a SEPARATE RTC register-interface (APB) clock. Without it the
  // RTC registers cannot be accessed and HAL_RTC_Init() fails. This is the key
  // difference from F1/F4 families.
  __HAL_RCC_RTCAPB_CLK_ENABLE();

  rtcHandle.Instance = RTC;
  rtcHandle.Init.HourFormat = RTC_HOURFORMAT_24;
  // LSI ~32 kHz: 32000 / (127+1) / (249+1) = 1 Hz
  rtcHandle.Init.AsynchPrediv = 127;
  rtcHandle.Init.SynchPrediv = 249;
  rtcHandle.Init.OutPut = RTC_OUTPUT_DISABLE;
  rtcHandle.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  rtcHandle.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  rtcHandle.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;

  if (HAL_RTC_Init(&rtcHandle) != HAL_OK) {
    Serial.println(F("RTC: HAL_RTC_Init failed"));
    return false;
  }
  Serial.println(F("RTC: HAL_RTC_Init OK"));

  if (HAL_RTCEx_BKUPRead(&rtcHandle, RTC_BKP_DR0) != kRtcInitMarker) {
    Serial.println(F("RTC: first init, setting default time"));
    RTC_TimeTypeDef time = {};
    RTC_DateTypeDef date = {};

    time.Hours = kRtcDefaultHours;
    time.Minutes = kRtcDefaultMinutes;
    time.Seconds = kRtcDefaultSeconds;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;

    date.Year = kRtcDefaultYear;
    date.Month = kRtcDefaultMonth;
    date.Date = kRtcDefaultDate;
    date.WeekDay = kRtcDefaultWeekDay;

    if (HAL_RTC_SetTime(&rtcHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
      Serial.println(F("RTC: SetTime failed"));
      return false;
    }
    if (HAL_RTC_SetDate(&rtcHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
      Serial.println(F("RTC: SetDate failed"));
      return false;
    }

    HAL_RTCEx_BKUPWrite(&rtcHandle, RTC_BKP_DR0, kRtcInitMarker);
    HAL_RTCEx_BKUPWrite(&rtcHandle, RTC_BKP_DR1, 0);
  } else {
    Serial.println(F("RTC: already initialized, keeping time"));
  }

  return true;
}

bool readRtc(RTC_TimeTypeDef &time, RTC_DateTypeDef &date)
{
  // HAL_RTC_GetTime must be called before HAL_RTC_GetDate: reading the time
  // locks the shadow registers, and reading the date unlocks them.
  if (HAL_RTC_GetTime(&rtcHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }
  if (HAL_RTC_GetDate(&rtcHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }
  return true;
}

bool setRtcDateTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hours, uint8_t minutes, uint8_t seconds)
{
  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31
      || hours > 23 || minutes > 59 || seconds > 59) {
    return false;
  }

  RTC_TimeTypeDef time = {};
  RTC_DateTypeDef date = {};

  time.Hours = hours;
  time.Minutes = minutes;
  time.Seconds = seconds;
  time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  time.StoreOperation = RTC_STOREOPERATION_RESET;

  date.Year = static_cast<uint8_t>(year - 2000);
  date.Month = month;
  date.Date = day;
  date.WeekDay = calculateRtcWeekDay(year, month, day);

  if (HAL_RTC_SetTime(&rtcHandle, &time, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }
  if (HAL_RTC_SetDate(&rtcHandle, &date, RTC_FORMAT_BIN) != HAL_OK) {
    return false;
  }
  return true;
}

uint32_t incrementRtcBootCount()
{
  uint32_t bootCount = HAL_RTCEx_BKUPRead(&rtcHandle, RTC_BKP_DR1);
  ++bootCount;
  HAL_RTCEx_BKUPWrite(&rtcHandle, RTC_BKP_DR1, bootCount);
  return bootCount;
}

void printRtcReading(const RTC_TimeTypeDef &time, const RTC_DateTypeDef &date)
{
  char buffer[40] = {};
  snprintf(buffer, sizeof(buffer), "RTC %02u:%02u:%02u  Date %02u/%02u/20%02u",
           static_cast<unsigned>(time.Hours), static_cast<unsigned>(time.Minutes),
           static_cast<unsigned>(time.Seconds), static_cast<unsigned>(date.Date),
           static_cast<unsigned>(date.Month), static_cast<unsigned>(date.Year));
  Serial.println(buffer);
}

// ใช้สูตร days-from-civil (ปี 2000-2099 เป็นบวกเสมอ จึงไม่ต้องจัดการค่าติดลบ)
uint64_t rtcEpochSeconds()
{
  RTC_TimeTypeDef time = {};
  RTC_DateTypeDef date = {};
  if (!readRtc(time, date)) {
    return 0; // sentinel: RTC ยังอ่านไม่ได้
  }

  const uint16_t year = static_cast<uint16_t>(2000 + date.Year);
  const uint8_t month = date.Month;
  const uint8_t day = date.Date;

  const uint16_t y = static_cast<uint16_t>(year - (month <= 2 ? 1 : 0));
  const uint32_t era = y / 400u;
  const uint32_t yoe = y - era * 400u;
  const uint32_t doy = (153u * (month > 2 ? (month - 3u) : (month + 9u)) + 2u) / 5u + day - 1u;
  const uint32_t doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  const uint64_t days = static_cast<uint64_t>(era) * 146097u + doe;

  return days * 86400ull
         + static_cast<uint32_t>(time.Hours) * 3600u
         + static_cast<uint32_t>(time.Minutes) * 60u
         + static_cast<uint32_t>(time.Seconds);
}
