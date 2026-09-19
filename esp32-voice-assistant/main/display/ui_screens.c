#include "ui_screens.h"
#include "st7735.h"
#include "config.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "esp_timer.h"

// Sem sensor de temperatura no BOM atual (ver README). Deixado como um
// ponto de extensao unico: troque este stub por leitura real (I2C/1-Wire)
// quando o sensor for adicionado.
static float read_cabin_temperature_c(void)
{
    return NAN;
}

static void draw_mic_icon(int16_t cx, int16_t cy, uint16_t color)
{
    // Corpo do microfone: capsula (retangulo com topo arredondado por um
    // circulo) + haste + base.
    st7735_fill_rect(cx - 4, cy - 10, 8, 12, color);
    st7735_fill_circle(cx, cy - 10, 4, color);
    st7735_fill_circle(cx, cy - 4, 4, color); // fecha a base da capsula
    st7735_draw_vline(cx, cy + 6, 4, color);
    st7735_draw_hline(cx - 4, cy + 10, 8, color);
    // "garfo" de suporte, um arco simplificado como dois traços laterais
    st7735_draw_vline(cx - 6, cy - 2, 8, color);
    st7735_draw_vline(cx + 6, cy - 2, 8, color);
}

void ui_screens_init(void)
{
    st7735_fill_screen(VA_COLOR_BLACK);
}

static void render_idle(const app_state_snapshot_t *state)
{
    st7735_fill_screen(VA_COLOR_BLACK);
    int16_t w = st7735_width();

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    bool time_valid = (tm_now.tm_year + 1900) > 2020; // sem NTP, epoch = 1970

    char date_buf[16];
    char time_buf[8];
    if (time_valid) {
        snprintf(date_buf, sizeof(date_buf), "%02d/%02d/%04d",
                  tm_now.tm_mday, tm_now.tm_mon + 1, tm_now.tm_year + 1900);
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    } else {
        snprintf(date_buf, sizeof(date_buf), "SEM HORA");
        snprintf(time_buf, sizeof(time_buf), "--:--");
    }

    int16_t dw = st7735_text_width(date_buf, 1);
    st7735_draw_text((w - dw) / 2, 12, date_buf, VA_COLOR_CYAN_DIM, VA_COLOR_BLACK, 1);

    int16_t tw = st7735_text_width(time_buf, 2);
    st7735_draw_text((w - tw) / 2, 34, time_buf, VA_COLOR_CYAN, VA_COLOR_BLACK, 2);

    float temp_c = read_cabin_temperature_c();
    char temp_buf[16];
    if (isnan(temp_c)) {
        snprintf(temp_buf, sizeof(temp_buf), "--\260C"); // \260 octal = 0xB0 = '°'
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "%d\260C", (int)lroundf(temp_c));
    }
    int16_t tempw = st7735_text_width(temp_buf, 2);
    st7735_draw_text((w - tempw) / 2, 70, temp_buf, VA_COLOR_CYAN_DIM, VA_COLOR_BLACK, 2);

    // Icone de mic apagado (ocioso) perto da base, e um pequeno indicador
    // de wifi no canto para diagnostico em bancada.
    draw_mic_icon(w / 2, 128, VA_COLOR_CYAN_DIM);
    st7735_fill_circle(w - 8, 8, 3, state->wifi_connected ? VA_COLOR_CYAN : VA_COLOR_RED);
}

