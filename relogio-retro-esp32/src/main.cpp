#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "rtc_manager.h"
#include "wifi_manager.h"
#include "ntp_manager.h"
#include "weather.h"
#include "buttons.h"
#include "palette.h"
#include "ui_clock.h"

namespace {
  int g_lastRenderedMinute = -1;

  int hhmmToMinutes(const char *hhmm) {
    int h = 0, m = 0;
    sscanf(hhmm, "%d:%d", &h, &m);
    return h * 60 + m;
  }

  // Decide âmbar (dia) ou ciano (noite) comparando a hora atual com o
  // sunrise/sunset mais recente do Open-Meteo. Se não há dado de clima ainda
  // (sem Wi-Fi no boot, por exemplo), usa uma janela fixa 06:00-18:00.
  bool computeIsDaytime(const DateTime &now) {
    int nowMinutes = now.hour() * 60 + now.minute();
    const WeatherData &w = Weather::data();

    if (w.valid) {
      int sunrise = hhmmToMinutes(w.sunrise);
      int sunset = hhmmToMinutes(w.sunset);
      return nowMinutes >= sunrise && nowMinutes < sunset;
    }
    return nowMinutes >= 6 * 60 && nowMinutes < 18 * 60;
  }
}

void setup() {
  Serial.begin(115200);

  Display::begin();
  Buttons::begin();
  Palette::begin();

  if (!RtcManager::begin()) {
    Serial.println("[RTC] DS3231 nao respondeu no I2C!");
  }

  WifiManager::begin();
  bool wifiOk = WifiManager::isConnected();
  Serial.printf("[WiFi] conectado: %s\n", wifiOk ? "sim" : "nao");

  if (wifiOk) {
    if (NtpManager::sync()) {
      Serial.println("[NTP] hora sincronizada");
    }
    Weather::fetch();
  }

  DateTime bootTime = RtcManager::now();
  Palette::autoSelectDayNight(computeIsDaytime(bootTime));

  UiClock::begin();
  UiClock::setTime(bootTime, millis());
}

void loop() {
  uint32_t nowMs = millis();

  WifiManager::loop(nowMs);
  NtpManager::loop(nowMs, WifiManager::isConnected());
  Weather::loop(nowMs, WifiManager::isConnected());
  Buttons::loop(nowMs);

  DateTime now = RtcManager::now();
  if (now.minute() != g_lastRenderedMinute) {
    g_lastRenderedMinute = now.minute();
    UiClock::setTime(now, nowMs);
  }

  UiClock::render(now, Weather::data(), nowMs);

  // Enquanto os dígitos estão caindo, renderiza rápido (~30fps) pra animação
  // ficar fluida; parado, reduz o ritmo pra sobrar CPU/energia.
  delay(UiClock::isAnimating(nowMs) ? 16 : 200);
}
