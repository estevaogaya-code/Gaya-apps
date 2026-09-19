// Ponto de entrada para "algo pediu pra eu ouvir um comando". Hoje a
// unica fonte real e o botao fisico de push-to-talk (GPIO configurado em
// Kconfig), que e a alternativa recomendada ao wake word por voz
// enquanto se dirige (ver README - limitacao de wake word em portugues).
//
// A estrutura ja separa "fonte do disparo" de "quando parar de gravar"
// para que o WakeNet possa ser plugado sem reescrever o main.c: ambos os
// mecanismos escrevem na mesma fila de eventos.
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// Habilite depois de integrar o componente esp-sr (ver commands.h e
// README). Enquanto 0, todo o bloco WakeNet abaixo fica compilado fora,
// entao o firmware roda 100% no modo push-to-talk sem depender do
// componente.
#define VA_WAKENET_ENABLED 0

typedef enum {
    WAKEWORD_SOURCE_PTT_BUTTON = 0,
    WAKEWORD_SOURCE_WAKENET,
} wakeword_source_t;

typedef struct {
    wakeword_source_t source;
} wakeword_event_t;

esp_err_t wakeword_init(void);

// Bloqueia ate um disparo (botao pressionado, ou futuramente wake word
// detectada) ou o timeout expirar. Retorna true se `out_event` foi
// preenchido.
bool wakeword_wait_for_trigger(wakeword_event_t *out_event, TickType_t timeout);

// Para o fluxo de push-to-talk: verdadeiro enquanto o botao continua
// fisicamente pressionado. O main.c usa isso para saber quando parar de
// gravar (grava enquanto o usuario segura o botao).
bool wakeword_ptt_is_held(void);
