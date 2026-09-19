#include "st7735.h"
#include "font5x7.h"
#include "config.h"

#include <string.h>
#include <ctype.h>
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "st7735";

// Comandos ST7735S (subset usado no init + escrita de frame).
#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_INVOFF  0x20
#define CMD_DISPON  0x29
#define CMD_CASET   0x2A
#define CMD_RASET   0x2B
#define CMD_RAMWR   0x2C
#define CMD_MADCTL  0x36
#define CMD_COLMOD  0x3A
#define CMD_FRMCTR1 0xB1
#define CMD_FRMCTR2 0xB2
#define CMD_FRMCTR3 0xB3
#define CMD_INVCTR  0xB4
#define CMD_PWCTR1  0xC0
#define CMD_PWCTR2  0xC1
#define CMD_PWCTR3  0xC2
#define CMD_PWCTR4  0xC3
#define CMD_PWCTR5  0xC4
#define CMD_VMCTR1  0xC5
#define CMD_GMCTRP1 0xE0
#define CMD_GMCTRN1 0xE1

#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_FREQ_HZ    5000

static spi_device_handle_t s_spi = NULL;
static st7735_rotation_t s_rotation = ST7735_ROTATION_0;

// Offset da RAM do controlador em relacao ao glass fisico. Para os
// paineis 0.96" 80x160 mais comuns (variante "mini160x80"), o glass
// esta deslocado ~26px em X e ~1px em Y dentro de uma RAM de 132x162 ou
// 128x160. Se a imagem aparecer cortada ou deslocada no seu painel,
// ajuste estes dois valores (variam por lote/fabricante).
#define LCD_COL_OFFSET 26
#define LCD_ROW_OFFSET 1

