// Maquina de estados central do assistente. Qualquer modulo (voz, rede,
// display) le/escreve o estado atual atraves destas funcoes; o acesso e
// protegido por mutex porque e tocado por tasks diferentes (voz, rede,
// display).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

typedef enum {
    APP_STATE_IDLE = 0,       // relogio (tela 1)
    APP_STATE_LISTENING,      // ouvindo comando (tela 2)
    APP_STATE_RESPONDING,     // exibindo pergunta + resposta (tela 3)
} app_state_id_t;

#define APP_STATE_MAX_TEXT 160

typedef struct {
    app_state_id_t id;
    char question[APP_STATE_MAX_TEXT]; // preenchido so em APP_STATE_RESPONDING
    char answer[APP_STATE_MAX_TEXT];
    bool wifi_connected;
    float mic_level;   // 0.0 .. 1.0, atualizado durante APP_STATE_LISTENING p/ waveform
} app_state_snapshot_t;

// Bit setado no event group toda vez que o estado muda; a task de display
// espera nele em vez de fazer polling.
#define APP_STATE_CHANGED_BIT (1 << 0)

void app_state_init(void);

EventGroupHandle_t app_state_event_group(void);

void app_state_set_idle(void);
void app_state_set_listening(void);
void app_state_set_responding(const char *question, const char *answer);
void app_state_set_wifi_connected(bool connected);
void app_state_set_mic_level(float level_0_to_1);

// Copia o snapshot atual (thread-safe) para `out`.
void app_state_get(app_state_snapshot_t *out);
