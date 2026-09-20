#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "config.h"

// Wrapper do display ST7735S: inicialização, orientação paisagem 160x80 e
// um framebuffer offscreen (GFXcanvas16) para desenhar sem flicker e depois
// empurrar de uma vez via SPI (necessário pra animação ficar fluida).
namespace Display {

  void begin();

  // Canvas de desenho (160 largura x 80 altura, já na orientação paisagem).
  GFXcanvas16 &canvas();

  // Copia o canvas inteiro pro display físico.
  void present();

  // Brilho do backlight via PWM (0-255).
  void setBacklight(uint8_t level);
}
