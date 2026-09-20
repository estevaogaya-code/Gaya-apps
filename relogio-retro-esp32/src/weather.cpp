#include "weather.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "secrets.h"

namespace {
  WeatherData g_data;
  uint32_t g_lastFetchMs = 0;
  bool g_everFetched = false;

  void extractHHMM(const char *iso, char *out) {
    // ISO vem como "2026-09-20T06:12"; pegamos só "06:12".
    size_t len = strlen(iso);
    if (len >= 5) {
      strncpy(out, iso + (len - 5), 5);
      out[5] = '\0';
    }
  }
}

namespace Weather {

  bool fetch() {
    if (WiFi.status() != WL_CONNECTED) return false;

    char url[256];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code&daily=sunrise,sunset"
        "&timezone=America%%2FSao_Paulo",
        (double)HOME_LATITUDE, (double)HOME_LONGITUDE);

    // Open-Meteo é https sem chave; setInsecure() pula a validação da cadeia
    // de certificado (aceitável aqui — sem dado sensível trafegando).
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      http.end();
      return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    WeatherData fresh;
    fresh.temperatureC = doc["current"]["temperature_2m"] | 0.0f;
    fresh.weatherCode = doc["current"]["weather_code"] | 0;

    const char *sunrise = doc["daily"]["sunrise"][0] | "";
    const char *sunset = doc["daily"]["sunset"][0] | "";
    if (sunrise[0]) extractHHMM(sunrise, fresh.sunrise);
    if (sunset[0]) extractHHMM(sunset, fresh.sunset);

    fresh.valid = true;
    g_data = fresh;
    return true;
  }

  void loop(uint32_t nowMs, bool wifiConnected) {
    if (!wifiConnected) return;
    if (g_everFetched && (nowMs - g_lastFetchMs) < WEATHER_UPDATE_INTERVAL_MS) return;

    if (fetch()) {
      g_everFetched = true;
    }
    g_lastFetchMs = nowMs; // não martela a API mesmo se falhar
  }

  const WeatherData &data() {
    return g_data;
  }

  const char *description(int weatherCode) {
    switch (weatherCode) {
      case 0: return "CEU LIMPO";
      case 1: return "QUASE LIMPO";
      case 2: return "PARC NUBLADO";
      case 3: return "NUBLADO";
      case 45: case 48: return "NEVOEIRO";
      case 51: case 53: case 55: return "GAROA";
      case 56: case 57: return "GAROA GELADA";
      case 61: case 63: case 65: return "CHUVA";
      case 66: case 67: return "CHUVA GELADA";
      case 71: case 73: case 75: case 77: return "NEVE";
      case 80: case 81: case 82: return "PANCADAS CHUVA";
      case 85: case 86: return "PANCADAS NEVE";
      case 95: return "TEMPESTADE";
      case 96: case 99: return "TEMP C/ GRANIZO";
      default: return "---";
    }
  }
}
