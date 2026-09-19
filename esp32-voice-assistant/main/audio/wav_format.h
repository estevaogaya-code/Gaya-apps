// Cabecalho WAV canonico (44 bytes, PCM), compartilhado entre quem
// grava (network/ai_client.c, ao empacotar o audio capturado) e quem
// reproduz (audio/tts_player.c, ao interpretar o audio de TTS).
#pragma once

#include <stdint.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    char riff_id[4];
    uint32_t riff_size;
    char wave_id[4];
    char fmt_id[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_id[4];
    uint32_t data_size;
} wav_header_t;

static inline wav_header_t wav_header_make(uint32_t sample_rate, uint16_t num_channels,
                                            uint16_t bits_per_sample, uint32_t data_size)
{
    wav_header_t h;
    memcpy(h.riff_id, "RIFF", 4);
    h.riff_size = 36 + data_size;
    memcpy(h.wave_id, "WAVE", 4);
    memcpy(h.fmt_id, "fmt ", 4);
    h.fmt_size = 16;
    h.audio_format = 1; // PCM
    h.num_channels = num_channels;
    h.sample_rate = sample_rate;
    h.bits_per_sample = bits_per_sample;
    h.block_align = num_channels * (bits_per_sample / 8);
    h.byte_rate = sample_rate * h.block_align;
    memcpy(h.data_id, "data", 4);
    h.data_size = data_size;
    return h;
}
