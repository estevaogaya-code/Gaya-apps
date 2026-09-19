// Cliente HTTP(S) para o fallback online: grava o audio localmente
// (main.c, via i2s_mic) e manda o WAV inteiro para uma API que faz
// STT+IA no servidor, recebendo de volta o texto reconhecido e a
// resposta (e opcionalmente um audio ja sintetizado).
//
// Isso bate com o pedido original ("grava o audio, envia via Wi-Fi...
// recebe resposta em texto"): o ESP32 nao faz nenhum reconhecimento de
// fala para o caminho online, so grava e envia; toda a inteligencia (STT
// + chamada ao modelo de IA) fica no backend apontado por
// CONFIG_VA_AI_API_URL. Modelos de linguagem como o Claude nao aceitam
// audio bruto diretamente, entao esse backend intermediario e quem faz a
// transcricao antes de chamar a IA.
//
// Contrato esperado do endpoint:
//   Request  (POST, Content-Type: audio/wav): bytes do arquivo WAV (PCM
//             16-bit mono, sample rate = VA_I2S_MIC_SAMPLE_RATE)
//   Response (200, JSON): {
//     "recognized_text": "<transcricao, para mostrar em 'VOCE:'>",
//     "response_text":   "<resposta da IA, para mostrar e falar>",
//     "tts_audio_url":   "<opcional, WAV 16-bit PCM mono ja sintetizado>"
//   }
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    char recognized_text[256];
    char response_text[256];
    char tts_audio_url[256];
    bool has_tts_audio_url;
} ai_query_result_t;

// Envia `sample_count` amostras mono de 16 bits (capturadas em
// `sample_rate` Hz) como um WAV e aguarda a resposta. Bloqueia ate a
// resposta chegar ou o timeout configurado (CONFIG_VA_AI_HTTP_TIMEOUT_MS).
esp_err_t ai_client_query_audio(const int16_t *pcm, size_t sample_count,
                                 uint32_t sample_rate, ai_query_result_t *out_result);
