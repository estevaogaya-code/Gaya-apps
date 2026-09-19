// Renderizacao das 3 telas do assistente (ociosa / ouvindo / respondendo)
// sobre o driver st7735. Toda a logica de "o que desenhar" fica aqui; o
// "quando desenhar" fica a cargo da display_task em main.c, que observa
// app_state_event_group().
#pragma once

#include "app_state.h"

void ui_screens_init(void);

// Redesenha a tela correspondente ao snapshot atual. Chamada pela
// display_task sempre que o estado muda (ou periodicamente em IDLE, para
// o relogio seguir andando).
void ui_screens_render(const app_state_snapshot_t *state);
