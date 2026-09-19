#include "commands.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

const command_entry_t COMMANDS_TABLE[CMD_COUNT - 1] = {
    { CMD_QUE_HORAS,      "que horas sao" },
    { CMD_QUE_DIA,        "que dia e hoje" },
    { CMD_TEMPERATURA,    "qual a temperatura" },
    { CMD_LIGAR_FAROL,    "ligar farol" },
    { CMD_DESLIGAR_FAROL, "desligar farol" },
    { CMD_ABRIR_VIDRO,    "abrir vidro" },
    { CMD_FECHAR_VIDRO,   "fechar vidro" },
    { CMD_LIGAR_AR,       "ligar ar condicionado" },
    { CMD_DESLIGAR_AR,    "desligar ar condicionado" },
    { CMD_VOLUME_MAIS,    "aumentar volume" },
    { CMD_VOLUME_MENOS,   "diminuir volume" },
    { CMD_REPETIR,        "repetir" },
    { CMD_CANCELAR,       "cancelar" },
};

static void handle_que_horas(char *out, size_t len)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if ((tm_now.tm_year + 1900) <= 2020) {
        snprintf(out, len, "Ainda nao sincronizei a hora pela rede.");
    } else {
        snprintf(out, len, "Agora sao %02d:%02d.", tm_now.tm_hour, tm_now.tm_min);
    }
}

static void handle_que_dia(char *out, size_t len)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if ((tm_now.tm_year + 1900) <= 2020) {
        snprintf(out, len, "Ainda nao sincronizei a data pela rede.");
    } else {
        snprintf(out, len, "Hoje e dia %02d/%02d/%04d.",
                 tm_now.tm_mday, tm_now.tm_mon + 1, tm_now.tm_year + 1900);
    }
}

static void handle_temperatura(char *out, size_t len)
{
    // Ver ui_screens.c: nao ha sensor de temperatura no BOM atual.
    snprintf(out, len, "Nao tenho um sensor de temperatura instalado ainda.");
}

// Funcoes do carro (farol, vidro, ar-condicionado) dependem de rele/CAN
// que nao fazem parte do BOM descrito. Deixamos o comando reconhecido
// (para nao cair no fallback de IA para algo que deveria ser instantaneo)
// mas o handler avisa honestamente que o acionamento fisico ainda nao
// existe, em vez de fingir sucesso.
static void handle_not_wired(char *out, size_t len, const char *acao)
{
    snprintf(out, len, "Comando '%s' reconhecido, mas o acionamento fisico ainda nao foi ligado no firmware.", acao);
}

bool commands_execute(command_id_t id, char *response_out, size_t response_out_len)
{
    switch (id) {
        case CMD_QUE_HORAS:      handle_que_horas(response_out, response_out_len); return true;
        case CMD_QUE_DIA:        handle_que_dia(response_out, response_out_len); return true;
        case CMD_TEMPERATURA:    handle_temperatura(response_out, response_out_len); return true;
        case CMD_LIGAR_FAROL:    handle_not_wired(response_out, response_out_len, "ligar farol"); return true;
        case CMD_DESLIGAR_FAROL: handle_not_wired(response_out, response_out_len, "desligar farol"); return true;
        case CMD_ABRIR_VIDRO:    handle_not_wired(response_out, response_out_len, "abrir vidro"); return true;
        case CMD_FECHAR_VIDRO:   handle_not_wired(response_out, response_out_len, "fechar vidro"); return true;
        case CMD_LIGAR_AR:       handle_not_wired(response_out, response_out_len, "ligar ar condicionado"); return true;
        case CMD_DESLIGAR_AR:    handle_not_wired(response_out, response_out_len, "desligar ar condicionado"); return true;
        case CMD_VOLUME_MAIS:    handle_not_wired(response_out, response_out_len, "aumentar volume"); return true;
        case CMD_VOLUME_MENOS:   handle_not_wired(response_out, response_out_len, "diminuir volume"); return true;
        case CMD_REPETIR:        snprintf(response_out, response_out_len, "TODO: repetir a ultima resposta."); return true;
        case CMD_CANCELAR:       snprintf(response_out, response_out_len, "Cancelado."); return true;
        case CMD_NONE:
        default:
            return false;
    }
}

command_id_t commands_recognize_stub(const int16_t *pcm, size_t sample_count)
{
    (void)pcm;
    (void)sample_count;
    // Sem MultiNet ligado, nunca reconhecemos localmente - ver TODO no
    // header. Isso e intencional: preferimos cair no fallback online a
    // fingir uma classificacao que nao existe.
    return CMD_NONE;
}
