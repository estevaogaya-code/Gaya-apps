#include "app_state.h"

#include <string.h>
#include "freertos/task.h"
#include "freertos/semphr.h"

static app_state_snapshot_t s_state;
static SemaphoreHandle_t s_mutex;
static EventGroupHandle_t s_event_group;

void app_state_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    s_event_group = xEventGroupCreate();
    memset(&s_state, 0, sizeof(s_state));
    s_state.id = APP_STATE_IDLE;
}

EventGroupHandle_t app_state_event_group(void)
{
    return s_event_group;
}

static void notify_changed(void)
{
    xEventGroupSetBits(s_event_group, APP_STATE_CHANGED_BIT);
}

void app_state_set_idle(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.id = APP_STATE_IDLE;
    s_state.question[0] = '\0';
    s_state.answer[0] = '\0';
    xSemaphoreGive(s_mutex);
    notify_changed();
}

void app_state_set_listening(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.id = APP_STATE_LISTENING;
    s_state.mic_level = 0.0f;
    xSemaphoreGive(s_mutex);
    notify_changed();
}

void app_state_set_responding(const char *question, const char *answer)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.id = APP_STATE_RESPONDING;
    strncpy(s_state.question, question ? question : "", APP_STATE_MAX_TEXT - 1);
    strncpy(s_state.answer, answer ? answer : "", APP_STATE_MAX_TEXT - 1);
    s_state.question[APP_STATE_MAX_TEXT - 1] = '\0';
    s_state.answer[APP_STATE_MAX_TEXT - 1] = '\0';
    xSemaphoreGive(s_mutex);
    notify_changed();
}

void app_state_set_wifi_connected(bool connected)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.wifi_connected = connected;
    xSemaphoreGive(s_mutex);
    notify_changed();
}

void app_state_set_mic_level(float level_0_to_1)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.mic_level = level_0_to_1;
    xSemaphoreGive(s_mutex);
    notify_changed();
}

void app_state_get(app_state_snapshot_t *out)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_mutex);
}
