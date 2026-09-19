#include "ai_client.h"
#include "wav_format.h"
#include "sdkconfig.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "ai_client";

#define AI_RESP_BUF_MAX 4096

esp_err_t ai_client_query_audio(const int16_t *pcm, size_t sample_count,
                                 uint32_t sample_rate, ai_query_result_t *out_result)
{
    memset(out_result, 0, sizeof(*out_result));

    uint32_t data_size = (uint32_t)(sample_count * sizeof(int16_t));
    wav_header_t hdr = wav_header_make(sample_rate, /*channels=*/1, /*bits=*/16, data_size);
    size_t total_len = sizeof(hdr) + data_size;

    esp_http_client_config_t config = {
        .url = CONFIG_VA_AI_API_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CONFIG_VA_AI_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    if (strlen(CONFIG_VA_AI_API_KEY) > 0) {
        char auth_header[300];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", CONFIG_VA_AI_API_KEY);
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    esp_err_t err = esp_http_client_open(client, (int)total_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha ao abrir conexao: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    if (esp_http_client_write(client, (const char *)&hdr, sizeof(hdr)) < 0 ||
        esp_http_client_write(client, (const char *)pcm, data_size) < 0) {
        ESP_LOGE(TAG, "falha ao enviar audio");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    char *resp_buf = malloc(AI_RESP_BUF_MAX);
    if (!resp_buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    size_t resp_len = 0;
    (void)content_length; // so usado como dica; lemos ate EOF de qualquer forma
    while (resp_len + 256 < AI_RESP_BUF_MAX) {
        int n = esp_http_client_read(client, resp_buf + resp_len, 256);
        if (n <= 0) break;
        resp_len += n;
    }
    resp_buf[resp_len] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "API respondeu status %d: %s", status, resp_buf);
        free(resp_buf);
        return ESP_FAIL;
    }

    cJSON *resp_json = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!resp_json) {
        ESP_LOGE(TAG, "resposta nao e JSON valido");
        return ESP_FAIL;
    }

    const cJSON *recognized_item = cJSON_GetObjectItemCaseSensitive(resp_json, "recognized_text");
    if (cJSON_IsString(recognized_item) && recognized_item->valuestring) {
        strncpy(out_result->recognized_text, recognized_item->valuestring,
                sizeof(out_result->recognized_text) - 1);
    }

    const cJSON *text_item = cJSON_GetObjectItemCaseSensitive(resp_json, "response_text");
    if (cJSON_IsString(text_item) && text_item->valuestring) {
        strncpy(out_result->response_text, text_item->valuestring,
                sizeof(out_result->response_text) - 1);
    } else {
        strncpy(out_result->response_text, "A API nao retornou texto.",
                sizeof(out_result->response_text) - 1);
    }

    const cJSON *tts_item = cJSON_GetObjectItemCaseSensitive(resp_json, "tts_audio_url");
    if (cJSON_IsString(tts_item) && tts_item->valuestring) {
        strncpy(out_result->tts_audio_url, tts_item->valuestring,
                sizeof(out_result->tts_audio_url) - 1);
        out_result->has_tts_audio_url = true;
    }

    cJSON_Delete(resp_json);
    return ESP_OK;
}
