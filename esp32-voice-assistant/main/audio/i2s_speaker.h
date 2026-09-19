// Driver do amplificador MAX98357A (I2S digital, somente TX), em uma
// instancia de periferico I2S dedicada (I2S_NUM_1), separada do
// microfone (que usa I2S_NUM_0 em RX).
//
// GAIN flutuando = 9dB. SD preso em VCC = amplificador sempre habilitado
// (sem controle de shutdown por software nesta revisao de hardware).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

esp_err_t i2s_speaker_init(uint32_t sample_rate_hz);

// Reconfigura o clock de amostragem (ex: audio de TTS vindo em taxa
// diferente da default). Reaproveita o canal ja aberto.
esp_err_t i2s_speaker_set_sample_rate(uint32_t sample_rate_hz);

// Escreve `samples` amostras mono de 16 bits (duplicadas para os dois
// slots do MAX98357A). Bloqueia ate `timeout` ticks.
esp_err_t i2s_speaker_write(const int16_t *pcm, size_t samples, TickType_t timeout);

// Toca silencio - util para "flush" antes/depois de uma fala.
esp_err_t i2s_speaker_write_silence_ms(uint32_t ms);
