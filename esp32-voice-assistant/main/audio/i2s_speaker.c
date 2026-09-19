#include "i2s_speaker.h"
#include "config.h"

#include <string.h>
#include <inttypes.h>
#include "driver/i2s_std.h"
#include "esp_log.h"

static const char *TAG = "i2s_speaker";
static i2s_chan_handle_t s_tx_chan = NULL;
static uint32_t s_sample_rate = 0;

esp_err_t i2s_speaker_init(uint32_t sample_rate_hz)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(VA_I2S_AMP_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz),
        // MONO duplica a mesma amostra nos slots L/R; com o SD do
        // MAX98357A preso em VCC (media L+R), o resultado e o audio mono
        // esperado sem atenuacao de 6dB por cancelamento de fase.
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                          I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = VA_PIN_AMP_BCLK,
            .ws   = VA_PIN_AMP_LRC,
            .dout = VA_PIN_AMP_DIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
    s_sample_rate = sample_rate_hz;

    ESP_LOGI(TAG, "MAX98357A pronto em I2S%d, %" PRIu32 " Hz", VA_I2S_AMP_PORT, sample_rate_hz);
    return ESP_OK;
}

esp_err_t i2s_speaker_set_sample_rate(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == s_sample_rate) {
        return ESP_OK;
    }
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    esp_err_t err = i2s_channel_disable(s_tx_chan);
    if (err != ESP_OK) return err;
    err = i2s_channel_reconfig_std_clock(s_tx_chan, &clk_cfg);
    if (err != ESP_OK) return err;
    err = i2s_channel_enable(s_tx_chan);
    if (err == ESP_OK) {
        s_sample_rate = sample_rate_hz;
    }
    return err;
}

esp_err_t i2s_speaker_write(const int16_t *pcm, size_t samples, TickType_t timeout)
{
    size_t bytes_written = 0;
    return i2s_channel_write(s_tx_chan, pcm, samples * sizeof(int16_t), &bytes_written, timeout);
}

esp_err_t i2s_speaker_write_silence_ms(uint32_t ms)
{
    size_t samples = (s_sample_rate * ms) / 1000;
    static const int16_t silence[128] = {0};
    size_t remaining = samples;
    while (remaining > 0) {
        size_t chunk = remaining > 128 ? 128 : remaining;
        esp_err_t err = i2s_speaker_write(silence, chunk, pdMS_TO_TICKS(200));
        if (err != ESP_OK) return err;
        remaining -= chunk;
    }
    return ESP_OK;
}
