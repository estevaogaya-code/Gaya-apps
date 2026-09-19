// Driver minimo para o controlador ST7735S via SPI (ESP-IDF spi_master).
// Escrito para um panel de 0.96" 80x160 (comum em breakouts baratos).
// Nao depende de nenhuma lib third-party (Adafruit-GFX etc.) de proposito,
// para manter o firmware inicial auto-contido.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    ST7735_ROTATION_0 = 0,
    ST7735_ROTATION_90,
    ST7735_ROTATION_180,
    ST7735_ROTATION_270,
} st7735_rotation_t;

// Inicializa SPI, GPIOs de controle (DC/RST/CS), o backlight (LEDC/PWM)
// e envia a sequencia de init do ST7735S. Deve ser chamada uma vez no boot.
esp_err_t st7735_init(void);

// Ajusta a orientacao logica do frame buffer (troca largura/altura quando
// necessario). Use ST7735_ROTATION_90/270 se o display fisico ficar
// montado na vertical dentro do furo do relogio.
void st7735_set_rotation(st7735_rotation_t rotation);

// Largura/altura logicas atuais (dependem da rotacao).
uint16_t st7735_width(void);
uint16_t st7735_height(void);

// Duty do backlight em 0..100%.
void st7735_set_backlight(uint8_t percent);

// --- Primitivas de desenho (cores em RGB565) ---
void st7735_fill_screen(uint16_t color);
void st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void st7735_draw_pixel(int16_t x, int16_t y, uint16_t color);
void st7735_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void st7735_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void st7735_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
// Circulo com espessura de traco (0 = so o contorno de 1px).
void st7735_draw_circle(int16_t cx, int16_t cy, int16_t radius, uint16_t color);
void st7735_fill_circle(int16_t cx, int16_t cy, int16_t radius, uint16_t color);

// --- Texto (fonte 5x7 embutida em font5x7.h, ver display/font5x7.h) ---
// scale multiplica cada pixel do glifo (1 = 5x7, 2 = 10x14, ...).
// Retorna a largura em pixels ocupada pelo texto desenhado.
int16_t st7735_draw_text(int16_t x, int16_t y, const char *text,
                          uint16_t color, uint16_t bg_color, uint8_t scale);

// Largura em pixels que `text` ocuparia com a fonte embutida, sem desenhar.
int16_t st7735_text_width(const char *text, uint8_t scale);
