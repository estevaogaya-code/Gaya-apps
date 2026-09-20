#pragma once
#include <Arduino.h>
#include "config.h"

// Paleta de cores do mostrador VFD e escolha manual via botões.
namespace Palette {

  void begin();

  // Define a cor automaticamente a partir da comparação hora atual x
  // sunrise/sunset (chamado uma vez ao ligar, antes de qualquer escolha manual).
  void autoSelectDayNight(bool isDaytime);

  void next();     // Botão 1
  void previous();  // Botão 2

  uint16_t digitColor();      // cor dos segmentos acesos
  uint16_t ghostColor();      // cor "sombra" dos segmentos apagados (dim)
  uint16_t textColor();       // cor de data/clima (mesma família, um pouco mais fraca)

  PaletteIndex current();
}
