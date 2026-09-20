#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>

// Um dígito em 7 segmentos com animação de "queda em cascata" (efeito Tetris)
// para os segmentos que precisam acender ao trocar de valor.
//
// Ordem das camadas, do topo pra base, cada uma ~90ms depois da anterior:
//   A (topo) -> B/F (laterais superiores) -> G (meio) -> C/E (laterais
//   inferiores) -> D (base)
class Digit7Seg {
 public:
  // x,y = canto superior esquerdo do dígito; w,h = tamanho total do dígito.
  void begin(int16_t x, int16_t y, int16_t w, int16_t h, int16_t thickness);

  // Define o novo valor (0-9). Detecta quais segmentos precisam animar.
  void setValue(uint8_t value, uint32_t nowMs);

  // Desenha o estado atual (ghost + segmentos acesos/animando) no canvas.
  void draw(GFXcanvas16 &canvas, uint16_t litColor, uint16_t ghostColor, uint32_t nowMs);

  bool isAnimating(uint32_t nowMs) const;

 private:
  struct SegRect { int16_t x, y, w, h; bool vertical; };

  static const uint8_t kGroupOfSegment[7]; // a,b,c,d,e,f,g -> grupo 0..4
  static const uint32_t kGroupDelayMs = 90;
  static const uint32_t kFallDurationMs = 220;
  static const int16_t kFallDistancePx = 24;

  SegRect segRect(uint8_t segIndex) const;
  void drawSegment(GFXcanvas16 &canvas, const SegRect &r, int16_t yOffset, uint16_t color);
  static float easeOutBack(float t);

  int16_t x_ = 0, y_ = 0, w_ = 0, h_ = 0, thickness_ = 4;
  uint8_t currentMask_ = 0;      // segmentos já assentados (ligados, sem animação)
  uint8_t targetMask_ = 0;       // segmentos que devem ficar ligados no valor atual
  uint8_t animatingMask_ = 0;    // segmentos em queda (bit setado = animando)
  uint32_t animStartMs_[7] = {0};
};