static void lcd_cmd(uint8_t cmd)
{
    gpio_set_level(VA_PIN_LCD_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    gpio_set_level(VA_PIN_LCD_DC, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static inline void lcd_data8(uint8_t d)
{
    lcd_data(&d, 1);
}

static void lcd_set_addr_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint16_t col_off = LCD_COL_OFFSET, row_off = LCD_ROW_OFFSET;
    // Nas rotacoes 90/270 os offsets de coluna/linha do controlador se
    // invertem porque MADCTL troca os eixos internamente.
    if (s_rotation == ST7735_ROTATION_90 || s_rotation == ST7735_ROTATION_270) {
        col_off = LCD_ROW_OFFSET;
        row_off = LCD_COL_OFFSET;
    }

    uint8_t caset[4] = {
        (uint8_t)((x0 + col_off) >> 8), (uint8_t)((x0 + col_off) & 0xFF),
        (uint8_t)((x1 + col_off) >> 8), (uint8_t)((x1 + col_off) & 0xFF),
    };
    lcd_cmd(CMD_CASET);
    lcd_data(caset, sizeof(caset));

    uint8_t raset[4] = {
        (uint8_t)((y0 + row_off) >> 8), (uint8_t)((y0 + row_off) & 0xFF),
        (uint8_t)((y1 + row_off) >> 8), (uint8_t)((y1 + row_off) & 0xFF),
    };
    lcd_cmd(CMD_RASET);
    lcd_data(raset, sizeof(raset));

    lcd_cmd(CMD_RAMWR);
}

// Preenche `count` pixels (ja com a janela de endereco ajustada) com uma
// unica cor, em blocos, para nao estourar a pilha nem o limite de DMA.
static void lcd_push_solid(uint16_t color, uint32_t count)
{
    // ST7735 espera cada pixel como byte-alto, byte-baixo no barramento.
    // O ESP32 e little-endian, entao montamos o buffer byte a byte em vez
    // de escrever um uint16_t direto (que sairia na ordem trocada).
    static uint8_t line_buf[80 * 2]; // 1 linha do panel (80px) por chunk
    uint8_t hi = color >> 8, lo = color & 0xFF;
    for (int i = 0; i < 80; i++) {
        line_buf[i * 2] = hi;
        line_buf[i * 2 + 1] = lo;
    }
    gpio_set_level(VA_PIN_LCD_DC, 1);
    while (count > 0) {
        uint32_t chunk = count > 80 ? 80 : count;
        spi_transaction_t t = {
            .length = chunk * 16,
            .tx_buffer = line_buf,
        };
        spi_device_polling_transmit(s_spi, &t);
        count -= chunk;
    }
}

esp_err_t st7735_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VA_PIN_LCD_DC) | (1ULL << VA_PIN_LCD_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);

    spi_bus_config_t buscfg = {
        .sclk_io_num = VA_PIN_LCD_SCK,
        .mosi_io_num = VA_PIN_LCD_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = VA_LCD_WIDTH * 80 * 2, // meia tela por transacao
    };
    ESP_ERROR_CHECK(spi_bus_initialize(VA_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = VA_LCD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = VA_PIN_LCD_CS,
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(VA_LCD_SPI_HOST, &devcfg, &s_spi));

    // Backlight via LEDC (PWM).
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);
    ledc_channel_config_t ch_cfg = {
        .gpio_num = VA_PIN_LCD_BLK,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_cfg);

    // Hardware reset.
    gpio_set_level(VA_PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(VA_PIN_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(VA_PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_cmd(CMD_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));
    lcd_cmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(255));

    lcd_cmd(CMD_FRMCTR1);
    lcd_data((uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    lcd_cmd(CMD_FRMCTR2);
    lcd_data((uint8_t[]){0x01, 0x2C, 0x2D}, 3);
    lcd_cmd(CMD_FRMCTR3);
    lcd_data((uint8_t[]){0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6);
    lcd_cmd(CMD_INVCTR);
    lcd_data8(0x07);

    lcd_cmd(CMD_PWCTR1);
    lcd_data((uint8_t[]){0xA2, 0x02, 0x84}, 3);
    lcd_cmd(CMD_PWCTR2);
    lcd_data8(0xC5);
    lcd_cmd(CMD_PWCTR3);
    lcd_data((uint8_t[]){0x0A, 0x00}, 2);
    lcd_cmd(CMD_PWCTR4);
    lcd_data((uint8_t[]){0x8A, 0x2A}, 2);
    lcd_cmd(CMD_PWCTR5);
    lcd_data((uint8_t[]){0x8A, 0xEE}, 2);
    lcd_cmd(CMD_VMCTR1);
    lcd_data8(0x0E);

    lcd_cmd(CMD_INVOFF);

    lcd_cmd(CMD_MADCTL);
    lcd_data8(0xC8); // orientacao base; refeito por st7735_set_rotation()

    lcd_cmd(CMD_COLMOD);
    lcd_data8(0x05); // 16 bits/pixel (RGB565)

    lcd_cmd(CMD_GMCTRP1);
    lcd_data((uint8_t[]){0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
                          0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}, 16);
    lcd_cmd(CMD_GMCTRN1);
    lcd_data((uint8_t[]){0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                          0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}, 16);

    lcd_cmd(CMD_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    st7735_set_rotation(ST7735_ROTATION_0);
    st7735_set_backlight(0);
    ESP_LOGI(TAG, "ST7735S inicializado (%dx%d logico)", st7735_width(), st7735_height());
    return ESP_OK;
}

void st7735_set_rotation(st7735_rotation_t rotation)
{
    s_rotation = rotation;
    uint8_t madctl;
    switch (rotation) {
        case ST7735_ROTATION_90:  madctl = 0xA8; break;
        case ST7735_ROTATION_180: madctl = 0x08; break;
        case ST7735_ROTATION_270: madctl = 0x68; break;
        default:                  madctl = 0xC8; break;
    }
    lcd_cmd(CMD_MADCTL);
    lcd_data8(madctl);
}

uint16_t st7735_width(void)
{
    return (s_rotation == ST7735_ROTATION_90 || s_rotation == ST7735_ROTATION_270)
               ? VA_LCD_HEIGHT : VA_LCD_WIDTH;
}

uint16_t st7735_height(void)
{
    return (s_rotation == ST7735_ROTATION_90 || s_rotation == ST7735_ROTATION_270)
               ? VA_LCD_WIDTH : VA_LCD_HEIGHT;
}

void st7735_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (max_duty * percent) / 100);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static inline bool clip_rect(int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    int16_t maxw = st7735_width(), maxh = st7735_height();
    if (*x >= maxw || *y >= maxh || *w <= 0 || *h <= 0) return false;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > maxw) *w = maxw - *x;
    if (*y + *h > maxh) *h = maxh - *y;
    return (*w > 0 && *h > 0);
}

void st7735_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (!clip_rect(&x, &y, &w, &h)) return;
    lcd_set_addr_window(x, y, x + w - 1, y + h - 1);
    lcd_push_solid(color, (uint32_t)w * (uint32_t)h);
}

void st7735_fill_screen(uint16_t color)
{
    st7735_fill_rect(0, 0, st7735_width(), st7735_height(), color);
}

void st7735_draw_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= st7735_width() || y >= st7735_height()) return;
    lcd_set_addr_window(x, y, x, y);
    uint8_t d[2] = { color >> 8, color & 0xFF };
    lcd_data(d, 2);
}

