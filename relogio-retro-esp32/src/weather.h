#pragma once
#include <Arduino.h>

struct WeatherData {
  bool valid = false;
  float temperatureC = 0;
  int weatherCode = 0;
  // "HH:MM" em hora local, já como a Open-Meteo devolve (timezone=auto não
  // usado; pedimos os horários em ISO e extraímos HH:MM direto).
  char sunrise[6] = "00:00";
  char sunset[6] = "00:00";
};

namespace Weather {

  // Busca uma vez (bloqueia até WEATHER_HTTP_TIMEOUT_MS). Retorna true em sucesso.
  bool fetch();

  // Chamar periodicamente; refaz o fetch a cada WEATHER_UPDATE_INTERVAL_MS.
  void loop(uint32_t nowMs, bool wifiConnected);

  const WeatherData &data();

  // Texto curto em pt-br pro weather_code do Open-Meteo (WMO code).
  const char *description(int weatherCode);
}
