#include "tts_player.h"
#include "i2s_speaker.h"
#include "wav_format.h"
#include "config.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "tts_player";

// Le um WAV canonico (44 bytes: RIFF/WAVE/fmt /data, sem chunks extras
// como LIST/INFO). Se o seu backend de TTS gerar WAV com chunks
// adicionais antes de "data", este parser simples vai falhar - ajuste
// aqui se for o seu caso.

static esp_err_t play_beep(void)
{
    ESP_LOGI(TAG, "sem tts_audio_url - tocando bipe de confirmacao");
    i2s_speaker_set_sample_rate(VA_I2S_AMP_SAMPLE_RATE);

    const int tone_hz = 880;
    const int duration_ms = 120;
    int samples = (VA_I2S_AMP_SAMPLE_RATE * duration_ms) / 1000;
    int16_t *tone = malloc(samples * sizeof(int16_t));
    if (!tone) return ESP_ERR_NO_MEM;

    for (int rep = 0; rep < 2; rep++) {
        for (int i = 0; i < samples; i++) {
            float t = (float)i / VA_I2S_AMP_SAMPLE_RATE;
            tone[i] = (int16_t)(6000.0f * sinf(2.0f * (float)M_PI * tone_hz * t));
        }
        i2s_speaker_write(tone, samples, pdMS_TO_TICKS(1000));
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    free(tone);
    return ESP_OK;
}

esp_err_t tts_player_play_url(const char *url)
{
    if (!url || url[0] == '\0') {
        return play_beep();
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao abrir stream de TTS: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);

    wav_header_t hdr;
    int got = 0;
    while (got < (int)sizeof(hdr)) {
        int n = esp_http_client_read(client, ((char *)&hdr) + got, sizeof(hdr) - got);
        if (n <= 0) break;
        got += n;
    }
    if (got < (int)sizeof(hdr) || memcmp(hdr.riff_id, "RIFF", 4) != 0 ||
        memcmp(hdr.wave_id, "WAVE", 4) != 0 || memcmp(hdr.data_id, "data", 4) != 0) {
        ESP_LOGE(TAG, "audio de TTS nao parece um WAV canonico valido");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    if (hdr.bits_per_sample != 16) {
        ESP_LOGE(TAG, "so suporto WAV PCM 16-bit por enquanto (recebido %d bits)",
                  hdr.bits_per_sample);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NOT_SUPPORTED;
    }

    ESP_LOGI(TAG, "tocando TTS: %" PRIu32 " Hz, %d canal(is)", hdr.sample_rate, hdr.num_channels);
    i2s_speaker_set_sample_rate(hdr.sample_rate);

    const int chunk_samples = 512;
    int16_t raw_buf[chunk_samples];
    int16_t mono_buf[chunk_samples];

    while (1) {
        int n = esp_http_client_read(client, (char *)raw_buf, sizeof(raw_buf));
        if (n <= 0) break;
        int samples_in = n / (int)sizeof(int16_t);

        if (hdr.num_channels == 1) {
            i2s_speaker_write(raw_buf, samples_in, pdMS_TO_TICKS(2000));
        } else {
            // Downmix estereo -> mono fazendo a media de cada par L/R.
            int pairs = samples_in / 2;
            for (int i = 0; i < pairs; i++) {
                mono_buf[i] = (int16_t)(((int32_t)raw_buf[2 * i] + raw_buf[2 * i + 1]) / 2);
            }
            i2s_speaker_write(mono_buf, pairs, pdMS_TO_TICKS(2000));
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}
