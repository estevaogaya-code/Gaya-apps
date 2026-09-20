#include "buttons.h"
#include "config.h"
#include "palette.h"

namespace {
  struct DebouncedButton {
    uint8_t pin;
    bool stableState = HIGH;   // HIGH = solto (pull-up)
    bool lastRawState = HIGH;
    uint32_t lastChangeMs = 0;
  };

  DebouncedButton g_btn1{PIN_BTN_1};
  DebouncedButton g_btn2{PIN_BTN_2};

  // Retorna true uma única vez, no instante em que o botão é reconhecido
  // como pressionado (borda de descida já debounced).
  bool pressedEdge(DebouncedButton &b, uint32_t nowMs) {
    bool raw = digitalRead(b.pin);
    if (raw != b.lastRawState) {
      b.lastRawState = raw;
      b.lastChangeMs = nowMs;
    }
    if ((nowMs - b.lastChangeMs) >= BUTTON_DEBOUNCE_MS && b.stableState != raw) {
      bool wasHigh = (b.stableState == HIGH);
      b.stableState = raw;
      if (wasHigh && raw == LOW) return true; // borda de descida = pressionou
    }
    return false;
  }
}

namespace Buttons {

  void begin() {
    pinMode(PIN_BTN_1, INPUT_PULLUP);
    pinMode(PIN_BTN_2, INPUT_PULLUP);
  }

  void loop(uint32_t nowMs) {
    if (pressedEdge(g_btn1, nowMs)) {
      Palette::next();
    }
    if (pressedEdge(g_btn2, nowMs)) {
      Palette::previous();
    }
  }
}
