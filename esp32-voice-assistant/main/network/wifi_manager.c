#include "wifi_manager.h"
#include "app_state.h"
#include "sdkconfig.h"

#include <string.h>
#include <stdlib.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        app_state_set_wifi_connected(false);
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        if (s_retry_count < CONFIG_VA_WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "reconectando (%d/%d)...", s_retry_count, CONFIG_VA_WIFI_MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "wifi indisponivel apos %d tentativas; vou tentar de novo em breve",
                      CONFIG_VA_WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_retry_count = 0;
        app_state_set_wifi_connected(true);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "conectado, sincronizando hora via SNTP");

        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&sntp_cfg);
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000));
        setenv("TZ", "<-03>3", 1); // horario de Brasilia fixo; ajuste p/ sua regiao
        tzset();
    }
}

esp_err_t wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, CONFIG_VA_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_VA_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "conectando a '%s'...", CONFIG_VA_WIFI_SSID);
    return ESP_OK;
}

bool wifi_manager_wait_connected(TickType_t timeout)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                            pdFALSE, pdFALSE, timeout);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_manager_is_connected(void)
{
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}
