#include "ntp_manager.h"
#include <time.h>
#include "config.h"
#include "rtc_manager.h"

namespace {
  uint32_t g_lastAttemptMs = 0;
  bool g_everSynced = false;
  const uint32_t kRetryIntervalMs = 30UL * 1000UL; // enquanto não sincronizou
}

namespace NtpManager {

  bool sync() {
    // Fuso fixo UTC-3, sem horário de verão (offset=0).
    configTime(TIMEZONE_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeInfo;
    // getLocalTime tenta por até 10s (10 x 500ms) até o SNTP responder.
    if (!getLocalTime(&timeInfo, 10000)) {
      return false;
    }

    DateTime localNow(
        timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
        timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    RtcManager::setLocalTime(localNow);

    g_everSynced = true;
    return true;
  }

  void loop(uint32_t nowMs, bool wifiConnected) {
    if (!wifiConnected) return;

    uint32_t interval = g_everSynced ? NTP_RESYNC_INTERVAL_MS : kRetryIntervalMs;
    if (g_lastAttemptMs != 0 && (nowMs - g_lastAttemptMs) < interval) return;

    g_lastAttemptMs = nowMs;
    sync(); // sync() já marca g_everSynced internamente se der certo
  }
}
