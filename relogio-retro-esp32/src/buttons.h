#pragma once
#include <Arduino.h>

namespace Buttons {

  void begin();

  // Lê os 2 botões (debounce ~50ms) e chama Palette::next()/previous() na
  // borda de descida (nível baixo = pressionado). Chamar a cada loop.
  void loop(uint32_t nowMs);
}
