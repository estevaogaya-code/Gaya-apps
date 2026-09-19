// Tabela de comandos locais (offline). Pensada para ser o alvo de
// reconhecimento do MultiNet quando essa peca for integrada (ver
// commands_recognize_stub) - por enquanto tambem pode ser usada por um
// parser de texto simples vindo da propria resposta da IA, se quiser
// interceptar comandos triviais antes de gastar uma chamada de API.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CMD_NONE = 0,
    CMD_QUE_HORAS,
    CMD_QUE_DIA,
    CMD_TEMPERATURA,
    CMD_LIGAR_FAROL,
    CMD_DESLIGAR_FAROL,
    CMD_ABRIR_VIDRO,
    CMD_FECHAR_VIDRO,
    CMD_LIGAR_AR,
    CMD_DESLIGAR_AR,
    CMD_VOLUME_MAIS,
    CMD_VOLUME_MENOS,
    CMD_REPETIR,
    CMD_CANCELAR,
    CMD_COUNT,
} command_id_t;

typedef struct {
    command_id_t id;
    // Frase de referencia em portugues - documenta a intencao e serve de
    // rotulo de treino quando o MultiNet (ou outro classificador local)
    // for conectado aqui.
    const char *phrase;
} command_entry_t;

extern const command_entry_t COMMANDS_TABLE[CMD_COUNT - 1];

// Executa a acao local do comando e escreve uma resposta textual em
// `response_out` (para mostrar no display e mandar pro TTS). Retorna
// false se o comando nao tiver handler local (nao deveria acontecer para
// ids validos != CMD_NONE).
bool commands_execute(command_id_t id, char *response_out, size_t response_out_len);

// TODO(MultiNet): esta funcao e o ponto de integracao do reconhecimento
// offline de comandos. Hoje ela sempre retorna CMD_NONE (nenhum comando
// local reconhecido), o que faz o fluxo principal cair no fallback
// online para qualquer fala capturada. Para ativar o reconhecimento
// local:
//   1. Adicione o componente esp-sr (`idf.py add-dependency
//      "espressif/esp-sr"`) e habilite MultiNet no menuconfig do
//      componente, escolhendo um modelo (ingles/chines - nao ha MultiNet
//      oficial em portugues, ver README).
//   2. Alimente o `pcm`/`sample_count` (16kHz mono) no pipeline do
//      MultiNet a cada frame e leia o indice de comando retornado.
//   3. Mapeie esse indice para um command_id_t (ex: por posicao na
//      lista de comandos configurada no componente) e retorne aqui.
command_id_t commands_recognize_stub(const int16_t *pcm, size_t sample_count);
