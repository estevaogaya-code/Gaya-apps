#include "rtc_manager.h"
#include <Wire.h>
#include "config.h"

namespace {
  RTC_DS3231 g_rtc;
  bool g_ok = false;
}

namespace RtcManager {

  bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    g_ok = g_rtc.begin();
    return g_ok;
  }

  DateTime now() {
    if (!g_ok) return DateTime((uint32_t)0);
    return g_rtc.now();
  }

  void setLocalTime(const DateTime &localTime) {
    if (!g_ok) return;
    g_rtc.adjust(localTime);
  }

  bool lostPower() {
    if (!g_ok) return true;
    return g_rtc.lostPower();
  }
}
