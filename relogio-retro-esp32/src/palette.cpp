#include "palette.h"

namespace {
  // RGB565 das cores base, calculado de #FFB300, #3FD0FF, #39FF14, #FF6A00.
  const uint16_t kBaseColor[PALETTE_COUNT] = {
    0xFD80, // #FFB300 âmbar
    0x3E9F, // #3FD0FF ciano
    0x3FE2, // #39FF14 verde
    0xFB40, // #FF6A00 laranja
  };

  PaletteIndex g_current = PALETTE_AMBER;

  // Escurece uma cor RGB565 mantendo o matiz (usado no efeito "sombra").
  uint16_t dim(uint16_t color, float factor) {
    uint8_t r = ((color >> 11) & 0x1F);
    uint8_t g = ((color >> 5) & 0x3F);
    uint8_t b = (color & 0x1F);
    r = (uint8_t)(r * factor);
    g = (uint8_t)(g * factor);
    b = (uint8_t)(b * factor);
    return (r << 11) | (g << 5) | b;
  }
}

namespace Palette {

  void begin() {
    g_current = PALETTE_AMBER;
  }

  void autoSelectDayNight(bool isDaytime) {
    g_current = isDaytime ? PALETTE_AMBER : PALETTE_CYAN;
  }

  void next() {
    g_current = (PaletteIndex)((g_current + 1) % PALETTE_COUNT);
  }

  void previous() {
    g_current = (PaletteIndex)((g_current + PALETTE_COUNT - 1) % PALETTE_COUNT);
  }

  uint16_t digitColor() {
    return kBaseColor[g_current];
  }

  uint16_t ghostColor() {
    // Segmentos apagados: ~8% de brilho, efeito "88:88" fraco atrás dos dígitos.
    return dim(kBaseColor[g_current], 0.08f);
  }

  uint16_t textColor() {
    // Data/clima um pouco mais fracos que os dígitos principais.
    return dim(kBaseColor[g_current], 0.65f);
  }

  PaletteIndex current() {
    return g_current;
  }
}
