// Conexao Wi-Fi (modo estacao) ao tethering do celular + sincronizacao
// de hora via SNTP assim que ha IP. O estado de conexao e refletido em
// app_state (icone de wifi na tela ociosa).
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t wifi_manager_init(void);

// Bloqueia ate a primeira conexao (ou timeout). Util antes de tentar a
// primeira chamada de rede.
bool wifi_manager_wait_connected(TickType_t timeout);

bool wifi_manager_is_connected(void);