void st7735_draw_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    st7735_fill_rect(x, y, w, 1, color);
}

void st7735_draw_vline(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    st7735_fill_rect(x, y, 1, h, color);
}

void st7735_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    st7735_draw_hline(x, y, w, color);
    st7735_draw_hline(x, y + h - 1, w, color);
    st7735_draw_vline(x, y, h, color);
    st7735_draw_vline(x + w - 1, y, h, color);
}

// Bresenham classico para circulo (contorno de 1px).
void st7735_draw_circle(int16_t cx, int16_t cy, int16_t radius, uint16_t color)
{
    int16_t x = radius, y = 0, err = 0;
    while (x >= y) {
        st7735_draw_pixel(cx + x, cy + y, color);
        st7735_draw_pixel(cx + y, cy + x, color);
        st7735_draw_pixel(cx - y, cy + x, color);
        st7735_draw_pixel(cx - x, cy + y, color);
        st7735_draw_pixel(cx - x, cy - y, color);
        st7735_draw_pixel(cx - y, cy - x, color);
        st7735_draw_pixel(cx + y, cy - x, color);
        st7735_draw_pixel(cx + x, cy - y, color);
        y += 1;
        if (err <= 0) { err += 2 * y + 1; }
        if (err > 0)  { x -= 1; err -= 2 * x + 1; }
    }
}

void st7735_fill_circle(int16_t cx, int16_t cy, int16_t radius, uint16_t color)
{
    for (int16_t y = -radius; y <= radius; y++) {
        int16_t dx = (int16_t)__builtin_sqrtf((float)(radius * radius - y * y));
        st7735_draw_hline(cx - dx, cy + y, 2 * dx + 1, color);
    }
}

// --- Texto -------------------------------------------------------------

static const font5x7_glyph_t *lookup_glyph(char c)
{
    uint8_t code = (uint8_t)toupper((unsigned char)c);
    for (size_t i = 0; i < FONT5X7_TABLE_LEN; i++) {
        if (FONT5X7_TABLE[i].code == code) {
            return &FONT5X7_TABLE[i];
        }
    }
    return NULL; // desconhecido -> renderiza como espaco
}

static void draw_glyph(int16_t x, int16_t y, const font5x7_glyph_t *g,
                        uint16_t color, uint16_t bg_color, uint8_t scale)
{
    st7735_fill_rect(x, y, 5 * scale + scale, 7 * scale, bg_color);
    if (!g) return;
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g->col[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                st7735_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

int16_t st7735_draw_text(int16_t x, int16_t y, const char *text,
                          uint16_t color, uint16_t bg_color, uint8_t scale)
{
    if (scale == 0) scale = 1;
    int16_t cursor = x;
    for (const char *p = text; *p; p++) {
        const font5x7_glyph_t *g = (*p == ' ') ? NULL : lookup_glyph(*p);
        draw_glyph(cursor, y, g, color, bg_color, scale);
        cursor += (6 * scale); // 5 colunas de glifo + 1 coluna de espaco
    }
    return cursor - x;
}

int16_t st7735_text_width(const char *text, uint8_t scale)
{
    if (scale == 0) scale = 1;
    return (int16_t)(strlen(text) * 6 * scale);
}
