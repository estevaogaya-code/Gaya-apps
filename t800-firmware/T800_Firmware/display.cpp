#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include "display.h"
#include "config.h"
#include "globals.h"

Adafruit_ST7735 tft = Adafruit_ST7735(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// ---------------------------------------------------------------------------
// Animacao de boot estilo terminal (texto verde, paisagem)
// ---------------------------------------------------------------------------
const char* BOOT_WORDS[] = {
  "INIT", "BOOT", "SCAN", "SYS", "MEM", "SERVO", "PWM", "I2C",
  "EYE", "LOAD", "ACT", "CAL", "WIFI", "HC_SR04"
};
const int BOOT_WORDS_COUNT = sizeof(BOOT_WORDS) / sizeof(BOOT_WORDS[0]);

static String randomHex(int digits) {
  String s = "0x";
  const char* hexChars = "0123456789ABCDEF";
  for (int i = 0; i < digits; i++) s += hexChars[random(0, 16)];
  return s;
}

static String randomBootLine() {
  String w = BOOT_WORDS[random(0, BOOT_WORDS_COUNT)];
  String status = (random(0, 100) > 15) ? "OK" : "ERR";
  return randomHex(3) + " " + w + " " + status;
}

void displayInit() {
  SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.initR(INITR_MINI160x80);       // ajuste conforme o driver exato do seu painel
  tft.setRotation(1);                // paisagem (160x80)

  ledcAttach(PIN_TFT_BLK, LEDC_FREQ_BACKLIGHT, LEDC_RES_BACKLIGHT);
  setBacklight(backlightLevel);
}

void setBacklight(uint8_t level) {
  backlightLevel = level;
  ledcWrite(PIN_TFT_BLK, level);
}

// desenha N colunas de texto subindo, por 'durationMs' milissegundos
void terminalAnimation(unsigned long durationMs) {
  const int cols = 3;
  const int colWidth = tft.width() / cols;
  const int lineHeight = 9;
  const int maxLines = tft.height() / lineHeight;

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);

  int lineCount[cols] = {0, 0, 0};
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    for (int c = 0; c < cols; c++) {
      // rola a coluna pra cima
      if (lineCount[c] >= maxLines) {
        // limpa a coluna e recomeca de baixo (efeito simplificado, sem scroll real de pixel)
        tft.fillRect(c * colWidth, 0, colWidth, tft.height(), ST77XX_BLACK);
        lineCount[c] = 0;
      }
      tft.setCursor(c * colWidth + 2, tft.height() - (lineCount[c] + 1) * lineHeight);
      tft.print(randomBootLine());
      lineCount[c]++;
    }
    delay(140);
  }
}

void showStandby() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(4, tft.height() / 2 - 4);
  tft.print("STANDBY");
}

void showTargetAcquired() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(4, tft.height() / 2 - 4);
  tft.print("TARGET ACQUIRED");
}

void showPairingMode() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 4);
  tft.print("PAIRING MODE");
  tft.setCursor(4, 16);
  tft.print(WiFi.softAPIP().toString());
}

void showRemoteModeBadge() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 4);
  tft.print("REMOTE MODE");
}