static void render_listening(const app_state_snapshot_t *state)
{
    st7735_fill_screen(VA_COLOR_BLACK);
    int16_t w = st7735_width();

    const char *label = "OUVINDO...";
    int16_t lw = st7735_text_width(label, 1);
    st7735_draw_text((w - lw) / 2, 14, label, VA_COLOR_CYAN, VA_COLOR_BLACK, 1);

    // Circulo pulsante ao redor do icone de mic: raio oscila com o tempo
    // (uma respiracao continua) e cresce um pouco com o nivel do mic.
    int64_t t_ms = esp_timer_get_time() / 1000;
    float pulse = (sinf((float)t_ms / 300.0f) + 1.0f) / 2.0f; // 0..1
    int16_t base_r = 22;
    int16_t r = base_r + (int16_t)(pulse * 6.0f) + (int16_t)(state->mic_level * 10.0f);
    int16_t cx = w / 2, cy = 66;

    st7735_draw_circle(cx, cy, r, VA_COLOR_CYAN_DIM);
    st7735_draw_circle(cx, cy, r - 3, VA_COLOR_CYAN_DIM);
    draw_mic_icon(cx, cy, VA_COLOR_CYAN);

    // Barras de waveform: placeholder simples derivado do nivel de audio
    // atual (um escalar). Uma versao futura pode plotar o buffer de
    // amostras recentes por barra para uma forma de onda "de verdade".
    const int bars = 9;
    int16_t bar_w = 4, gap = 3;
    int16_t total_w = bars * bar_w + (bars - 1) * gap;
    int16_t bx = (w - total_w) / 2;
    int16_t by_base = 130;
    int16_t max_h = 24;
    for (int i = 0; i < bars; i++) {
        float phase = (float)t_ms / 90.0f + i * 0.9f;
        float jitter = (sinf(phase) + 1.0f) / 2.0f;
        float amp = 0.15f + 0.85f * state->mic_level * jitter;
        int16_t h = (int16_t)(amp * max_h);
        if (h < 2) h = 2;
        st7735_fill_rect(bx + i * (bar_w + gap), by_base - h, bar_w, h, VA_COLOR_CYAN);
    }
    st7735_draw_hline(bx - 4, by_base + 2, total_w + 8, VA_COLOR_CYAN_DIM);
}

// Quebra `text` em linhas de ate `max_chars` caracteres (sem hifenizacao,
// quebra no ultimo espaco antes do limite quando possivel) e desenha ate
// `max_lines`. Retorna o numero de linhas efetivamente desenhadas.
static int draw_wrapped(int16_t x, int16_t y, int16_t line_height,
                         const char *text, int max_chars, int max_lines,
                         uint16_t color)
{
    int line = 0;
    const char *p = text;
    while (*p && line < max_lines) {
        int len = (int)strlen(p);
        int take = len < max_chars ? len : max_chars;
        if (take == max_chars && len > max_chars) {
            // procura o ultimo espaco dentro do trecho para nao cortar palavra
            int cut = take;
            while (cut > 0 && p[cut] != ' ') cut--;
            if (cut > 0) take = cut;
        }
        char line_buf[40];
        int copy_len = take < (int)sizeof(line_buf) - 1 ? take : (int)sizeof(line_buf) - 1;
        memcpy(line_buf, p, copy_len);
        line_buf[copy_len] = '\0';

        st7735_draw_text(x, y + line * line_height, line_buf, color, VA_COLOR_BLACK, 1);

        p += take;
        while (*p == ' ') p++;
        line++;
    }
    return line;
}

static void render_responding(const app_state_snapshot_t *state)
{
    st7735_fill_screen(VA_COLOR_BLACK);
    int16_t w = st7735_width();
    const int max_chars = 13; // ~80px / (6px por char em scale 1)

    st7735_draw_text(4, 4, "VOCE:", VA_COLOR_CYAN_DIM, VA_COLOR_BLACK, 1);
    int lines = draw_wrapped(4, 16, 10, state->question, max_chars, 4, VA_COLOR_WHITE);

    int16_t answer_y = 16 + lines * 10 + 10;
    st7735_draw_hline(4, answer_y - 4, w - 8, VA_COLOR_CYAN_DIM);
    st7735_draw_text(4, answer_y, "GAYA:", VA_COLOR_CYAN, VA_COLOR_BLACK, 1);
    draw_wrapped(4, answer_y + 12, 10, state->answer, max_chars,
                 (st7735_height() - (answer_y + 12)) / 10, VA_COLOR_CYAN);
}

void ui_screens_render(const app_state_snapshot_t *state)
{
    switch (state->id) {
        case APP_STATE_LISTENING:
            render_listening(state);
            break;
        case APP_STATE_RESPONDING:
            render_responding(state);
            break;
        case APP_STATE_IDLE:
        default:
            render_idle(state);
            break;
    }
}
