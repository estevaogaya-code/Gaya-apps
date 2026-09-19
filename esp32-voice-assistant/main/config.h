// Mapa de pinos e constantes globais do assistente de voz automotivo.
// Ver README.md para o desenho do circuito completo.
#pragma once

#include "driver/gpio.h"
#include "driver/i2s_std.h"

// ---------------------------------------------------------------------
// Display ST7735S, 0.96", 80x160, SPI
// ---------------------------------------------------------------------
#define VA_PIN_LCD_SCK   GPIO_NUM_12
#define VA_PIN_LCD_MOSI  GPIO_NUM_11
#define VA_PIN_LCD_CS    GPIO_NUM_10
#define VA_PIN_LCD_DC    GPIO_NUM_9
#define VA_PIN_LCD_RST   GPIO_NUM_8
#define VA_PIN_LCD_BLK   GPIO_NUM_7 // PWM (LEDC)

#define VA_LCD_SPI_HOST       SPI2_HOST
#define VA_LCD_SPI_CLOCK_HZ   (26 * 1000 * 1000)

// Resolucao fisica do panel (paisagem: painel montado giudo 90 graus no
// furo redondo do relogio, mas o glass continua 80x160 - ver
// st7735_set_rotation()).
#define VA_LCD_WIDTH  80
#define VA_LCD_HEIGHT 160

// ---------------------------------------------------------------------
// Microfone INMP441 (I2S digital, RX) - canal esquerdo (L/R -> GND)
// ---------------------------------------------------------------------
#define VA_PIN_MIC_WS    GPIO_NUM_4  // LRCLK / word select
#define VA_PIN_MIC_BCLK  GPIO_NUM_5
#define VA_PIN_MIC_SD    GPIO_NUM_6  // dados (saida do INMP441 -> entrada do ESP32)

#define VA_I2S_MIC_PORT       I2S_NUM_0
#define VA_I2S_MIC_SAMPLE_RATE 16000 // suficiente para voz; WakeNet/MultiNet usam 16kHz

// ---------------------------------------------------------------------
// Amplificador MAX98357A (I2S digital, TX) + alto-falante 3-4W
// GAIN flutuando = 9dB. SD preso em VCC = sempre habilitado (sem shutdown).
// ---------------------------------------------------------------------
#define VA_PIN_AMP_BCLK  GPIO_NUM_16
#define VA_PIN_AMP_LRC   GPIO_NUM_17
#define VA_PIN_AMP_DIN   GPIO_NUM_18

#define VA_I2S_AMP_PORT        I2S_NUM_1
#define VA_I2S_AMP_SAMPLE_RATE 16000 // igualado ao audio de TTS; ver tts_player.c

// ---------------------------------------------------------------------
// Botao fisico de push-to-talk (fallback seguro ao wake word em ingles)
// ---------------------------------------------------------------------
// (o GPIO real vem de Kconfig CONFIG_VA_PIN_PTT_BUTTON, ativo em nivel
// baixo com pull-up interno)

// ---------------------------------------------------------------------
// Paleta de cores (RGB565) - fundo preto, texto/icones ciano #22D3EE
// ---------------------------------------------------------------------
#define VA_COLOR_BLACK   0x0000
#define VA_COLOR_WHITE   0xFFFF
// 0x22D3EE -> R=0x22(5b=4) G=0xD3(6b=52) B=0xEE(5b=29) -> RGB565 0x269D
#define VA_COLOR_CYAN    0x269D
#define VA_COLOR_CYAN_DIM 0x12AC // ciano ~40% de brilho, usado em elementos secundarios
#define VA_COLOR_RED     0xF800 // erros / sem wifi
