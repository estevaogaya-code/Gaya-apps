#pragma once
#include <RTClib.h>

// Wrapper do RTC DS3231 via I2C. O RTC guarda sempre hora LOCAL (UTC-3 já
// aplicado), então o resto do firmware não precisa lidar com fuso.
namespace RtcManager {

  // Inicializa I2C + DS3231. Retorna false se o módulo não responder.
  bool begin();

  DateTime now();

  // Ajusta o RTC a partir de um epoch UTC (ex.: vindo do NTP) já convertido
  // pra hora local.
  void setLocalTime(const DateTime &localTime);

  bool lostPower();
}
