#pragma once
#include <Arduino.h>

// ─── Display ST7735S 0.96" 80x160 (SPI) ─────────────────────────────────────
// Ajustar conforme a placa ESP32-C3 usada (DevKitM-1 / Super Mini / etc).
#define PIN_TFT_SCK   4
#define PIN_TFT_MOSI  6
#define PIN_TFT_CS    7
#define PIN_TFT_DC    2
#define PIN_TFT_RES   3
#define PIN_TFT_BLK   5   // PWM (LEDC) para brilho

#define TFT_WIDTH   80
#define TFT_HEIGHT  160
// 0 = retrato nativo; giramos para paisagem (160x80) na inicialização.
#define TFT_ROTATION 1

// ─── I2C / RTC DS3231 ────────────────────────────────────────────────────────
#define PIN_I2C_SDA  8
#define PIN_I2C_SCL  9

// ─── Botões (pull-up interno, ativo em nível baixo) ─────────────────────────
#define PIN_BTN_1  0   // avança cor
#define PIN_BTN_2  1   // volta cor
#define BUTTON_DEBOUNCE_MS 50

// ─── Fuso horário fixo (Brasil, sem horário de verão em 2026) ───────────────
#define TIMEZONE_OFFSET_SEC   (-3 * 3600)
#define NTP_SERVER_1  "pool.ntp.org"
#define NTP_SERVER_2  "a.st1.ntp.br"
#define NTP_RESYNC_INTERVAL_MS  (6UL * 60UL * 60UL * 1000UL) // resync a cada 6h

// ─── Clima (Open-Meteo, sem chave) ───────────────────────────────────────────
#define WEATHER_UPDATE_INTERVAL_MS  (20UL * 60UL * 1000UL) // 20 min
#define WEATHER_HTTP_TIMEOUT_MS     8000

// ─── Wi-Fi ───────────────────────────────────────────────────────────────────
#define WIFI_CONNECT_TIMEOUT_MS   15000
#define WIFI_RECONNECT_CHECK_MS   5000

// ─── Paleta de cores (RGB565) ────────────────────────────────────────────────
// Âmbar #FFB300, Ciano #3FD0FF, Verde #39FF14, Laranja #FF6A00
#define COLOR_BLACK   0x0000

enum PaletteIndex : uint8_t {
  PALETTE_AMBER = 0,
  PALETTE_CYAN,
  PALETTE_GREEN,
  PALETTE_ORANGE,
  PALETTE_COUNT
};
