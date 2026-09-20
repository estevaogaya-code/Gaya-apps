#include "ui_clock.h"
#include "display.h"
#include "palette.h"
#include "segments.h"
#include "config.h"

namespace {
  const int16_t kDigitW = 30;
  const int16_t kDigitH = 44;
  const int16_t kThickness = 6;
  const int16_t kGap = 6;
  const int16_t kColonGap = 14;
  const int16_t kDigitY = 17; // abaixo da linha de data

  Digit7Seg g_digits[4]; // H dezena, H unidade, M dezena, M unidade
  int16_t g_colonX = 0;

  const char *kWeekDay[7] = {"DOM", "SEG", "TER", "QUA", "QUI", "SEX", "SAB"};
  const char *kMonth[12] = {
      "JAN", "FEV", "MAR", "ABR", "MAI", "JUN",
      "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};

  void drawDateRow(GFXcanvas16 &canvas, const DateTime &now, uint16_t color) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %02d %s", kWeekDay[now.dayOfTheWeek()],
              now.day(), kMonth[now.month() - 1]);

    canvas.setTextSize(1);
    canvas.setTextColor(color);
    int16_t x1, y1; uint16_t w, h;
    canvas.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((TFT_HEIGHT - w) / 2, 3);
    canvas.print(buf);
  }

  void drawWeatherRow(GFXcanvas16 &canvas, const WeatherData &weather, uint16_t color) {
    // \xF8 e \xFA sao "°" e "·" na fonte padrao (codepage 437) do Adafruit_GFX.
    char buf[32];
    if (weather.valid) {
      snprintf(buf, sizeof(buf), "%.0f\xF8""C \xFA %s", (double)weather.temperatureC,
                Weather::description(weather.weatherCode));
    } else {
      snprintf(buf, sizeof(buf), "-- \xFA SEM DADOS");
    }

    canvas.setTextSize(1);
    canvas.setTextColor(color);
    int16_t x1, y1; uint16_t w, h;
    canvas.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    canvas.setCursor((TFT_HEIGHT - w) / 2, TFT_WIDTH - 11);
    canvas.print(buf);
  }

  void drawColon(GFXcanvas16 &canvas, uint16_t color) {
    int16_t dotSize = 6;
    int16_t cx = g_colonX + (kColonGap - dotSize) / 2;
    canvas.fillRect(cx, kDigitY + kDigitH / 3 - dotSize / 2, dotSize, dotSize, color);
    canvas.fillRect(cx, kDigitY + (kDigitH * 2) / 3 - dotSize / 2, dotSize, dotSize, color);
  }
}

namespace UiClock {

  void begin() {
    int16_t totalWidth = 4 * kDigitW + 3 * kGap + kColonGap;
    int16_t startX = (TFT_HEIGHT - totalWidth) / 2;

    int16_t x0 = startX;
    int16_t x1 = x0 + kDigitW + kGap;
    g_colonX = x1 + kDigitW + kGap / 2; // pequena folga antes do gap de colon
    int16_t x2 = x1 + kDigitW + kGap + kColonGap;
    int16_t x3 = x2 + kDigitW + kGap;

    g_digits[0].begin(x0, kDigitY, kDigitW, kDigitH, kThickness);
    g_digits[1].begin(x1, kDigitY, kDigitW, kDigitH, kThickness);
    g_digits[2].begin(x2, kDigitY, kDigitW, kDigitH, kThickness);
    g_digits[3].begin(x3, kDigitY, kDigitW, kDigitH, kThickness);
  }

  void setTime(const DateTime &now, uint32_t nowMs) {
    uint8_t hh = now.hour(), mm = now.minute();
    g_digits[0].setValue(hh / 10, nowMs);
    g_digits[1].setValue(hh % 10, nowMs);
    g_digits[2].setValue(mm / 10, nowMs);
    g_digits[3].setValue(mm % 10, nowMs);
  }

  void render(const DateTime &now, const WeatherData &weather, uint32_t nowMs) {
    GFXcanvas16 &canvas = Display::canvas();
    canvas.fillScreen(COLOR_BLACK);

    uint16_t lit = Palette::digitColor();
    uint16_t ghost = Palette::ghostColor();
    uint16_t text = Palette::textColor();

    drawDateRow(canvas, now, text);
    for (int i = 0; i < 4; i++) {
      g_digits[i].draw(canvas, lit, ghost, nowMs);
    }
    drawColon(canvas, lit);
    drawWeatherRow(canvas, weather, text);

    Display::present();
  }

  bool isAnimating(uint32_t nowMs) {
    for (int i = 0; i < 4; i++) {
      if (g_digits[i].isAnimating(nowMs)) return true;
    }
    return false;
  }
}
