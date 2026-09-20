#include "wifi_manager.h"
#include <WiFi.h>
#include "config.h"
#include "secrets.h"

namespace {
  uint32_t g_lastCheckMs = 0;
}

namespace WifiManager {

  void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
      delay(200);
    }
  }

  void loop(uint32_t nowMs) {
    // WiFi.setAutoReconnect cobre a maioria das quedas, mas alguns roteadores
    // deixam o STA em estado "morto" sem reconectar sozinho — força de tempos
    // em tempos como rede de segurança.
    if (nowMs - g_lastCheckMs < WIFI_RECONNECT_CHECK_MS) return;
    g_lastCheckMs = nowMs;

    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
  }
}
