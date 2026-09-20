#pragma once
#include <RTClib.h>
#include "weather.h"

// Composição da tela: data no topo, HH:MM em 7 segmentos no centro (com
// animação de queda em cascata a cada minuto) e clima embaixo.
namespace UiClock {

  void begin();

  // Atualiza os dígitos-alvo (só dispara animação se HH:MM mudou).
  void setTime(const DateTime &now, uint32_t nowMs);

  // Desenha o frame atual no canvas do Display e mostra na tela.
  // Chamar em loop rápido (~30ms) enquanto algum dígito está animando;
  // pode ser chamado mais devagar quando está tudo parado.
  void render(const DateTime &now, const WeatherData &weather, uint32_t nowMs);

  bool isAnimating(uint32_t nowMs);
}
