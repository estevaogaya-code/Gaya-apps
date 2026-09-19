// "TTS local": na pratica, o texto ja vem sintetizado pelo backend (a
// mesma chamada que devolve response_text pode devolver tts_audio_url -
// ver network/ai_client.h). Este modulo baixa esse WAV (PCM 16-bit,
// mono ou estereo, qualquer sample rate) e toca no MAX98357A via
// i2s_speaker. Sintese 100% on-device (offline) fica fora do escopo
// deste firmware inicial: engines leves em portugues para ESP32 ainda
// nao sao um caminho maduro - ver README.
#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Toca o WAV apontado por `url`. Bloqueia ate o audio terminar. Se `url`
// for NULL/vazio, toca um bipe curto de confirmacao no lugar (feedback
// audivel minimo mesmo sem TTS disponivel).
esp_err_t tts_player_play_url(const char *url);
