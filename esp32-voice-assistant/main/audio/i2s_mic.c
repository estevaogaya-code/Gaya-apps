#include "i2s_mic.h"
#include "config.h"

#include <math.h>
#include <stdlib.h>
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "i2s_mic";
static i2s_chan_handle_t s_rx_chan = NULL;

esp_err_t i2s_mic_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(VA_I2S_MIC_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(VA_I2S_MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                          I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = VA_PIN_MIC_BCLK,
            .ws   = VA_PIN_MIC_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = VA_PIN_MIC_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    // INMP441 com L/R preso em GND transmite no slot esquerdo.
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    ESP_LOGI(TAG, "INMP441 pronto em I2S%d, %d Hz", VA_I2S_MIC_PORT, VA_I2S_MIC_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t i2s_mic_read(int16_t *out_buf, size_t max_samples, size_t *out_read, TickType_t timeout)
{
    // O INMP441 entrega amostras de 24 bits justificadas a esquerda dentro
    // de um slot de 32 bits (MSB primeiro). Lemos em um buffer de 32 bits
    // e reduzimos para 16 bits pegando os 16 bits mais significativos -
    // suficiente para deteccao de nivel/wake word, embora perca resolucao
    // fina; para gravacoes de alta fidelidade mantenha os 32 bits.
    static int32_t raw_buf[512];
    size_t to_read = max_samples > 512 ? 512 : max_samples;

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_rx_chan, raw_buf, to_read * sizeof(int32_t),
                                      &bytes_read, timeout);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        return err;
    }

    size_t samples_read = bytes_read / sizeof(int32_t);
    for (size_t i = 0; i < samples_read; i++) {
        out_buf[i] = (int16_t)(raw_buf[i] >> 16);
    }
    *out_read = samples_read;
    return ESP_OK;
}

float i2s_mic_calc_level(const int16_t *buf, size_t count)
{
    if (count == 0) return 0.0f;
    int32_t peak = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t v = abs((int)buf[i]);
        if (v > peak) peak = v;
    }
    float level = (float)peak / 32768.0f;
    return level > 1.0f ? 1.0f : level;
}
