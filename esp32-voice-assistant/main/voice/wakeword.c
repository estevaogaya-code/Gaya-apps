#include "wakeword.h"

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "wakeword";

static QueueHandle_t s_trigger_queue;
static QueueHandle_t s_gpio_isr_queue;

static void IRAM_ATTR ptt_isr_handler(void *arg)
{
    uint32_t pin = (uint32_t)(uintptr_t)arg;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_gpio_isr_queue, &pin, &woken);
    if (woken) portYIELD_FROM_ISR();
}

static void ptt_debounce_task(void *arg)
{
    uint32_t pin;
    TickType_t last_press_tick = 0;
    while (1) {
        if (xQueueReceive(s_gpio_isr_queue, &pin, portMAX_DELAY)) {
            // Debounce simples: ignora bordas repetidas em menos de 50ms.
            TickType_t now = xTaskGetTickCount();
            if ((now - last_press_tick) < pdMS_TO_TICKS(50)) {
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(20)); // assentar o contato
            if (gpio_get_level((gpio_num_t)pin) != 0) {
                continue; // ja soltou (ruido) - ignora
            }
            last_press_tick = now;
            wakeword_event_t evt = { .source = WAKEWORD_SOURCE_PTT_BUTTON };
            xQueueSend(s_trigger_queue, &evt, 0);
            ESP_LOGI(TAG, "push-to-talk pressionado");
        }
    }
}

esp_err_t wakeword_init(void)
{
    s_trigger_queue = xQueueCreate(4, sizeof(wakeword_event_t));
    s_gpio_isr_queue = xQueueCreate(4, sizeof(uint32_t));

    gpio_num_t pin = (gpio_num_t)CONFIG_VA_PIN_PTT_BUTTON;
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // botao para GND
    };
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, ptt_isr_handler, (void *)(uintptr_t)pin);

    xTaskCreate(ptt_debounce_task, "ptt_debounce", 2048, NULL, 10, NULL);

#if VA_WAKENET_ENABLED
    // TODO(WakeNet): inicialize aqui o componente esp-sr:
    //   - esp_srmodel_init("model") para carregar os modelos da particao
    //     `model` (ver partitions.csv);
    //   - crie uma task dedicada que alimenta o AFE/WakeNet com frames de
    //     16kHz mono vindos de i2s_mic_read();
    //   - ao detectar a wake word (ex: "Hi ESP"/"Computer" - nao ha
    //     modelo oficial em portugues, ver README), publique um
    //     wakeword_event_t{ .source = WAKEWORD_SOURCE_WAKENET } na mesma
    //     s_trigger_queue usada pelo botao, via uma funcao auxiliar
    //     (exporte-a se precisar chamar de outro arquivo).
#endif

    ESP_LOGI(TAG, "push-to-talk pronto no GPIO%d", pin);
    return ESP_OK;
}

bool wakeword_wait_for_trigger(wakeword_event_t *out_event, TickType_t timeout)
{
    return xQueueReceive(s_trigger_queue, out_event, timeout) == pdTRUE;
}

bool wakeword_ptt_is_held(void)
{
    return gpio_get_level((gpio_num_t)CONFIG_VA_PIN_PTT_BUTTON) == 0;
}
