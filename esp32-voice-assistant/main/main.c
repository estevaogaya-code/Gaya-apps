// Assistente de voz automotivo - firmware inicial.
//
// Fluxo: wakeword_task fica bloqueada esperando um disparo (hoje, so o
// botao de push-to-talk). Ao disparar, grava enquanto o botao continua
// pressionado, tenta um comando local (commands.c - hoje sempre "nenhum"
// ate o MultiNet ser integrado) e, se nao achar, manda o audio para a
// API de IA configurada em Kconfig. A resposta e mostrada no display e
// falada via TTS (audio baixado do backend, ou um bipe se nao houver).
//
// display_task apenas observa app_state e redesenha a tela adequada -
// nenhuma logica de negocio mora la.
#include <string.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "app_state.h"
#include "st7735.h"
#include "ui_screens.h"
#include "i2s_mic.h"
#include "i2s_speaker.h"
#include "tts_player.h"
#include "wakeword.h"
#include "commands.h"
#include "wifi_manager.h"
#include "ai_client.h"

static const char *TAG = "main";

#define CAPTURE_MAX_SECONDS 6
#define CAPTURE_MAX_SAMPLES (VA_I2S_MIC_SAMPLE_RATE * CAPTURE_MAX_SECONDS)
#define CAPTURE_MIN_SAMPLES (VA_I2S_MIC_SAMPLE_RATE / 4) // 250ms - descarta toques acidentais
#define RESPONSE_DISPLAY_MS 6000

static const command_entry_t *find_command_entry(command_id_t id)
{
    for (size_t i = 0; i < CMD_COUNT - 1; i++) {
        if (COMMANDS_TABLE[i].id == id) {
            return &COMMANDS_TABLE[i];
        }
    }
    return NULL;
}

static void display_task(void *arg)
{
    (void)arg;
    EventGroupHandle_t events = app_state_event_group();
    app_state_snapshot_t snapshot;

    while (1) {
        app_state_get(&snapshot);
        ui_screens_render(&snapshot);

        // Enquanto ouvindo, redesenha rapido pra animar o pulso/waveform;
        // nos outros estados, uma vez por segundo basta (o relogio anda).
        TickType_t wait = (snapshot.id == APP_STATE_LISTENING)
                               ? pdMS_TO_TICKS(100)
                               : pdMS_TO_TICKS(1000);
        xEventGroupWaitBits(events, APP_STATE_CHANGED_BIT, pdTRUE, pdFALSE, wait);
    }
}

static void voice_task(void *arg)
{
    (void)arg;
    int16_t *capture_buf = heap_caps_malloc(CAPTURE_MAX_SAMPLES * sizeof(int16_t),
                                             MALLOC_CAP_SPIRAM);
    if (!capture_buf) {
        ESP_LOGE(TAG, "sem PSRAM suficiente pro buffer de captura - abortando voice_task");
        vTaskDelete(NULL);
        return;
    }
    int16_t read_chunk[512];

    while (1) {
        wakeword_event_t trigger;
        if (!wakeword_wait_for_trigger(&trigger, portMAX_DELAY)) {
            continue;
        }

        app_state_set_listening();
        ESP_LOGI(TAG, "gravando comando (fonte=%d)...", trigger.source);

        size_t total_samples = 0;
        while (wakeword_ptt_is_held() && total_samples < CAPTURE_MAX_SAMPLES) {
            size_t got = 0;
            i2s_mic_read(read_chunk, 512, &got, pdMS_TO_TICKS(200));
            if (got == 0) {
                continue;
            }
            app_state_set_mic_level(i2s_mic_calc_level(read_chunk, got));

            size_t to_copy = got;
            if (total_samples + to_copy > CAPTURE_MAX_SAMPLES) {
                to_copy = CAPTURE_MAX_SAMPLES - total_samples;
            }
            memcpy(capture_buf + total_samples, read_chunk, to_copy * sizeof(int16_t));
            total_samples += to_copy;
        }

        if (total_samples < CAPTURE_MIN_SAMPLES) {
            ESP_LOGW(TAG, "gravacao curta demais (%d amostras) - ignorando", (int)total_samples);
            app_state_set_idle();
            continue;
        }

        char response_text[256] = {0};
        char question_text[256] = {0};
        bool has_tts_url = false;
        char tts_url[256] = {0};

        command_id_t local_cmd = commands_recognize_stub(capture_buf, total_samples);
        if (local_cmd != CMD_NONE && commands_execute(local_cmd, response_text, sizeof(response_text))) {
            const command_entry_t *entry = find_command_entry(local_cmd);
            strncpy(question_text, entry ? entry->phrase : "(comando local)", sizeof(question_text) - 1);
        } else if (!wifi_manager_is_connected()) {
            strncpy(question_text, "(offline)", sizeof(question_text) - 1);
            strncpy(response_text, "Sem Wi-Fi e nenhum comando local reconhecido para este audio.",
                     sizeof(response_text) - 1);
        } else {
            ai_query_result_t result;
            esp_err_t err = ai_client_query_audio(capture_buf, total_samples,
                                                    VA_I2S_MIC_SAMPLE_RATE, &result);
            if (err == ESP_OK) {
                strncpy(question_text,
                         result.recognized_text[0] ? result.recognized_text : "(nao entendi)",
                         sizeof(question_text) - 1);
                strncpy(response_text, result.response_text, sizeof(response_text) - 1);
                has_tts_url = result.has_tts_audio_url;
                strncpy(tts_url, result.tts_audio_url, sizeof(tts_url) - 1);
            } else {
                ESP_LOGE(TAG, "ai_client_query_audio falhou: %s", esp_err_to_name(err));
                strncpy(question_text, "(erro)", sizeof(question_text) - 1);
                strncpy(response_text, "Nao consegui falar com a IA agora.", sizeof(response_text) - 1);
            }
        }

        app_state_set_responding(question_text, response_text);
        tts_player_play_url(has_tts_url ? tts_url : NULL);

        vTaskDelay(pdMS_TO_TICKS(RESPONSE_DISPLAY_MS));
        app_state_set_idle();
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    app_state_init();

    ESP_ERROR_CHECK(st7735_init());
    ui_screens_init();
    st7735_set_backlight(CONFIG_VA_DISPLAY_BACKLIGHT_DUTY_PERCENT);

    ESP_ERROR_CHECK(i2s_mic_init());
    ESP_ERROR_CHECK(i2s_speaker_init(VA_I2S_AMP_SAMPLE_RATE));

    ESP_ERROR_CHECK(wakeword_init());
    ESP_ERROR_CHECK(wifi_manager_init()); // conecta em background; nao bloqueia o boot

    xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);
    xTaskCreatePinnedToCore(voice_task, "voice_task", 8192, NULL, 6, NULL, 1);

    ESP_LOGI(TAG, "assistente de voz pronto");
}
