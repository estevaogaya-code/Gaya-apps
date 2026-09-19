#include <Adafruit_NeoPixel.h>
#include "status_led.h"
#include "config.h"
#include "globals.h"

Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

void statusLedInit() {
  rgbLed.begin();
  rgbLed.setBrightness(120); // limite geral, evita ofuscar / puxar corrente demais
  rgbLed.show();
}

// LED RGB DE STATUS - azul "batimento cardiaco" em espera, vermelho fixo
// quando um estimulo externo (sensor) dispara o ciclo de movimento
uint8_t heartbeatBrightness(unsigned long t) {
  unsigned long p = t % 1000UL;           // ciclo de 1s
  if (p < 80)   return map(p, 0, 80, 0, 255);          // "lub" subindo
  if (p < 160)  return map(p, 80, 160, 255, 60);        // "lub" descendo
  if (p < 220)  return map(p, 160, 220, 60, 255);       // "dub" subindo
  if (p < 400)  return map(p, 220, 400, 255, 0);        // "dub" descendo
  return 0;                                              // pausa entre batidas
}

void updateStatusLed() {
  if (movementActive) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0)); // vermelho fixo = acionado
  } else {
    uint8_t b = heartbeatBrightness(millis());
    rgbLed.setPixelColor(0, rgbLed.Color(0, 0, b));   // azul pulsando = em espera
  }
  rgbLed.show();
}
