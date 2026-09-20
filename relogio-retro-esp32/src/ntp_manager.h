#pragma once
#include <Arduino.h>

namespace NtpManager {

  // Só funciona com Wi-Fi conectado. Retorna true se conseguiu sincronizar
  // o horário do sistema (que já sai em hora local, UTC-3 fixo).
  bool sync();

  // Chamar periodicamente (após ter Wi-Fi); resincroniza a cada
  // NTP_RESYNC_INTERVAL_MS e grava no RTC quando bem-sucedido.
  void loop(uint32_t nowMs, bool wifiConnected);
}
