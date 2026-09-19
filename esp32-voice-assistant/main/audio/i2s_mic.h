// Driver do microfone INMP441 (I2S digital, somente RX) usando a API
// i2s_std do ESP-IDF v5.x, em uma instancia de periferico I2S dedicada
// (I2S_NUM_0), separada do amplificador (que usa I2S_NUM_1 em TX).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t i2s_mic_init(void);

// Le ate `max_samples` amostras de 16 bits (ja convertidas do formato
// nativo do INMP441 - ver i2s_mic.c) para `out_buf`. `out_read` recebe o
// numero de amostras efetivamente lidas. Bloqueia ate `timeout` ticks.
esp_err_t i2s_mic_read(int16_t *out_buf, size_t max_samples,
                        size_t *out_read, TickType_t timeout);

// Utilitario: calcula um nivel de amplitude normalizado (0.0..1.0, estilo
// pico/RMS simplificado) sobre um buffer de amostras ja lido. Usado para
// animar a waveform na tela "ouvindo".
float i2s_mic_calc_level(const int16_t *buf, size_t count);
