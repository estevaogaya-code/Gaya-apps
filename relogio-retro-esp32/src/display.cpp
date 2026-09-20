#include "display.h"
#include <SPI.h>

namespace {
  Adafruit_ST7735 g_tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RES);
  GFXcanvas16 g_canvas(TFT_HEIGHT, TFT_WIDTH); // já em paisagem: 160x80
  const uint32_t kBacklightFreqHz = 5000;
  const uint8_t kBacklightResolutionBits = 8;
}

namespace Display {

  void begin() {
    SPI.begin(PIN_TFT_SCK, -1 /* MISO não usado */, PIN_TFT_MOSI, PIN_TFT_CS);

    // 80x160 "mini" — mesmo driver do ST7735, variante de resolução reduzida.
    g_tft.initR(INITR_MINI160x80);
    g_tft.setRotation(TFT_ROTATION); // paisagem: 160 largura x 80 altura
    g_tft.fillScreen(ST77XX_BLACK);

    // Backlight via LEDC (API core ESP32 Arduino >= 3.x).
    ledcAttach(PIN_TFT_BLK, kBacklightFreqHz, kBacklightResolutionBits);
    setBacklight(255);

    g_canvas.fillScreen(COLOR_BLACK);
  }

  GFXcanvas16 &canvas() {
    return g_canvas;
  }

  void present() {
    g_tft.drawRGBBitmap(0, 0, g_canvas.getBuffer(), TFT_HEIGHT, TFT_WIDTH);
  }

  void setBacklight(uint8_t level) {
    ledcWrite(PIN_TFT_BLK, level);
  }
}
