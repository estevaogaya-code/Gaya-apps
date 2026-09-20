#include "segments.h"

// Máscaras de 7 segmentos por dígito (bit0=a topo, b,c,d,e,f, bit6=g meio).
static const uint8_t kDigitMask[10] = {
  0x3F, // 0: a b c d e f
  0x06, // 1: b c
  0x5B, // 2: a b g e d
  0x4F, // 3: a b g c d
  0x66, // 4: f g b c
  0x6D, // 5: a f g c d
  0x7D, // 6: a f g e c d
  0x07, // 7: a b c
  0x7F, // 8: todos
  0x6F, // 9: a b c d f g
};

// grupo de cada segmento a,b,c,d,e,f,g (índice 0..6) -> 0=topo,1=laterais
// superiores,2=meio,3=laterais inferiores,4=base
const uint8_t Digit7Seg::kGroupOfSegment[7] = {0, 1, 3, 4, 3, 1, 2};

void Digit7Seg::begin(int16_t x, int16_t y, int16_t w, int16_t h, int16_t thickness) {
  x_ = x; y_ = y; w_ = w; h_ = h; thickness_ = thickness;
  currentMask_ = 0;
  targetMask_ = 0;
  animatingMask_ = 0;
}

void Digit7Seg::setValue(uint8_t value, uint32_t nowMs) {
  if (value > 9) value = 0;
  uint8_t newTarget = kDigitMask[value];
  if (newTarget == targetMask_) return; // sem mudança, nada a animar

  uint8_t turningOn = newTarget & ~currentMask_;
  uint8_t turningOff = currentMask_ & ~newTarget;

  // Segmentos que apagam: somem na hora (viram "ghost" imediatamente).
  currentMask_ &= ~turningOff;
  animatingMask_ &= ~turningOff;

  // Segmentos que acendem: entram em animação de queda, escalonados por grupo.
  for (uint8_t i = 0; i < 7; i++) {
    if (turningOn & (1 << i)) {
      animatingMask_ |= (1 << i);
      animStartMs_[i] = nowMs + kGroupOfSegment[i] * kGroupDelayMs;
    }
  }

  targetMask_ = newTarget;
}

bool Digit7Seg::isAnimating(uint32_t nowMs) const {
  for (uint8_t i = 0; i < 7; i++) {
    if (animatingMask_ & (1 << i)) {
      uint32_t elapsed = nowMs - animStartMs_[i];
      if ((int32_t)elapsed < (int32_t)kFallDurationMs) return true;
    }
  }
  return false;
}

float Digit7Seg::easeOutBack(float t) {
  const float c1 = 1.70158f;
  const float c3 = c1 + 1.0f;
  float p = t - 1.0f;
  return 1.0f + c3 * p * p * p + c1 * p * p;
}

Digit7Seg::SegRect Digit7Seg::segRect(uint8_t segIndex) const {
  int16_t t = thickness_;
  int16_t midY = y_ + h_ / 2 - t / 2;
  switch (segIndex) {
    case 0: return { x_, y_, w_, t, false };                          // a topo
    case 1: return { (int16_t)(x_ + w_ - t), y_, t, (int16_t)(h_ / 2), true };       // b top-right
    case 2: return { (int16_t)(x_ + w_ - t), (int16_t)(y_ + h_ / 2), t, (int16_t)(h_ / 2), true }; // c bottom-right
    case 3: return { x_, (int16_t)(y_ + h_ - t), w_, t, false };       // d base
    case 4: return { x_, (int16_t)(y_ + h_ / 2), t, (int16_t)(h_ / 2), true };       // e bottom-left
    case 5: return { x_, y_, t, (int16_t)(h_ / 2), true };                          // f top-left
    default: return { x_, midY, w_, t, false };                       // g meio
  }
}

void Digit7Seg::drawSegment(GFXcanvas16 &canvas, const SegRect &r, int16_t yOffset, uint16_t color) {
  canvas.fillRect(r.x, r.y + yOffset, r.w, r.h, color);
}

namespace {
  // Escurece uma cor RGB565 pra desenhar o "glow" atrás do segmento aceso.
  uint16_t dimForGlow(uint16_t color) {
    uint8_t r = (color >> 11) & 0x1F, g = (color >> 5) & 0x3F, b = color & 0x1F;
    return (uint8_t)(r * 0.35f) << 11 | (uint8_t)(g * 0.35f) << 5 | (uint8_t)(b * 0.35f);
  }
}

void Digit7Seg::draw(GFXcanvas16 &canvas, uint16_t litColor, uint16_t ghostColor, uint32_t nowMs) {
  for (uint8_t i = 0; i < 7; i++) {
    SegRect r = segRect(i);
    bool isTargetOn = targetMask_ & (1 << i);
    bool isAnimatingSeg = animatingMask_ & (1 << i);

    if (isAnimatingSeg) {
      int32_t elapsed = (int32_t)(nowMs - animStartMs_[i]);
      if (elapsed < 0) {
        // Ainda não chegou a vez do grupo: continua invisível (fora da tela).
        continue;
      }
      if (elapsed >= (int32_t)kFallDurationMs) {
        // Animação concluída: assenta o segmento e desenha na posição final.
        animatingMask_ &= ~(1 << i);
        currentMask_ |= (1 << i);
        canvas.fillRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, dimForGlow(litColor));
        drawSegment(canvas, r, 0, litColor);
      } else {
        float t = (float)elapsed / (float)kFallDurationMs;
        float eased = easeOutBack(t); // pode passar de 1.0 (overshoot) e voltar
        int16_t yOffset = (int16_t)((1.0f - eased) * -kFallDistancePx);
        drawSegment(canvas, r, yOffset, litColor);
      }
      continue;
    }

    if (isTargetOn) {
      canvas.fillRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, dimForGlow(litColor));
      drawSegment(canvas, r, 0, litColor);
    } else {
      drawSegment(canvas, r, 0, ghostColor);
    }
  }
}
