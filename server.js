/**
 * Dashboard Industrial — EtherNet/IP Direto
 * ControlLogix 1756-L62/B · slot 0 · IP 10.0.0.100
 * 82 tags | Alarmes condicionais | Log | Limites editáveis | Histórico 7 dias
 */

const express   = require('express');
const http      = require('http');
const WebSocket = require('ws');
const cors      = require('cors');
const path      = require('path');
const fs        = require('fs');
const { Controller, Tag } = require('st-ethernet-ip');

const CLP_CONFIG = { ip: '10.0.0.100', slot: 0, readInterval: 3000 };

// ─── Tags base ────────────────────────────────────────────────────────────────
// alarme_condicional: fn(dadosAtuais) => bool — se retornar false, alarme ignorado
const TAGS_BASE = [
  { id:'ea_501bm140_it_u01', label:'CORRENTE MOTOR MOINHO 501BM140',                        tagName:'EA_501BM140_IT_U01_AJUSTE[9]',          unit:'A',    min:0,   max:150  },
  { id:'ea_501bm140_tt_u01', label:'TEMP ESTATOR FASE A MOTOR 501BM140',                    tagName:'EA_501BM140_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_501bm140_tt_u02', label:'TEMP ESTATOR FASE B MOTOR 501BM140',                    tagName:'EA_501BM140_TT_U02_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_501bm140_tt_u03', label:'TEMP ESTATOR FASE C MOTOR 501BM140',                    tagName:'EA_501BM140_TT_U03_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_501bm140_tt_u04', label:'TEMP MANCAL MOTOR MOINHO 501BM140 LA',                  tagName:'EA_501BM140_TT_U04_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_501bm140_tt_u05', label:'TEMP MANCAL MOTOR MOINHO 501BM140 LOA',                 tagName:'EA_501BM140_TT_U05_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_501bm140_tt_u06', label:'TEMP MANCAL MOINHO 501BM140 LA',                        tagName:'EA_501BM140_TT_U06_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_501bm140_tt_u07', label:'TEMP MANCAL MOINHO 501BM140 LOA',                       tagName:'EA_501BM140_TT_U07_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_501bm140_tt_u09', label:'TEMP GASES SAIDA MOINHO 501BM140',                      tagName:'EA_501BM140_TT_U09_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_501bf040_tt_u01', label:'TEMP GASES ENTRADA FILTRO 501BF040',                    tagName:'EA_501BF040_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:80  },
  { id:'ea_501cp01_pt_u01',  label:'PRESSÃO AR COMPRIMIDO AREA 501',                        tagName:'EA_501CP01_PT_U01_AJUSTE[9]',           unit:'Bar',  min:0,   max:10   },
  { id:'ea_501md070_tt_u01', label:'TEMP ROLAMENTO INFERIOR SEP 501MD070',                  tagName:'EA_501MD070_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_501md070_tt_u02', label:'TEMP ROLAMENTO SUPERIOR SEP 501MD070',                  tagName:'EA_501MD070_TT_U02_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_501md070_tt_u03', label:'TEMP REDUTOR PRINCIPAL SEP 501MD070',                   tagName:'EA_501MD070_TT_U03_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  // BOMBAS LEVANTAMENTO 501 — alarme CONDICIONAL
  {
    id:'ea_501lq145a02_pt', label:'PRESSÃO BOMBA LEVANTAMENTO 501LQ145A02',
    tagName:'EA_501LQ145A02_PT_U01_AJUSTE[9]', unit:'Bar', min:0, max:250, alarme_lo:35,
    alarme_condicional: d => {
      const pot = d['ea_501bm140_jt_u01']?.valor; const cor = d['ea_rede_501be060_c']?.valor;
      return pot != null && pot < 100 && cor != null && cor > 10;
    },
    condicional_desc: 'Ativo: Potência 501BM140 < 100kW E Corrente 501BE060 > 10A (moinho prestes a partir)',
  },
  {
    id:'ea_501lq145a01_pt', label:'PRESSÃO BOMBA LEVANTAMENTO 501LQ145A01',
    tagName:'EA_501LQ145A01_PT_U01_AJUSTE[9]', unit:'Bar', min:0, max:250, alarme_lo:35,
    alarme_condicional: d => {
      const pot = d['ea_501bm140_jt_u01']?.valor; const cor = d['ea_rede_501be060_c']?.valor;
      return pot != null && pot < 100 && cor != null && cor > 10;
    },
    condicional_desc: 'Ativo: Potência 501BM140 < 100kW E Corrente 501BE060 > 10A (moinho prestes a partir)',
  },
  { id:'ea_501wp001_tt_u01', label:'TEMP AGUA ARREFECIMENTO MANCAIS 501',                   tagName:'EA_501WP001_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200  },
  { id:'ea_501bm140_wt_u01', label:'PESO CALHA RETORNO 501BM140',                           tagName:'EA_501BM140_WT_U01_AJUSTE[9]',          unit:'Kg',   min:0,   max:6000, alarme_hi:600 },
  { id:'ea_501tr001_tt_u01', label:'TEMP TRAFO 13,2/0,38KV FASE A',                         tagName:'EA_501TR001_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_501tr001_tt_u02', label:'TEMP TRAFO 13,2/0,38KV FASE B',                         tagName:'EA_501TR001_TT_U02_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_501tr001_tt_u03', label:'TEMP TRAFO 13,2/0,38KV FASE C',                         tagName:'EA_501TR001_TT_U03_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_502bm140_it_u01', label:'CORRENTE MOTOR MOINHO 502BM140',                        tagName:'EA_502BM140_IT_U01_AJUSTE[9]',          unit:'A',    min:0,   max:150  },
  { id:'ea_502bm140_tt_u01', label:'TEMP ESTATOR FASE A MOTOR 502BM140',                    tagName:'EA_502BM140_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_502bm140_tt_u02', label:'TEMP ESTATOR FASE B MOTOR 502BM140',                    tagName:'EA_502BM140_TT_U02_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_502bm140_tt_u03', label:'TEMP ESTATOR FASE C MOTOR 502BM140',                    tagName:'EA_502BM140_TT_U03_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_502bm140_tt_u04', label:'TEMP MANCAL MOTOR MOINHO 502BM140 LA',                  tagName:'EA_502BM140_TT_U04_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_502bm140_tt_u05', label:'TEMP MANCAL MOTOR MOINHO 502BM140 LOA',                 tagName:'EA_502BM140_TT_U05_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_502bm140_tt_u06', label:'TEMP MANCAL MOINHO 502BM140 LA',                        tagName:'EA_502BM140_TT_U06_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_502bm140_tt_u07', label:'TEMP MANCAL MOINHO 502BM140 LOA',                       tagName:'EA_502BM140_TT_U07_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_502bm140_tt_u09', label:'TEMP GASES SAIDA MOINHO 502BM140',                      tagName:'EA_502BM140_TT_U09_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  { id:'ea_502bf040_tt_u01', label:'TEMP GASES ENTRADA FILTRO 502BF040',                    tagName:'EA_502BF040_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:80  },
  { id:'ea_502md070_tt_u01', label:'TEMP ROLAMENTO INFERIOR SEP 502MD070',                  tagName:'EA_502MD070_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_502md070_tt_u02', label:'TEMP ROLAMENTO SUPERIOR SEP 502MD070',                  tagName:'EA_502MD070_TT_U02_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:73  },
  { id:'ea_502md070_tt_u03', label:'TEMP REDUTOR PRINCIPAL SEP 502MD070',                   tagName:'EA_502MD070_TT_U03_AJUSTE[9]',          unit:'°C',   min:0,   max:200, alarme_hi:110 },
  // BOMBAS LEVANTAMENTO 502 — alarme CONDICIONAL
  {
    id:'ea_502lq145a01_pt', label:'PRESSÃO BOMBA LEVANTAMENTO 502LQ145A01',
    tagName:'EA_502LQ145A01_PT_U01_AJUSTE[9]', unit:'Bar', min:0, max:250, alarme_lo:35,
    alarme_condicional: d => {
      const pot = d['ea_502bm140_jt_u01']?.valor; const cor = d['ea_rede_502be060_c']?.valor;
      return pot != null && pot < 100 && cor != null && cor > 10;
    },
    condicional_desc: 'Ativo: Potência 502BM140 < 100kW E Corrente 502BE060 > 10A (moinho prestes a partir)',
  },
  {
    id:'ea_502lq145a02_pt', label:'PRESSÃO BOMBA LEVANTAMENTO 502LQ145A02',
    tagName:'EA_502LQ145A02_PT_U01_AJUSTE[9]', unit:'Bar', min:0, max:250, alarme_lo:35,
    alarme_condicional: d => {
      const pot = d['ea_502bm140_jt_u01']?.valor; const cor = d['ea_rede_502be060_c']?.valor;
      return pot != null && pot < 100 && cor != null && cor > 10;
    },
    condicional_desc: 'Ativo: Potência 502BM140 < 100kW E Corrente 502BE060 > 10A (moinho prestes a partir)',
  },
  { id:'ea_502wp001_tt_u01', label:'TEMP AGUA ARREFECIMENTO MANCAIS 502',                   tagName:'EA_502WP001_TT_U01_AJUSTE[9]',          unit:'°C',   min:0,   max:200  },
  { id:'ea_502bm140_wt_u01', label:'PESO CALHA RETORNO 502BM140',                           tagName:'EA_502BM140_WT_U01_AJUSTE[9]',          unit:'Kg',   min:0,   max:6000, alarme_hi:600 },
  { id:'ea_502md070_it_u01', label:'CORRENTE MOTOR SEPARADOR 502MD070',                     tagName:'EA_502MD070_IT_U01_AJUSTE[9]',          unit:'A',    min:0,   max:200, alarme_hi:110 },
  { id:'ea_501md070_it_u01', label:'CORRENTE MOTOR SEPARADOR 501MD070',                     tagName:'EA_501MD070_IT_U01_AJUSTE[9]',          unit:'A',    min:0,   max:200, alarme_hi:110 },
  { id:'ea_501md070_st_u01', label:'VELOCIDADE SEPARADOR 501MD070',                         tagName:'EA_501MD070_ST_U01_AJUSTE[9]',          unit:'Hz',   min:0,   max:200  },
  { id:'ea_502md070_st_u01', label:'VELOCIDADE SEPARADOR 502MD070',                         tagName:'EA_502MD070_ST_U01_AJUSTE[9]',          unit:'Hz',   min:0,   max:200  },
  { id:'ea_501bm140_db_u01', label:'DECIBELIMETRO SAIDA MOINHO 501BM140',                   tagName:'EA_501BM140_DB_U01_AJUSTE[9]',          unit:'dB',   min:30,  max:120, alarme_hi:105 },
  { id:'ea_502bm140_db_u01', label:'DECIBELIMETRO SAIDA MOINHO 502BM140',                   tagName:'EA_502BM140_DB1_U01_AJUSTE[9]',          unit:'dB',   min:30,  max:120, alarme_hi:105 },
  { id:'ea_501bm140_jt_u01', label:'POTENCIA MOTOR MOINHO 501BM140',                        tagName:'EA_501BM140_JT_U01_AJUSTE[9]',          unit:'kW',   min:0,   max:1100 },
  { id:'ea_502bm140_jt_u01', label:'POTENCIA MOTOR MOINHO 502BM140',                        tagName:'EA_502BM140_JT_U01_AJUSTE[9]',          unit:'kW',   min:0,   max:1100 },
  { id:'ea_rede_501bc035_c', label:'CORRENTE 501BC035',    tagName:'EA_REDE_501BC035_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_501bc035_v', label:'VELOCIDADE 501BC035',  tagName:'EA_REDE_501BC035_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_501be060_c', label:'CORRENTE 501BE060',    tagName:'EA_REDE_501BE060_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:80,  alarme_hi:110,
    alarme_condicional: d => (d['ea_rede_501be060_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 501BE060 > 5A (elevador ligado)' },
  { id:'ea_rede_501be060_v', label:'VELOCIDADE 501BE060',  tagName:'EA_REDE_501BE060_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:75,  alarme_lo:60,
    alarme_condicional: d => (d['ea_rede_501be060_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 501BE060 > 5A (elevador ligado)' },
  { id:'ea_rede_501be090_c', label:'CORRENTE 501BE090',    tagName:'EA_REDE_501BE090_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:40,  alarme_hi:30,
    alarme_condicional: d => (d['ea_rede_501be090_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 501BE090 > 5A (elevador ligado)' },
  { id:'ea_rede_501be090_v', label:'VELOCIDADE 501BE090',  tagName:'EA_REDE_501BE090_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:75,  alarme_lo:60,
    alarme_condicional: d => (d['ea_rede_501be090_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 501BE090 > 5A (elevador ligado)' },
  { id:'ea_rede_501fn042_c', label:'CORRENTE 501FN042',    tagName:'EA_REDE_501FN042_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:250, alarme_hi:210 },
  { id:'ea_rede_501fn042_v', label:'VELOCIDADE 501FN042',  tagName:'EA_REDE_501FN042_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_501sc041_c', label:'CORRENTE 501SC041',    tagName:'EA_REDE_501SC041_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_501sc041_v', label:'VELOCIDADE 501SC041',  tagName:'EA_REDE_501SC041_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_501wf005_c', label:'CORRENTE 501WF005',    tagName:'EA_REDE_501WF005_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_501wf005_v', label:'VELOCIDADE 501WF005',  tagName:'EA_REDE_501WF005_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_501wf010_c', label:'CORRENTE 501WF010',    tagName:'EA_REDE_501WF010_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_501wf010_v', label:'VELOCIDADE 501WF010',  tagName:'EA_REDE_501WF010_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_502bc035_c', label:'CORRENTE 502BC035',    tagName:'EA_REDE_502BC035_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_502bc035_v', label:'VELOCIDADE 502BC035',  tagName:'EA_REDE_502BC035_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_502be060_c', label:'CORRENTE 502BE060',    tagName:'EA_REDE_502BE060_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:80,  alarme_hi:110,
    alarme_condicional: d => (d['ea_rede_502be060_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 502BE060 > 5A (elevador ligado)' },
  { id:'ea_rede_502be060_v', label:'VELOCIDADE 502BE060',  tagName:'EA_REDE_502BE060_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:75,  alarme_lo:60,
    alarme_condicional: d => (d['ea_rede_502be060_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 502BE060 > 5A (elevador ligado)' },
  { id:'ea_rede_502be090_c', label:'CORRENTE 502BE090',    tagName:'EA_REDE_502BE090_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:40,  alarme_hi:14,
    alarme_condicional: d => (d['ea_rede_502be090_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 502BE090 > 5A (elevador ligado)' },
  { id:'ea_rede_502be090_v', label:'VELOCIDADE 502BE090',  tagName:'EA_REDE_502BE090_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:75,  alarme_lo:60,
    alarme_condicional: d => (d['ea_rede_502be090_c']?.valor ?? 0) > 5,
    condicional_desc: 'Ativo somente se Corrente 502BE090 > 5A (elevador ligado)' },
  { id:'ea_rede_502fn042_c', label:'CORRENTE 502FN042',    tagName:'EA_REDE_502FN042_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:250, alarme_hi:210 },
  { id:'ea_rede_502fn042_v', label:'VELOCIDADE 502FN042',  tagName:'EA_REDE_502FN042_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_502sc041_c', label:'CORRENTE 502SC041',    tagName:'EA_REDE_502SC041_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_502sc041_v', label:'VELOCIDADE 502SC041',  tagName:'EA_REDE_502SC041_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_502wf005_c', label:'CORRENTE 502WF005',    tagName:'EA_REDE_502WF005_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_502wf005_v', label:'VELOCIDADE 502WF005',  tagName:'EA_REDE_502WF005_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'ea_rede_502wf010_c', label:'CORRENTE 502WF010',    tagName:'EA_REDE_502WF010_CORRENTE_AJUSTE[9]',   unit:'A',  min:0, max:20,  alarme_hi:13  },
  { id:'ea_rede_502wf010_v', label:'VELOCIDADE 502WF010',  tagName:'EA_REDE_502WF010_VELOCIDADE_AJUSTE[9]', unit:'Hz', min:0, max:60   },
  { id:'totalizador_501',    label:'MEDIA TH 501BM140',       tagName:'TOTALIZADOR_501[37]', unit:'t/h', min:0, max:50 },
  { id:'totalizador_502',    label:'MEDIA TH 502BM140',       tagName:'TOTALIZADOR_502[31]', unit:'t/h', min:0, max:50 },
  { id:'tot501_min_dia',     label:'MIN RODANDO DIA 501',     tagName:'TOTALIZADOR_501[33]', unit:'min', min:0, max:720 },
  { id:'tot501_ton_total',   label:'TOTAL ALIM. DIA 501',     tagName:'TOTALIZADOR_501[38]', unit:'t',   min:0, max:2000 },
  { id:'tot501_prod_t2',     label:'PROD 2º TURNO 501',       tagName:'TOTALIZADOR_501[85]', unit:'t',   min:0, max:1000 },
  { id:'tot501_th_t2',       label:'T/H 2º TURNO 501',        tagName:'TOTALIZADOR_501[86]', unit:'t/h', min:0, max:50 },
  { id:'tot501_min_t2',      label:'MIN RODANDO 2ºT 501',     tagName:'TOTALIZADOR_501[87]', unit:'min', min:0, max:720 },
  { id:'tot501_min_total_dia', label:'MIN RODANDO TOTAL DIA 501', tagName:'TOTALIZADOR_501[84]', unit:'min', min:0, max:1440 },
  { id:'tot502_min_dia',     label:'MIN RODANDO DIA 502',     tagName:'TOTALIZADOR_502[27]', unit:'min', min:0, max:720 },
  { id:'tot502_ton_total',   label:'TOTAL ALIM. DIA 502',     tagName:'TOTALIZADOR_502[38]', unit:'t',   min:0, max:2000 },
  { id:'tot502_ton_diurno',  label:'PROD DIURNA 502',          tagName:'TOTALIZADOR_502[88]', unit:'t',   min:0, max:2000 },
  { id:'tot502_prod_t2',     label:'PROD 2º TURNO 502',       tagName:'TOTALIZADOR_502[85]', unit:'t',   min:0, max:1000 },
  { id:'tot502_th_t2',       label:'T/H 2º TURNO 502',        tagName:'TOTALIZADOR_502[86]', unit:'t/h', min:0, max:50 },
  { id:'tot502_min_t2',      label:'MIN RODANDO 2ºT 502',     tagName:'TOTALIZADOR_502[87]', unit:'min', min:0, max:720 },
  { id:'tot502_min_total_dia', label:'MIN RODANDO TOTAL DIA 502', tagName:'TOTALIZADOR_502[84]', unit:'min', min:0, max:1440 },
  // kW/t calculado em tempo real pelo CLP
  { id:'tot501_kwht_clp',    label:'kW/t MOINHO 501 (CLP)',   tagName:'TOTALIZADOR_501[47]', unit:'kW/t',min:0, max:100 },
  { id:'tot502_kwht_clp',    label:'kW/t MOINHO 502 (CLP)',   tagName:'TOTALIZADOR_502[39]', unit:'kW/t',min:0, max:100 },

  // ── TEMPERATURAS ROLAMENTOS REDUTOR (1794-IR8 via AENT_106) ─────────────────
  // CLP já faz DIV(ChXData, 10, TTUxx_AJUSTE[0]) — valor em [0] já está em °C
  // Redutor Moinho 501BM140
  { id:'ea_501bm140_rd_ttu01', label:'TEMP ROL EIXO ENTRADA L.A. REDUTOR 501',   tagName:'EA_501BM140_RD_TTU01_AJUSTE[0]', unit:'°C', min:0, max:200 },
  { id:'ea_501bm140_rd_ttu02', label:'TEMP ROL EIXO ENTRADA L.O.A. REDUTOR 501', tagName:'EA_501BM140_RD_TTU02_AJUSTE[0]', unit:'°C', min:0, max:200 },
  { id:'ea_501bm140_rd_ttu03', label:'TEMP ROL EIXO SAIDA L.A. REDUTOR 501',     tagName:'EA_501BM140_RD_TTU03_AJUSTE[0]', unit:'°C', min:0, max:200 },
  { id:'ea_501bm140_rd_ttu04', label:'TEMP ROL EIXO SAIDA L.O.A. REDUTOR 501',   tagName:'EA_501BM140_RD_TTU04_AJUSTE[0]', unit:'°C', min:0, max:200 },
  // Redutor Moinho 502BM140
  { id:'ea_502bm140_rd_ttu01', label:'TEMP ROL EIXO ENTRADA L.A. REDUTOR 502',   tagName:'EA_502BM140_RD_TTU01_AJUSTE[0]', unit:'°C', min:0, max:200 },
  { id:'ea_502bm140_rd_ttu02', label:'TEMP ROL EIXO ENTRADA L.O.A. REDUTOR 502', tagName:'EA_502BM140_RD_TTU02_AJUSTE[0]', unit:'°C', min:0, max:200 },
  { id:'ea_502bm140_rd_ttu03', label:'TEMP ROL EIXO SAIDA L.A. REDUTOR 502',     tagName:'EA_502BM140_RD_TTU03_AJUSTE[0]', unit:'°C', min:0, max:200 },
  { id:'ea_502bm140_rd_ttu04', label:'TEMP ROL EIXO SAIDA L.O.A. REDUTOR 502',   tagName:'EA_502BM140_RD_TTU04_AJUSTE[0]', unit:'°C', min:0, max:200 },










  // ── COLETA DE PROCESSO — tags adicionais ────────────────────────────────────
  { id:'th_501wf005', label:'TON/H ATUAL 501WF005', tagName:'EA_501WF005_WT_U01_AJUSTE[2]', unit:'t/h', min:0, max:200 },
  { id:'th_501wf010', label:'TON/H ATUAL 501WF010', tagName:'EA_501WF010_WT_U01_AJUSTE[2]', unit:'t/h', min:0, max:200 },
  { id:'th_502wf005', label:'TON/H ATUAL 502WF005', tagName:'EA_502WF005_WT_U01_AJUSTE[2]', unit:'t/h', min:0, max:200 },
  { id:'th_502wf010', label:'TON/H ATUAL 502WF010', tagName:'EA_502WF010_WT_U01_AJUSTE[2]', unit:'t/h', min:0, max:200 },

  // ── Setpoint (SP) do PID de dosagem de cada balança — mesma unidade (t/h)
  // da vazão instantânea, porque no L5K o PID roda direto contra
  // EA_xxxWFxxx_WT_U01_AJUSTE[2] como PV: PID(PID_501WF005, EA_501WF005_WT_U01_AJUSTE[2], 6, ...).
  // Confirmado nas rotinas GR4_501WF005/GR4_501WF010/GR4_502WF005/GR4_502WF010.
  { id:'sp_501wf005', label:'SP DOSAGEM 501WF005', tagName:'PID_501WF005.SP', unit:'t/h', min:0, max:200 },
  { id:'sp_501wf010', label:'SP DOSAGEM 501WF010', tagName:'PID_501WF010.SP', unit:'t/h', min:0, max:200 },
  { id:'sp_502wf005', label:'SP DOSAGEM 502WF005', tagName:'PID_502WF005.SP', unit:'t/h', min:0, max:200 },
  { id:'sp_502wf010', label:'SP DOSAGEM 502WF010', tagName:'PID_502WF010.SP', unit:'t/h', min:0, max:200 },

  // ── Peso atual das moegas de alimentação (índice [9] do array — mesmo campo usado no ladder pra CHEIA/VAZIA) ──
  { id:'ea_501wf005_wt_u01', label:'PESO MOEGA 501WF005', tagName:'EA_501WF005_WT_U01_AJUSTE[9]', unit:'Kg', min:0, max:15000 },
  { id:'ea_501wf010_wt_u01', label:'PESO MOEGA 501WF010', tagName:'EA_501WF010_WT_U01_AJUSTE[9]', unit:'Kg', min:0, max:15000 },
  { id:'ea_502wf005_wt_u01', label:'PESO MOEGA 502WF005', tagName:'EA_502WF005_WT_U01_AJUSTE[9]', unit:'Kg', min:0, max:15000 },
  { id:'ea_502wf010_wt_u01', label:'PESO MOEGA 502WF010', tagName:'EA_502WF010_WT_U01_AJUSTE[9]', unit:'Kg', min:0, max:15000 },
  // Contador de sacos expedidos pela ensacadeira — zera sozinho no CLP à
  // meia-noite (23:59:55), então o valor lido é sempre "sacos expedidos hoje".
  { id:'ct_700rf001_sacos', label:'SACOS EXPEDIDOS (DIA)', tagName:'COUNTER_700RF001_U01.ACC', unit:'sacos', min:0, max:99999 },

  // ── Relógio do CLP (GSV WallClockTime, Rockwell) ──────────────────────────
  // Array DINT[7] padrão: [0]=Ano [1]=Mes [2]=Dia [3]=Hora [4]=Min [5]=Seg [6]=Ms
  // (índice 6/Ms não é lido — não é usado em nenhum cálculo aqui, e cada tag
  // a mais é mais um ponto de falha na leitura EtherNet/IP, então só os 6
  // necessários pra montar a data/hora). Usado por horaCLP() pra decidir
  // turno diurno/noturno, data de referência do turno e reset diário de
  // sacos usando o relógio REAL do controlador em vez do relógio do Node.js
  // — elimina risco de desalinhamento entre os dois relógios.
  { id:'clp_ano', label:'RELOGIO CLP - ANO',    tagName:'LocalDateTime[0]', unit:'', min:2020, max:2100 },
  { id:'clp_mes', label:'RELOGIO CLP - MES',    tagName:'LocalDateTime[1]', unit:'', min:1,    max:12   },
  { id:'clp_dia', label:'RELOGIO CLP - DIA',    tagName:'LocalDateTime[2]', unit:'', min:1,    max:31   },
  { id:'clp_hora',label:'RELOGIO CLP - HORA',   tagName:'LocalDateTime[3]', unit:'', min:0,    max:23   },
  { id:'clp_min', label:'RELOGIO CLP - MINUTO', tagName:'LocalDateTime[4]', unit:'', min:0,    max:59   },
  { id:'clp_seg', label:'RELOGIO CLP - SEGUNDO',tagName:'LocalDateTime[5]', unit:'', min:0,    max:59   },

  // ── Consumo estimado de energia (rotina nova no CLP, GR1_500BC001 / 501) ──
  // AJUSTE[11] = potência total (separador + exaustor + motor + 20kW fixo),
  // já somada pelo próprio CLP — substitui o cálculo equivalente que o
  // server fazia em Node.js (cargaFixa()) só como amostra de potência.
  // AJUSTE[13] = kWh acumulado do turno atual, ZERADO PELO PRÓPRIO CLP duas
  // vezes por dia (06:59:55 fecha o noturno, 18:01:57 fecha o diurno) SEM
  // arquivar o valor em nenhum outro tag antes — ver registrarEnergiaClp()
  // pra entender como isso é capturado antes de se perder.
  { id:'ea_501bm140_pot_total', label:'POTENCIA TOTAL MOAGEM 501', tagName:'EA_501BM140_JT_U01_AJUSTE[11]', unit:'kW',  min:0, max:2000 },
  { id:'ea_502bm140_pot_total', label:'POTENCIA TOTAL MOAGEM 502', tagName:'EA_502BM140_JT_U01_AJUSTE[11]', unit:'kW',  min:0, max:2000 },
  { id:'ea_501bm140_kwh_turno', label:'ENERGIA ACUMULADA TURNO 501', tagName:'EA_501BM140_JT_U01_AJUSTE[13]', unit:'kWh', min:0, max:99999 },
  { id:'ea_502bm140_kwh_turno', label:'ENERGIA ACUMULADA TURNO 502', tagName:'EA_502BM140_JT_U01_AJUSTE[13]', unit:'kWh', min:0, max:99999 },
];

// ─── Tags Digitais — Status equipamentos (DINT bits) e Fluxos Hidráulicos ────
// Lidas via EtherNet/IP como DINT inteiro, bits extraídos no servidor
// Bit .6 = Ligado | .8 = Trip | .9 = Sobrecarga | .10 = Fluxo | .29 = Emergência | .30 = Alarme
const DINT_TAGS = [
  // M501BM140.6 / M502BM140.6 = retorno de ligado (motor do moinho rodando de
  // verdade). Usado como critério de "moinho rodando" pra atualizar energia
  // e minutos, em vez do limiar de potência (pot > 50kW) — mais preciso,
  // porque é o próprio bit de campo, não uma inferência por potência.
  { id: 'm501bm140', tagName: 'M501BM140', label: 'RETORNO DE LIGADO MOINHO 501', area: '501' },
  { id: 'm502bm140', tagName: 'M502BM140', label: 'RETORNO DE LIGADO MOINHO 502', area: '502' },
];

// Estado atual dos DINTs — { id: { raw, ligado, trip, sobrecarga, fluxo, emergencia, alarme, ts } }
let dadosDigitais = {};
DINT_TAGS.forEach(t => { dadosDigitais[t.id] = { raw:0, ligado:false, falha:false, fluxo:false, falha_fluxo:false, emergencia:false, alarme_geral:false, ts:Date.now() }; });

function parseDint(raw, isLQ, customBit) {
  const v    = raw | 0;
  const bit6 = !!(v & (1<<6));
  const bit7 = !!(v & (1<<7));
  const bit8 = !!(v & (1<<8));   // DISJUNTOR OK — falha quando =0
  const bit10= !!(v & (1<<10));  // FLUXO (LQ145)
  const bit14= !!(v & (1<<14));  // PRESENÇA TENSÃO / RETORNO (LQ147)

  // Lógica de "ligado" por tipo:
  // isLQ (LQ145, LQ150): bit6 OU bit7
  // customBit='14' (LQ147): bit14
  // padrão: bit6
  let ligado;
  if(isLQ)           ligado = bit6 || bit7;
  else if(customBit==='14') ligado = bit14;
  else               ligado = bit6;

  // Fluxo só para LQ145 (isLQ com bit10)
  const fluxo = isLQ ? bit10 : false;
  const falha_fluxo = isLQ ? (ligado && !bit10) : false;

  return {
    raw, ligado,
    falha:       !bit8,
    fluxo, falha_fluxo,
    emergencia:  !!(v & (1<<29)),
    alarme_geral:!!(v & (1<<30)),
  };
}

// ─── Arquivos ─────────────────────────────────────────────────────────────────────
const DB_FILE      = path.join(__dirname, 'historico.json');
const ALARMES_FILE = path.join(__dirname, 'log_alarmes.json');
const LIMITES_FILE = path.join(__dirname, 'limites_alarmes.json');
const HISTORY_DAYS = 7;

let limitesEditados = {};
if (fs.existsSync(LIMITES_FILE)) {
  try { limitesEditados = JSON.parse(fs.readFileSync(LIMITES_FILE, 'utf8')); } catch(e) {}
}

function buildTagsConfig() {
  return TAGS_BASE.map(t => {
    const e = limitesEditados[t.id] || {};
    return {
      ...t,
      alarme_hi:   e.alarme_hi   !== undefined ? e.alarme_hi   : (t.alarme_hi   ?? null),
      alarme_hihi: e.alarme_hihi !== undefined ? e.alarme_hihi : (t.alarme_hihi ?? null),
      alarme_lo:   e.alarme_lo   !== undefined ? e.alarme_lo   : (t.alarme_lo   ?? null),
    };
  });
}
let TAGS_CONFIG = buildTagsConfig();

// ─── Banco de dados ───────────────────────────────────────────────────────────
let db = { leituras: {}, amostras: {} };
if (fs.existsSync(DB_FILE)) {
  try {
    const parsed = JSON.parse(fs.readFileSync(DB_FILE, 'utf8'));
    db.leituras = parsed.leituras || {};
    db.amostras = parsed.amostras || {};
    console.log('[DB] Histórico carregado.');
  } catch(e) { console.warn('[DB] Histórico inválido, iniciando zerado.'); }
}
// Garantir que todas as tags têm arrays inicializados
TAGS_CONFIG.forEach(t => {
  if (!db.leituras[t.id]) db.leituras[t.id] = [];
  if (!db.amostras[t.id]) db.amostras[t.id] = [];
});

const ultimaAmostra = {};
let dbDirty = false;
setInterval(() => {
  if (!dbDirty) return;
  try { fs.writeFileSync(DB_FILE, JSON.stringify(db), 'utf8'); dbDirty = false; }
  catch(e) { console.error('[DB] Erro ao salvar:', e.message); }
}, 30000);

function registrarLeitura(tagId, valor, ts) {
  if (!db.leituras[tagId]) db.leituras[tagId] = [];
  db.leituras[tagId].push({ ts, v: valor });
  if (db.leituras[tagId].length > 2500) db.leituras[tagId].splice(0, 500);

  if (!db.amostras[tagId]) db.amostras[tagId] = [];
  const ultima = ultimaAmostra[tagId] || 0;
  if (ts - ultima >= 60000) {
    ultimaAmostra[tagId] = ts;
    db.amostras[tagId].push({ ts, v: valor });
    if (db.amostras[tagId].length > 11000) db.amostras[tagId].splice(0, 1000);
  }
  dbDirty = true;
}

setInterval(() => {
  const now = Date.now();
  const lim7d = now - HISTORY_DAYS * 86400000;
  const lim2h = now - 7200000;
  TAGS_CONFIG.forEach(t => {
    db.amostras[t.id] = (db.amostras[t.id] || []).filter(r => r.ts >= lim7d);
    db.leituras[t.id] = (db.leituras[t.id] || []).filter(r => r.ts >= lim2h);
  });
  dbDirty = true;
}, 3600000);

// ─── Estado em memória ────────────────────────────────────────────────────────
let dadosAtuais = {};
TAGS_CONFIG.forEach(t => { dadosAtuais[t.id] = { valor: null, qualidade: 'Uncertain', ts: Date.now(), alarme: 'normal' }; });

// ─── Log de Alarmes ───────────────────────────────────────────────────────────
let logAlarmes = [];
const MAX_LOG = 5000;
if (fs.existsSync(ALARMES_FILE)) {
  try { logAlarmes = JSON.parse(fs.readFileSync(ALARMES_FILE, 'utf8')); } catch(e) {}
}
function salvarLogAlarmes() {
  try { fs.writeFileSync(ALARMES_FILE, JSON.stringify(logAlarmes), 'utf8'); } catch(e) {}
}
const estadoAlarme = {};

function verificarAlarme(cfg, valor) {
  if (valor == null) return 'normal';
  if (cfg.alarme_hihi != null && valor > cfg.alarme_hihi) return 'hihi';
  if (cfg.alarme_hi   != null && valor > cfg.alarme_hi)   return 'hi';
  if (cfg.alarme_lo   != null && valor < cfg.alarme_lo)   return 'lo';
  return 'normal';
}

function processarAlarme(cfg, valor, ts) {
  let habilitado = true;
  if (cfg.alarme_condicional) {
    try { habilitado = cfg.alarme_condicional(dadosAtuais); }
    catch(e) { habilitado = false; }
  }
  const novoEstado = habilitado ? verificarAlarme(cfg, valor) : 'normal';
  const antigo     = estadoAlarme[cfg.id] || 'normal';
  if (novoEstado !== antigo) {
    estadoAlarme[cfg.id] = novoEstado;
    if (novoEstado !== 'normal') {
      const e = {
        id: `${Date.now()}_${cfg.id}`, ts, tagId: cfg.id, label: cfg.label, tagName: cfg.tagName,
        tipo: novoEstado, valor, unidade: cfg.unit,
        limite: novoEstado === 'hihi' ? cfg.alarme_hihi : novoEstado === 'hi' ? cfg.alarme_hi : cfg.alarme_lo,
        condicional: cfg.condicional_desc || null,
        retorno: null, ack: false, ackUser: null, ackTs: null,
      };
      logAlarmes.unshift(e);
      if (logAlarmes.length > MAX_LOG) logAlarmes.splice(MAX_LOG);
      salvarLogAlarmes();
      broadcast({ tipo: 'alarme_entrada', alarme: e });
      const cond = cfg.alarme_condicional ? ' [CONDICIONAL]' : '';
      console.log(`[ALARME${cond}] ${cfg.label} — ${novoEstado.toUpperCase()} | ${valor} ${cfg.unit}`);
    } else {
      const aberto = logAlarmes.find(a => a.tagId === cfg.id && !a.retorno);
      if (aberto) { aberto.retorno = ts; salvarLogAlarmes(); broadcast({ tipo: 'alarme_retorno', alarmeId: aberto.id, ts }); }
    }
  }
  return novoEstado;
}


// ─── Express + WS ─────────────────────────────────────────────────────────────
const app    = express();
const server = http.createServer(app);
const wss    = new WebSocket.Server({ server });
app.use(cors()); app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

function broadcast(data) {
  const m = JSON.stringify(data);
  wss.clients.forEach(c => { if (c.readyState === WebSocket.OPEN) c.send(m); });
}

const tagsSer = () => TAGS_CONFIG.map(t => ({ ...t, alarme_condicional: undefined }));

wss.on('connection', ws => {
  const snap = {};
  TAGS_CONFIG.forEach(t => { snap[t.id] = { ...t, alarme_condicional: undefined, ...dadosAtuais[t.id] }; });
  ws.send(JSON.stringify({ tipo: 'snapshot', dados: snap, tags: tagsSer() }));
  ws.send(JSON.stringify({ tipo: 'log_alarmes', alarmes: logAlarmes.slice(0, 300) }));
  ws.send(JSON.stringify({ tipo: 'nomes_produtos', nomes: nomesCache }));
  // Enviar coleta ao novo cliente — só as últimas 24h (288 registros a
  // 5min/registro), não o mês inteiro (~8600 registros). Isso evita um
  // JSON.stringify síncrono grande a CADA reconexão de WebSocket (celular
  // saindo/voltando de wifi, por exemplo — que já foi causa de travamento
  // antes, ver coletaAnteriorCache). Nenhum dos front-ends atuais
  // (mobile-status.html, dashboard) consome 'coleta_snapshot'/'coleta_anterior'
  // hoje — se algum outro painel usar isso pro mês inteiro, avisar antes de
  // reduzir mais ou remover de vez.
  const JANELA_COLETA_WS = 288;
  ws.send(JSON.stringify({ tipo: 'coleta_snapshot', mes: coletaCorrente.mes, registros: coletaCorrente.registros.slice(-JANELA_COLETA_WS) }));
  if (coletaAnteriorCache) {
    ws.send(JSON.stringify({ tipo: 'coleta_anterior', mes: coletaAnteriorCache.mes, registros: coletaAnteriorCache.registros.slice(-JANELA_COLETA_WS) }));
  }

  // Snapshot digitais
  const snapDig = {};
  DINT_TAGS.forEach(t => {
    const isLQ = !!(t.isLQ);
    const d = dadosDigitais[t.id] || {};
    snapDig[t.id] = { ...t, ...d, fluxo: isLQ ? (d.fluxo||false) : false };
  });
  ws.send(JSON.stringify({ tipo: 'snapshot_digital', dados: snapDig, tags: DINT_TAGS }));

  // Ping a cada 8s para manter conexão viva em switches/roteadores
  // que derrubam conexões TCP ociosas silenciosamente
  ws.isAlive = true;
  ws.on('pong', () => { ws.isAlive = true; });
});

// Heartbeat do servidor: pinga todos os clientes a cada 8s
// Clientes que não responderem ao pong são terminados (browser reconecta)
const wsPingInterval = setInterval(() => {
  wss.clients.forEach(ws => {
    if (ws.isAlive === false) { ws.terminate(); return; }
    ws.isAlive = false;
    ws.ping();
  });
}, 8000);
wss.on('close', () => clearInterval(wsPingInterval));

// ─── API ──────────────────────────────────────────────────────────────────────
app.get('/api/tags',  (req, res) => res.json(tagsSer().map(t => ({ ...t, ...dadosAtuais[t.id] }))));
app.get('/api/dados', (req, res) => { const r = {}; TAGS_CONFIG.forEach(t => { r[t.id] = { ...t, alarme_condicional: undefined, ...dadosAtuais[t.id] }; }); res.json(r); });

app.get('/api/historico/:tagId', (req, res) => {
  const tag = TAGS_CONFIG.find(t => t.id === req.params.tagId);
  if (!tag) return res.status(404).json({ erro: 'Tag não encontrada' });
  const agora = Date.now();
  const inicio = parseInt(req.query.inicio) || agora - 7200000;
  const fim    = parseInt(req.query.fim)    || agora;
  res.json({ tag: { ...tag, alarme_condicional: undefined }, registros: (db.leituras[tag.id] || []).filter(r => r.ts >= inicio && r.ts <= fim) });
});

app.get('/api/grafico/:tagId', (req, res) => {
  const tag = TAGS_CONFIG.find(t => t.id === req.params.tagId);
  if (!tag) return res.status(404).json({ erro: 'Tag não encontrada' });
  const agora  = Date.now();
  const dias   = Math.min(7, parseFloat(req.query.dias) || 1);
  const inicio = parseInt(req.query.inicio) || agora - Math.round(dias * 86400000);
  const fim    = parseInt(req.query.fim)    || agora;
  let amostras = (db.amostras[tag.id] || []).filter(r => r.ts >= inicio && r.ts <= fim);
  if (amostras.length > 2000) { const s = Math.ceil(amostras.length / 2000); amostras = amostras.filter((_, i) => i % s === 0); }
  res.json({ tag: { ...tag, alarme_condicional: undefined }, amostras });
});

app.get('/api/exportar/:tagId', (req, res) => {
  const tag = TAGS_CONFIG.find(t => t.id === req.params.tagId);
  if (!tag) return res.status(404).json({ erro: 'Tag não encontrada' });
  const agora  = Date.now();
  const dias   = Math.min(7, parseFloat(req.query.dias) || 7);
  const inicio = parseInt(req.query.inicio) || agora - Math.round(dias * 86400000);
  const fim    = parseInt(req.query.fim)    || agora;
  const amostras = (db.amostras[tag.id] || []).filter(r => r.ts >= inicio && r.ts <= fim);
  let csv = `Data/Hora,${tag.label} (${tag.unit})\n`;
  amostras.forEach(r => { csv += `${new Date(r.ts).toLocaleString('pt-BR')},${r.v}\n`; });
  res.setHeader('Content-Type', 'text/csv; charset=utf-8');
  res.setHeader('Content-Disposition', `attachment; filename="${tag.id}.csv"`);
  res.send('\uFEFF' + csv);
});

app.get('/api/alarmes', (req, res) => res.json(logAlarmes.slice(0, parseInt(req.query.limit) || 500)));

app.get('/api/exportar_alarmes', (req, res) => {
  const inicio = parseInt(req.query.inicio) || 0;
  const fim    = parseInt(req.query.fim)    || Date.now();
  const f = logAlarmes.filter(a => a.ts >= inicio && a.ts <= fim);
  let csv = 'Entrada,Tag,Descrição,Tipo,Valor,Unidade,Limite,Condição,Retorno,Reconhecido,Usuário\n';
  f.forEach(a => { csv += [new Date(a.ts).toLocaleString('pt-BR'), a.tagName, `"${a.label}"`, a.tipo.toUpperCase(), a.valor, a.unidade, a.limite, `"${a.condicional||''}"`, a.retorno ? new Date(a.retorno).toLocaleString('pt-BR') : 'ATIVO', a.ack ? 'SIM' : 'NÃO', a.ackUser||''].join(',') + '\n'; });
  res.setHeader('Content-Type', 'text/csv; charset=utf-8');
  res.setHeader('Content-Disposition', 'attachment; filename="log_alarmes.csv"');
  res.send('\uFEFF' + csv);
});

app.post('/api/alarmes/:id/ack', (req, res) => {
  const a = logAlarmes.find(x => x.id === req.params.id);
  if (!a) return res.status(404).json({ erro: 'Não encontrado' });
  a.ack = true; a.ackUser = req.body.usuario || 'Operador'; a.ackTs = Date.now();
  salvarLogAlarmes();
  broadcast({ tipo: 'alarme_ack', alarmeId: a.id, ackUser: a.ackUser, ackTs: a.ackTs });
  res.json({ ok: true });
});

app.get('/api/limites', (req, res) => res.json(limitesEditados));

app.post('/api/limites/:tagId', (req, res) => {
  const tagId = req.params.tagId;
  if (!TAGS_CONFIG.find(t => t.id === tagId)) return res.status(404).json({ erro: 'Tag não encontrada' });
  const p = v => (v === undefined || v === '' || v === null) ? null : Number(v);
  limitesEditados[tagId] = { alarme_hi: p(req.body.alarme_hi), alarme_hihi: p(req.body.alarme_hihi), alarme_lo: p(req.body.alarme_lo) };
  try { fs.writeFileSync(LIMITES_FILE, JSON.stringify(limitesEditados, null, 2), 'utf8'); } catch(e) {}
  TAGS_CONFIG = buildTagsConfig();
  broadcast({ tipo: 'limites_atualizados', tagId, limites: limitesEditados[tagId] });
  res.json({ ok: true, limites: limitesEditados[tagId] });
});

// ─── API Indicadores ─────────────────────────────────────────────────────────
// Retorna indicadores calculados para um moinho (501 ou 502)
// ─── Integração Firestore (estoque-cimento-gaucho) — Qualidade (Blaine/malha) ─
// Leitura pública via REST (confirmado: sem API key, sem firebase-admin), com
// cache em memória de ~2min — o documento inteiro tem ~8MB (todos os meses
// juntos), então buscar sem cache seria caro e desnecessário.
const FIRESTORE_URL = 'https://firestore.googleapis.com/v1/projects/estoque-cimento-gaucho/databases/(default)/documents/producao/dados';
let _fsCache = { payload: null, ts: 0 };

// Desembrulha o formato "wrapped value" do Firestore REST (mapValue/arrayValue/...)
function unwrapFirestoreValue(v) {
  if (v == null) return null;
  if ('nullValue' in v) return null;
  if ('stringValue' in v) return v.stringValue;
  if ('doubleValue' in v) return v.doubleValue;
  if ('integerValue' in v) return parseInt(v.integerValue, 10);
  if ('booleanValue' in v) return v.booleanValue;
  if ('timestampValue' in v) return v.timestampValue;
  if ('arrayValue' in v) return (v.arrayValue.values || []).map(unwrapFirestoreValue);
  if ('mapValue' in v) {
    const out = {};
    const fields = v.mapValue.fields || {};
    for (const k in fields) out[k] = unwrapFirestoreValue(fields[k]);
    return out;
  }
  return null;
}

async function getPayloadFirestore(forceRefresh = false) {
  const agora = Date.now();
  if (!forceRefresh && _fsCache.payload && (agora - _fsCache.ts) < 120000) return _fsCache.payload; // cache 2min
  try {
    const resp = await fetch(FIRESTORE_URL);
    if (!resp.ok) throw new Error(`Firestore respondeu ${resp.status}`);
    const doc = await resp.json();
    const payload = unwrapFirestoreValue({ mapValue: { fields: doc.fields } })?.payload || null;
    _fsCache = { payload, ts: agora };
    return payload;
  } catch (e) {
    console.warn('[FIRESTORE] Falha ao buscar dados de qualidade:', e.message);
    return _fsCache.payload; // devolve o último cache válido (mesmo vencido) em caso de falha
  }
}

// Embrulha um valor JS puro no formato "wrapped value" do Firestore REST.
// Números sempre viram doubleValue — na leitura (unwrapFirestoreValue) tanto
// doubleValue quanto integerValue voltam como number comum do JS, então não
// há perda nem incompatibilidade para o app cimento_gaucho.html.
function wrapFirestoreValue(v) {
  if (v === null || v === undefined) return { nullValue: null };
  if (typeof v === 'string')  return { stringValue: v };
  if (typeof v === 'boolean') return { booleanValue: v };
  if (typeof v === 'number')  return { doubleValue: v };
  if (Array.isArray(v)) return { arrayValue: { values: v.map(wrapFirestoreValue) } };
  if (typeof v === 'object') {
    const fields = {};
    for (const k in v) fields[k] = wrapFirestoreValue(v[k]);
    return { mapValue: { fields } };
  }
  return { nullValue: null };
}

// Confere se o payload tem a "forma" esperada antes de deixá-lo ser gravado
// de volta no Firestore. Como pushPayloadFirestore substitui o campo
// `payload` INTEIRO (não faz merge profundo), um fetch parcial/corrompido
// (rede cortada no meio, resposta truncada etc.) gravado de volta apaga
// entradas/saídas/ensacadeira/produção de todos os meses. Esse guard existe
// especificamente pra nunca deixar isso acontecer de novo (já aconteceu).
function payloadPareceIntegro(payload) {
  if (!payload || !Array.isArray(payload.months) || payload.months.length === 0) {
    console.error('[SYNC] ABORTADO: payload sem months[] — provável leitura incompleta do Firestore.');
    return false;
  }
  for (const mes of payload.months) {
    const diasNoMes = new Date(mes.year, mes.month, 0).getDate();
    const campos = ['moinhos', 'm1', 'm2', 'entradas', 'saidas', 'ensacadeira'];
    for (const campo of campos) {
      if (!Array.isArray(mes[campo])) {
        console.error(`[SYNC] ABORTADO: mês ${mes.key} está sem o array "${campo}" — payload incompleto, não vou gravar.`);
        return false;
      }
      if (mes[campo].length < diasNoMes - 2) {
        console.error(`[SYNC] ABORTADO: mês ${mes.key}.${campo} tem só ${mes[campo].length}/${diasNoMes} dias — payload truncado, não vou gravar.`);
        return false;
      }
    }
  }
  return true;
}

// Guarda uma cópia local do payload antes de sobrescrever no Firestore —
// rede de segurança pra recuperação manual caso algo passe pela validação
// acima e ainda assim dê problema. Mantém só os últimos 20 backups.
// Assíncrona: o payload do cimento_gaucho pode ter vários meses de dados
// (MBs), e isso roda a cada 15 min — escrever de forma síncrona bloquearia
// o event loop pelo tempo da escrita, mesma classe de bug que já corrigimos
// no turno em andamento.
const BACKUP_DIR = path.join(__dirname, 'backups_payload');
function backupPayloadLocal(payload) {
  if (!fs.existsSync(BACKUP_DIR)) { try { fs.mkdirSync(BACKUP_DIR); } catch(e) {} }
  const ts = new Date().toISOString().replace(/[:.]/g, '-');
  fs.writeFile(path.join(BACKUP_DIR, `payload_${ts}.json`), JSON.stringify(payload), 'utf8', (err) => {
    if (err) { console.warn('[SYNC] Falha ao gravar backup local:', err.message); return; }
    fs.readdir(BACKUP_DIR, (err2, arquivos) => {
      if (err2) return;
      const antigos = arquivos.filter(f => f.startsWith('payload_')).sort();
      while (antigos.length > 20) {
        const alvo = antigos.shift();
        fs.unlink(path.join(BACKUP_DIR, alvo), () => {});
      }
    });
  });
}

// Grava o payload inteiro de volta no Firestore (mesmo documento/campo que o
// cimento_gaucho.html usa em window._fb.save). Regras do Firestore já são
// públicas (allow read, write: if true), então não precisa de API key nem
// firebase-admin — só um PATCH REST simples, igual ao GET já usado acima.
// SEMPRE valida a integridade e guarda backup local antes de gravar.
async function pushPayloadFirestore(payload) {
  if (!payloadPareceIntegro(payload)) return false;
  backupPayloadLocal(payload);
  try {
    const body = {
      fields: {
        payload:   wrapFirestoreValue(payload),
        updatedAt: wrapFirestoreValue(Date.now()),
      },
    };
    const url = `${FIRESTORE_URL}?updateMask.fieldPaths=payload&updateMask.fieldPaths=updatedAt`;
    const resp = await fetch(url, {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    if (!resp.ok) {
      const txt = await resp.text().catch(() => '');
      throw new Error(`Firestore PATCH respondeu ${resp.status}: ${txt.slice(0, 300)}`);
    }
    _fsCache = { payload, ts: Date.now() };
    return true;
  } catch (e) {
    console.error('[FIRESTORE] Falha ao gravar boletim:', e.message);
    return false;
  }
}

// ── Especificações de qualidade por produto (mesma lógica do cimento_gaucho.html) ──
const QUALIDADE_SPECS = {
  'CPIV':    { malhaMax: 5, blaineMin: 4200, blaineMax: 5500 },
  'Pozomix': { malhaMax: 2, blaineMin: 5000, blaineMax: 6000 },
  'CPII-Z':  { malhaMax: 3, blaineMin: 4500, blaineMax: 5500 },
};
function classRecQ(receita) {
  const r = (receita || '').toUpperCase().trim();
  if (r.startsWith('CPIIZ') || r.startsWith('CPII-Z')) return 'CPII-Z';
  if (r.startsWith('CPIV'))  return 'CPIV';
  if (r.startsWith('POZO'))  return 'Pozomix';
  return receita || '';
}
function numOrNullQ(v) { const n = parseFloat(v); return isNaN(n) ? null : n; }
function qualScoreMalha(malha, spec) {
  if (malha == null || !spec) return null;
  if (malha <= spec.malhaMax) return 1;
  return Math.max(0, 1 - (malha - spec.malhaMax) / spec.malhaMax);
}
function qualScoreBlaine(blaine, spec) {
  if (blaine == null || !spec) return null;
  if (blaine >= spec.blaineMin && blaine <= spec.blaineMax) return 1;
  const faixa = spec.blaineMax - spec.blaineMin;
  const desvio = blaine < spec.blaineMin ? (spec.blaineMin - blaine) : (blaine - spec.blaineMax);
  return Math.max(0, 1 - desvio / faixa);
}
function calcQualidadeRow(r) {
  const specD = QUALIDADE_SPECS[classRecQ(r.receitaD || r.receita)];
  const specN = QUALIDADE_SPECS[classRecQ(r.receitaN || r.receita)];
  const scores = [];
  let temMedicao = false;
  if (specD) {
    const sm = qualScoreMalha(numOrNullQ(r.dMalha), specD);   if (sm != null) { scores.push(sm); temMedicao = true; }
    const sb = qualScoreBlaine(numOrNullQ(r.dBlaine), specD); if (sb != null) { scores.push(sb); temMedicao = true; }
  }
  if (specN) {
    const sm = qualScoreMalha(numOrNullQ(r.nMalha), specN);   if (sm != null) { scores.push(sm); temMedicao = true; }
    const sb = qualScoreBlaine(numOrNullQ(r.nBlaine), specN); if (sb != null) { scores.push(sb); temMedicao = true; }
  }
  return { score: scores.length ? scores.reduce((a,b)=>a+b,0)/scores.length : 1, temMedicao };
}

// linha 501 -> m1, 502 -> m2 (confirmado pelo usuário: moinho1=501, moinho2=502)
async function calcQualidadeMedia(linha, inicio, fim) {
  const payload = await getPayloadFirestore();
  if (!payload || !Array.isArray(payload.months)) {
    return { qualidade: null, diasComMedicao: 0, diasNoPeriodo: 0, malhaMedia: null, blaineMedia: null, detalhe: [] };
  }

  const chaveM = linha === '501' ? 'm1' : 'm2';
  let somaScore = 0, diasComMedicao = 0, diasNoPeriodo = 0;
  let somaMalha = 0, nMalha = 0, somaBlaine = 0, nBlaine = 0;
  const detalhe = [];

  for (let t = inicio; t < fim; t += 86400000) {
    const d = new Date(t);
    const chaveMes = `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}`;
    const dataDia  = `${String(d.getDate()).padStart(2,'0')}/${String(d.getMonth()+1).padStart(2,'0')}`;
    const mes = payload.months.find(m => m.key === chaveMes);
    if (!mes || !Array.isArray(mes[chaveM])) continue;
    const row = mes[chaveM].find(r => r.data === dataDia);
    if (!row) continue;
    diasNoPeriodo++;
    const { score, temMedicao } = calcQualidadeRow(row);
    if (temMedicao) { somaScore += score; diasComMedicao++; }

    const dMalha  = numOrNullQ(row.dMalha),  dBlaine = numOrNullQ(row.dBlaine);
    const nMalhaV = numOrNullQ(row.nMalha),  nBlaineV = numOrNullQ(row.nBlaine);
    if (dMalha  != null) { somaMalha  += dMalha;  nMalha++;  }
    if (nMalhaV != null) { somaMalha  += nMalhaV; nMalha++;  }
    if (dBlaine != null) { somaBlaine += dBlaine; nBlaine++; }
    if (nBlaineV!= null) { somaBlaine += nBlaineV;nBlaine++; }

    if (dMalha != null || dBlaine != null || nMalhaV != null || nBlaineV != null) {
      detalhe.push({
        data: dataDia,
        receitaD: row.receitaD || row.receita || null, dMalha, dBlaine,
        receitaN: row.receitaN || row.receita || null, nMalha: nMalhaV, nBlaine: nBlaineV,
      });
    }
  }

  return {
    qualidade: diasComMedicao > 0 ? parseFloat(((somaScore / diasComMedicao) * 100).toFixed(1)) : null,
    diasComMedicao,
    diasNoPeriodo,
    malhaMedia:  nMalha  > 0 ? parseFloat((somaMalha  / nMalha).toFixed(2))  : null,
    blaineMedia: nBlaine > 0 ? Math.round(somaBlaine / nBlaine) : null,
    detalhe, // um item por dia com medição — usado no boletim diário pra mostrar malha/Blaine por turno
  };
}

// Disponibilidade e Performance calculadas dia a dia, a partir da MESMA
// tabela do Firestore que o cimento_gaucho.html usa (minM1/minM2 já
// sincronizados por sincronizarBoletimFirebase(), dTh/nTh também) — mesma
// metodologia EXATA do calcOEEdia() do index.html, pra garantir que os dois
// boletins deem o mesmo número em cima dos mesmos dados.
// T/h nominal do EQUIPAMENTO (capacidade do moinho em si), não da receita —
// a receita tem sua própria taxa esperada (varia com matéria-prima/carga de
// bolas), mas o Desempenho do OEE precisa medir contra a capacidade do
// moinho, senão uma receita "no ponto" sempre daria ~100% e esconderia
// perda real de desempenho do equipamento. Mesmo valor pros dois moinhos
// (501 e 502) — confirmado pelo usuário. Igual ao NOMINAL_MOINHO_TH do
// index.html — se um dia precisar mudar, mudar nos dois lugares.
const NOMINAL_MOINHO_TH = 30;

async function calcDispPerfMedia(linha, inicio, fim) {
  const payload = await getPayloadFirestore();
  if (!payload || !Array.isArray(payload.months)) {
    return { disponibilidade: null, performance: null };
  }
  const chaveM    = linha === '501' ? 'm1' : 'm2';
  const campoMin  = linha === '501' ? 'minM1' : 'minM2';
  const agora     = horaCLP().data;

  let somaMin = 0, somaMinDisp = 0, somaDesempPond = 0;

  for (let t = inicio; t < fim; t += 86400000) {
    const d = new Date(t);
    const chaveMes = `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}`;
    const dataDia  = `${String(d.getDate()).padStart(2,'0')}/${String(d.getMonth()+1).padStart(2,'0')}`;
    const mes = payload.months.find(m => m.key === chaveMes);
    if (!mes || !Array.isArray(mes.moinhos) || !Array.isArray(mes[chaveM])) continue;

    const diaIndex = parseInt(dataDia.split('/')[0], 10) - 1;
    const min = mes.moinhos[diaIndex]?.[campoMin];
    if (min == null || min <= 0) continue; // mesmo filtro do index.html: só dias com minutos lançados

    const row = mes[chaveM].find(r => r.data === dataDia);
    if (!row) continue;

    // Minutos disponíveis: 1440 (dia inteiro) — ou só os minutos já
    // decorridos, se esse dia for HOJE — mesma lógica de minutosDisponiveisNoDia().
    const éHoje = d.getFullYear() === agora.getFullYear() && d.getMonth() === agora.getMonth() && d.getDate() === agora.getDate();
    const minDisp = éHoje ? (agora.getHours()*60 + agora.getMinutes()) : 1440;

    const thReal = ((parseFloat(row.dTh)||0)+(parseFloat(row.nTh)||0)) / Math.max((row.dTh?1:0)+(row.nTh?1:0),1);
    const desemp = Math.min(thReal/NOMINAL_MOINHO_TH, 1);

    somaMin        += min;
    somaMinDisp    += minDisp;
    somaDesempPond += desemp*min;
  }

  const disponibilidade = somaMinDisp > 0
    ? parseFloat((Math.min(somaMin/somaMinDisp, 1) * 100).toFixed(1)) : null;
  const performance = somaMin > 0
    ? parseFloat((somaDesempPond/somaMin * 100).toFixed(1)) : null;
  return { disponibilidade, performance };
}

// ─── API Boletim (Diário/Mensal) ───────────────────────────────────────────
// Usa a MESMA fonte que a aba "Dados de Produção" (producao.turnos, permanente,
// até 90 turnos ≈ 45 dias) — NÃO usa db.amostras (janela de só 7 dias, HISTORY_DAYS).
// Isso existe porque o /api/indicadores, ao integrar sobre db.amostras, dava
// totais divergentes do "Dados de Produção" pra qualquer período com mais de
// 7 dias — a maior parte do mês simplesmente não tinha mais amostra nenhuma.
app.get('/api/boletim/:linha', async (req, res) => {
  const ln = req.params.linha;
  if (!['501','502'].includes(ln)) return res.status(400).json({ erro: 'Linha inválida' });

  const agora   = Date.now();
  const inicio  = parseInt(req.query.inicio) || agora - 86400000;
  const fim     = parseInt(req.query.fim)    || agora;
  const nominal = NOMINAL_MOINHO_TH; // fixo (equipamento), não mais configurável por query param
  const horasPeriodo = (fim - inicio) / 3600000;
  const chaveLinha = `linha${ln}`;

  const turnosPeriodo = producao.turnos.filter(t => t.inicio >= inicio && t.inicio < fim);

  // Agrupa por dia (mesma convenção de "noturno pertence ao dia em que
  // começou" usada em todo o resto) e, pra cada dia, prefere o total
  // capturado direto do CLP ([84] pra minutos, [38] pra tonelada) — só cai
  // na soma dos turnos daquele dia se ainda não tiver captura (dia recente
  // demais, ou fora da janela de retenção). Isso evita que o boletim
  // (mesmo de um único dia) fique um pouco abaixo do real quando a soma
  // dos dois turnos individualmente perde alguns minutos/toneladas na
  // borda — o total do dia direto do CLP não tem essa perda.
  const porDiaPeriodo = {};
  turnosPeriodo.forEach(t => {
    const l = t[chaveLinha];
    if (!l) return;
    const dataStr = t.data; // já vem formatado 'DD/MM/YYYY' no registro do turno
    if (!porDiaPeriodo[dataStr]) porDiaPeriodo[dataStr] = { minRodando: 0, tonProduzida: 0 };
    porDiaPeriodo[dataStr].minRodando   += l.minRodando   || 0;
    porDiaPeriodo[dataStr].tonProduzida += l.tonProduzida || 0;
  });

  let tonTotal = 0, kwhTotal = 0, minRodando = 0;
  turnosPeriodo.forEach(t => {
    const l = t[chaveLinha];
    if (!l) return;
    kwhTotal += l.kwh || 0;
  });
  for (const dataStr of Object.keys(porDiaPeriodo)) {
    const minCapturado = minTotalDiaState.historico[`${dataStr}_${ln}`];
    const tonCapturado = tonTotalDiaState.historico[`${dataStr}_${ln}`];
    minRodando += minCapturado != null ? minCapturado : porDiaPeriodo[dataStr].minRodando;
    tonTotal   += tonCapturado != null ? tonCapturado : porDiaPeriodo[dataStr].tonProduzida;
  }

  const horasRodando = minRodando / 60;
  const tonHMedia = horasRodando > 0 ? parseFloat((tonTotal / horasRodando).toFixed(2)) : null;
  // kW/t = kWh do período ÷ toneladas do período (divisão direta das
  // variáveis do CLP), mesma convenção já usada pro total 501+502 combinado.
  // Antes fazia média de kW/t por turno (kwhtClpMedia/kwht individuais), que
  // diverge da razão real quando um turno roda com t/h baixo e potência alta.
  const kwht = tonTotal > 0 ? parseFloat((kwhTotal / tonTotal).toFixed(2)) : null;

  const disponibilidadePerf = await calcDispPerfMedia(ln, inicio, fim);
  const disponibilidade = disponibilidadePerf.disponibilidade;
  const performance     = disponibilidadePerf.performance;
  const oee = (disponibilidade != null && performance != null)
    ? parseFloat(((disponibilidade/100) * (performance/100) * 100).toFixed(1)) : null;

  const qualidadeInfo = await calcQualidadeMedia(ln, inicio, fim);
  const oeeCompleto = (disponibilidade != null && performance != null && qualidadeInfo.qualidade != null)
    ? parseFloat(((disponibilidade/100) * (performance/100) * (qualidadeInfo.qualidade/100) * 100).toFixed(1))
    : null;

  res.json({
    linha: ln,
    periodo: { inicio, fim, horas: parseFloat(horasPeriodo.toFixed(2)), horasRodando: parseFloat(horasRodando.toFixed(2)) },
    tonTotal:  parseFloat(tonTotal.toFixed(1)),
    kwhTotal:  parseFloat(kwhTotal.toFixed(1)),
    tonHMedia,
    kwht,
    disponibilidade,
    performance,
    oee,
    qualidade:        qualidadeInfo.qualidade,
    qualidadeDias:    qualidadeInfo.diasComMedicao,
    qualidadeDiasPeriodo: qualidadeInfo.diasNoPeriodo,
    malhaMedia:       qualidadeInfo.malhaMedia,
    blaineMedia:      qualidadeInfo.blaineMedia,
    qualidadeDetalhe: qualidadeInfo.detalhe,
    oeeCompleto,
    nominalTonH: nominal,
    turnosNoPeriodo: turnosPeriodo.length,
  });
});

// Mesmos dados do /api/boletim/:linha, mas agrupados por DIA em vez de
// somados no período inteiro — usado pro detalhamento dia-a-dia do boletim
// mensal (a tabela de totais já existia; isso complementa com o "aberto"
// por dia, sem duplicar cálculo: mesma fonte, mesma convenção de turno
// noturno pertencendo ao dia em que começou às 18h).
app.get('/api/boletim/:linha/dias', (req, res) => {
  const ln = req.params.linha;
  if (!['501','502'].includes(ln)) return res.status(400).json({ erro: 'Linha inválida' });

  const agora  = Date.now();
  const inicio = parseInt(req.query.inicio) || agora - 30*86400000;
  const fim    = parseInt(req.query.fim)    || agora;
  const chaveLinha = `linha${ln}`;

  const turnosPeriodo = producao.turnos.filter(t => t.inicio >= inicio && t.inicio < fim);

  const porDia = {}; // 'DD/MM/YYYY' -> { data, tonProduzida, kwh, minRodando }
  turnosPeriodo.forEach(t => {
    const l = t[chaveLinha];
    if (!l) return;
    const dataStr = t.data; // já vem formatado 'DD/MM/YYYY' no registro do turno
    if (!porDia[dataStr]) porDia[dataStr] = { data: dataStr, ts: t.inicio, tonProduzida: 0, kwh: 0, minRodando: 0 };
    porDia[dataStr].tonProduzida += l.tonProduzida || 0;
    porDia[dataStr].kwh += l.kwh || 0;
    porDia[dataStr].minRodando += l.minRodando || 0;
    if (t.inicio < porDia[dataStr].ts) porDia[dataStr].ts = t.inicio;
  });

  const dias = Object.values(porDia).map(d => {
    // Prefere o total combinado (diurno+noturno) capturado direto do CLP
    // ([84] pra minutos, [38] pra tonelada, no reset das 06:59:55) — mais
    // confiável que somar os dois registros de turno separados. Cai na soma
    // só se ainda não tiver capturado esse dia (ex: dia muito recente, ainda
    // em andamento, ou fora da janela de 35 dias de retenção).
    const minCapturado = minTotalDiaState.historico[`${d.data}_${ln}`];
    const tonCapturado = tonTotalDiaState.historico[`${d.data}_${ln}`];
    const minRodando = minCapturado != null ? minCapturado : d.minRodando;
    const tonProduzida = tonCapturado != null ? tonCapturado : d.tonProduzida;
    const horasRodando = minRodando / 60;
    return {
      data: d.data,
      ts: d.ts,
      tonProduzida: parseFloat(tonProduzida.toFixed(1)),
      kwh: parseFloat(d.kwh.toFixed(1)),
      minRodando: Math.round(minRodando),
      fonteMinRodando: minCapturado != null ? 'CLP' : 'somado',
      fonteTonProduzida: tonCapturado != null ? 'CLP' : 'somado',
      thMedia: horasRodando > 0 ? parseFloat((tonProduzida / horasRodando).toFixed(2)) : null,
    };
  }).sort((a,b) => a.ts - b.ts);

  res.json({ linha: ln, dias });
});

// ─── Sincronização automática com o Firestore (cimento_gaucho.html) ───────
// Faz UM fetch e UM push por ciclo. Só considera turnos JÁ FECHADOS de
// producao.turnos (turno em andamento fica em turnoCorrente e não entra
// aqui) para o boletim — proposital: o turno noturno (18h–07h) atravessa a
// meia-noite, então só dá pra saber o total certo do dia depois que ele
// fecha. Agrupa por (mês, dia, moinho) usando SEMPRE a data de início do
// turno — a mesma convenção que o cimento_gaucho.html já usa pra nProd
// (turno noturno pertence ao dia em que começou às 18h, não ao dia em que
// termina).
//
// 1) Minutos rodando / kWh do dia (moinhos[dia].minM1/kwhM1/minM2/kwhM2) —
//    soma diurno+noturno fechados daquele dia; sempre sobrescreve (só
//    alimenta OEE/gráficos, não afeta estoque).
//
// 2) Produção por turno (m1[dia].dProd/dTh e nProd/nTh, idem para m2) —
//    atualiza ao vivo, igual o boletim, ENQUANTO o turno é o que está em
//    andamento (mesmo id de turnoCorrente.key): sobrescreve a cada ciclo com
//    o checkpoint mais recente de producao.turnos. Assim que o turno muda
//    (fecha de verdade), a automação para de tocar naquele dia/campo pra
//    sempre — dali em diante é só edição manual (útil pra corrigir eventual
//    divergência entre o último checkpoint e o valor final do CLP).
//    malha/Blaine/receita continuam manuais — o CLP não mede isso.
//
// 3) Sacos Gaúcho 50kg (ensacadeira[dia].gaucho50) — mesmo princípio da
//    Produção: atualiza ao vivo enquanto o dia é HOJE, com uma graça de
//    15min depois da meia-noite pra corrigir com o valor exato do
//    fechamento (sacosState.historico). Depois disso, para de tocar pra
//    sempre — só edição manual. O contador do CLP só ensaca Gaúcho 50kg,
//    sem outros produtos misturados nele.
async function sincronizarBoletimFirebase() {
  try {
    const payload = await getPayloadFirestore(true); // sempre busca fresco antes de gravar
    if (!payload || !Array.isArray(payload.months)) {
      console.warn('[SYNC] payload do Firestore vazio ou inválido — abortando.');
      return;
    }

    const limiteAntigo = Date.now() - 5 * 86400000; // só turnos dos últimos 5 dias
    const turnosRecentes = producao.turnos.filter(t => t.inicio >= limiteAntigo);

    // Agrupa por "chaveMes|dataDia" somando diurno+noturno de cada moinho
    const somaPorDia = {}; // { 'YYYY-MM|DD/MM': { min1,kwh1,min2,kwh2 } }
    turnosRecentes.forEach(t => {
      const d = new Date(t.inicio);
      const chaveMesT = `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}`;
      const dataDia   = `${String(d.getDate()).padStart(2,'0')}/${String(d.getMonth()+1).padStart(2,'0')}`;
      const key = `${chaveMesT}|${dataDia}`;
      if (!somaPorDia[key]) somaPorDia[key] = { chaveMesT, dataDia, min1: 0, kwh1: 0, min2: 0, kwh2: 0 };
      if (t.linha501) { somaPorDia[key].min1 += t.linha501.minRodando || 0; somaPorDia[key].kwh1 += t.linha501.kwh || 0; }
      if (t.linha502) { somaPorDia[key].min2 += t.linha502.minRodando || 0; somaPorDia[key].kwh2 += t.linha502.kwh || 0; }
    });

    let algumaCoisaMudou = false;

    // 1) Minutos/kWh por dia (soma dos turnos fechados daquele dia) --------
    const atualizados = [];
    for (const key in somaPorDia) {
      const s = somaPorDia[key];
      const mes = payload.months.find(m => m.key === s.chaveMesT);
      if (!mes || !Array.isArray(mes.moinhos)) continue;
      const [dia] = s.dataDia.split('/');
      const diaIndex = parseInt(dia, 10) - 1;
      if (!mes.moinhos[diaIndex]) continue;

      mes.moinhos[diaIndex].minM1 = Math.round(s.min1);
      mes.moinhos[diaIndex].kwhM1 = parseFloat(s.kwh1.toFixed(1));
      mes.moinhos[diaIndex].minM2 = Math.round(s.min2);
      mes.moinhos[diaIndex].kwhM2 = parseFloat(s.kwh2.toFixed(1));
      algumaCoisaMudou = true;
      atualizados.push(`${s.chaveMesT} ${s.dataDia}: M1=${Math.round(s.min1)}min/${s.kwh1.toFixed(1)}kWh, M2=${Math.round(s.min2)}min/${s.kwh2.toFixed(1)}kWh`);
    }
    if (atualizados.length) console.log('[SYNC] Boletim (dias com turno fechado):\n  ' + atualizados.join('\n  '));

    // 2) Produção por turno — sobrescreve ao vivo enquanto é o turno corrente;
    //    continua sobrescrevendo por mais 30 min DEPOIS da troca de turno
    //    (07:00-07:30 e 18:00-18:30) — sem essa graça, o ciclo de 15 min quase
    //    nunca cai exatamente no instante do fechamento, então o valor travava
    //    até ~15 min defasado do total real do turno (ficava faltando produção
    //    do fim do turno, ~15t a menos era o sintoma). Passada a janela, trava
    //    de vez — só edição manual dali em diante.
    function dentroJanelaGraca() {
      const agora = horaCLP().data;
      const hm = agora.getHours() * 60 + agora.getMinutes();
      return (hm >= 7*60 && hm < 7*60 + 30) || (hm >= 18*60 && hm < 18*60 + 30);
    }
    const turnosNaoCorrentes = turnosRecentes.filter(t => t.id !== turnoCorrente.key);
    const ultimoFechado = turnosNaoCorrentes.length
      ? turnosNaoCorrentes.reduce((a, b) => (b.inicio > a.inicio ? b : a))
      : null;
    const naJanelaGraca = dentroJanelaGraca();

    const preenchidos = [];
    for (const t of turnosRecentes) {
      const éCorrente = t.id === turnoCorrente.key;
      const éUltimoFechadoEmGraca = !éCorrente && ultimoFechado && t.id === ultimoFechado.id && naJanelaGraca;
      if (!éCorrente && !éUltimoFechadoEmGraca) continue; // já fechou e passou da graça — não mexe mais, só manual

      const d = new Date(t.inicio);
      const chaveMesT = `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}`;
      const dataDia   = `${String(d.getDate()).padStart(2,'0')}/${String(d.getMonth()+1).padStart(2,'0')}`;
      const mes = payload.months.find(m => m.key === chaveMesT);
      if (!mes) continue;

      const isDiurno   = t.turno === 'diurno';
      const fieldProd  = isDiurno ? 'dProd' : 'nProd';
      const fieldTh    = isDiurno ? 'dTh'   : 'nTh';

      [['m1', t.linha501], ['m2', t.linha502]].forEach(([chaveM, l]) => {
        if (!l || l.tonProduzida == null) return;
        if (!Array.isArray(mes[chaveM])) return;
        const row = mes[chaveM].find(r => r.data === dataDia);
        if (!row) return;

        row[fieldProd] = l.tonProduzida;
        row[fieldTh]   = l.thMedia > 0 ? l.thMedia : null;
        algumaCoisaMudou = true;
        preenchidos.push(`${chaveMesT} ${dataDia} ${chaveM} ${t.turno} (${éCorrente ? 'ao vivo' : 'graça pós-fechamento'}): ${fieldProd}=${l.tonProduzida}t ${fieldTh}=${row[fieldTh]}`);
      });
    }
    if (preenchidos.length) console.log('[SYNC] Produção (turno em andamento):\n  ' + preenchidos.join('\n  '));

    // 3) Sacos Gaúcho 50kg (ensacadeira) — ao vivo enquanto o dia é HOJE. O
    //    contador do CLP (COUNTER_700RF001_U01) só ensaca Gaúcho 50kg —
    //    confirmado, sem ambiguidade de produto (diferente da produção, que
    //    tem 3 SKUs na mesma linha). Mesmo princípio da Produção: para de
    //    tocar o dia assim que ele fecha (aqui a virada é sempre à meia-noite,
    //    não em dois horários como o turno). Uma graça de 15min depois da
    //    meia-noite usa o valor EXATO capturado por registrarSacosExpedidos()
    //    (sacosState.historico) pro dia que acabou de fechar — mais preciso
    //    que o último "ao vivo" antes da virada, que pode ter perdido até
    //    ~15min de sacos embalados entre o último ciclo de sync e o reset
    //    real do CLP às 23:59:55.
    const hojeCLPData  = horaCLP().data;
    const chaveMesHoje = `${hojeCLPData.getFullYear()}-${String(hojeCLPData.getMonth()+1).padStart(2,'0')}`;
    const diaHojeIdx    = hojeCLPData.getDate() - 1;
    const mesHoje = payload.months.find(m => m.key === chaveMesHoje);
    if (mesHoje && Array.isArray(mesHoje.ensacadeira) && mesHoje.ensacadeira[diaHojeIdx] && sacosState.ultimoValor != null) {
      mesHoje.ensacadeira[diaHojeIdx].gaucho50 = sacosState.ultimoValor;
      algumaCoisaMudou = true;
      console.log(`[SYNC] Sacos Gaúcho 50kg (hoje, ao vivo): ${sacosState.ultimoValor} sc`);
    }
    const minutosDesdeMeiaNoite = hojeCLPData.getHours()*60 + hojeCLPData.getMinutes();
    if (minutosDesdeMeiaNoite < 15 && sacosState.historico.length) {
      const ontem = new Date(hojeCLPData); ontem.setDate(ontem.getDate() - 1);
      const chaveMesOntem = `${ontem.getFullYear()}-${String(ontem.getMonth()+1).padStart(2,'0')}`;
      const diaOntemIdx   = ontem.getDate() - 1;
      const dataOntemStr  = `${String(ontem.getDate()).padStart(2,'0')}/${String(ontem.getMonth()+1).padStart(2,'0')}/${ontem.getFullYear()}`;
      const registroOntem = sacosState.historico.find(h => h.data === dataOntemStr);
      if (registroOntem) {
        const mesOntem = chaveMesOntem === chaveMesHoje ? mesHoje : payload.months.find(m => m.key === chaveMesOntem);
        if (mesOntem && Array.isArray(mesOntem.ensacadeira) && mesOntem.ensacadeira[diaOntemIdx]) {
          mesOntem.ensacadeira[diaOntemIdx].gaucho50 = registroOntem.total;
          algumaCoisaMudou = true;
          console.log(`[SYNC] Sacos Gaúcho 50kg (fechamento, graça pós-meia-noite): ${dataOntemStr} = ${registroOntem.total} sc`);
        }
      }
    }

    if (!algumaCoisaMudou) { console.log('[SYNC] Nada a atualizar neste ciclo.'); return; }

    const ok = await pushPayloadFirestore(payload);
    if (!ok) console.error('[SYNC] falha ao gravar no Firestore.');
  } catch (e) {
    console.error('[SYNC] erro inesperado:', e.message);
  }
}

app.get('/api/indicadores/:linha', async (req, res) => {
  const ln = req.params.linha;
  if (!['501','502'].includes(ln)) return res.status(400).json({ erro: 'Linha inválida' });

  const agora  = Date.now();
  const inicio = parseInt(req.query.inicio) || agora - 86400000;
  const fim    = parseInt(req.query.fim)    || agora;
  const horas  = (fim - inicio) / 3600000;

  // Tags
  const ID_POT      = `ea_${ln}bm140_pot_total`; // Potência TOTAL (motor+separador+exaustor+20kW), já somada pelo CLP (AJUSTE[11])
  const ID_POT_MOTOR= `ea_${ln}bm140_jt_u01`;    // Potência SÓ do motor (AJUSTE[9]) — só pra exibição em "atual"
  const ID_TON      = `totalizador_${ln}`;         // t/h média          (TOTALIZADOR[37]/[31])
  const ID_DB       = `ea_${ln}bm140_db_u01`;     // Decibelímetro
  const ID_IT       = `ea_${ln}bm140_it_u01`;     // Corrente motor
  const ID_KWHT     = `tot${ln}_kwht_clp`;         // kW/t CLP           (TOTALIZADOR[47]/[39])
  const ID_KWH_ACC  = `ea_${ln}bm140_kwh_turno`;   // kWh acumulado do turno, direto do CLP (AJUSTE[13])

  function amostras(id) {
    return (db.amostras[id] || []).filter(r => r.ts >= inicio && r.ts <= fim);
  }

  const aPot     = amostras(ID_POT);
  const aTon     = amostras(ID_TON);
  const aKwhtClp = amostras(ID_KWHT);
  const aKwhAcc  = amostras(ID_KWH_ACC);

  // Reconstitui o total de kWh consumido ENTRE as amostras de um acumulador
  // que reseta periodicamente (AJUSTE[13], duas vezes por dia) — soma só os
  // INCREMENTOS entre amostras consecutivas, e quando o valor CAI (reset no
  // meio do período pedido) soma o valor novo inteiro (já é o acumulado
  // desde o reset). Importante: NÃO soma o valor absoluto da 1ª amostra —
  // isso incluiria energia acumulada ANTES do início do período pedido,
  // já que o acumulador só zera no turno, não no instante "inicio" pedido aqui.
  function reconstituirKwhAcumulado(amostrasAcc) {
    if (amostrasAcc.length < 2) return null; // precisa de pelo menos 2 amostras pra medir um incremento
    let total = 0;
    for (let i = 1; i < amostrasAcc.length; i++) {
      total += amostrasAcc[i].v >= amostrasAcc[i-1].v
        ? (amostrasAcc[i].v - amostrasAcc[i-1].v)
        : amostrasAcc[i].v;
    }
    return parseFloat(total.toFixed(1));
  }

  // ── Percorre pares de amostras de potência (só usado como FALLBACK) ───────
  // Preferência: reconstituir a partir do acumulador nativo do CLP acima.
  // Cai aqui só se não houver amostras de AJUSTE[13] no período (ex: tag
  // adicionada há pouco tempo, sem histórico de 7 dias ainda acumulado).
  let tRodandoH = 0;
  let tTotalH   = 0;
  let kwhCalc   = 0;
  for (let i = 1; i < aPot.length; i++) {
    const dt     = (aPot[i].ts - aPot[i-1].ts) / 3600000;
    tTotalH     += dt;
    if (aPot[i-1].v > 50) {
      tRodandoH += dt;
      kwhCalc   += ((aPot[i].v + aPot[i-1].v) / 2) * dt; // ID_POT já é a potência TOTAL, sem somar CF de novo
    }
  }
  const kwhReconstituido = reconstituirKwhAcumulado(aKwhAcc);
  const kwhTotal  = kwhReconstituido ?? parseFloat(kwhCalc.toFixed(1));
  const fonteKwh  = kwhReconstituido != null ? 'CLP' : 'calculado';
  const horasRodando = tRodandoH;

  // ── Potência média total — já vem somada do CLP, só quando rodando ────────
  const aPotRodando   = aPot.filter(r => r.v > 50);
  const potMediaTotal = aPotRodando.length
    ? parseFloat((aPotRodando.reduce((s, r) => s + r.v, 0) / aPotRodando.length).toFixed(1))
    : null;
  const potMedia = potMediaTotal;

  // ── t/h média — somente amostras com moinho rodando ───────────────────────
  const aTonRodando = aTon.filter(r => {
    const p = aPot.find(p => Math.abs(p.ts - r.ts) < 10000);
    return p ? p.v > 50 : false;
  });
  const tonHMedia = aTonRodando.length
    ? parseFloat((aTonRodando.reduce((s, r) => s + r.v, 0) / aTonRodando.length).toFixed(2))
    : null;

  // ── Toneladas = t/h_médio(rodando) × horas_rodando ────────────────────────
  const tonTotal = (tonHMedia != null && horasRodando > 0)
    ? parseFloat((tonHMedia * horasRodando).toFixed(1)) : null;

  // ── kW/t = kWh do período ÷ toneladas do período (divisão direta das
  // variáveis do CLP), mesma convenção do /api/boletim. kwhtClpMedia (média
  // das amostras do tag TOTALIZADOR[47]/[39] enquanto rodando) fica só como
  // referência de diagnóstico — não é mais o valor principal, porque média
  // de kW/t instantâneo por amostra diverge da razão real quando o t/h cai
  // e a potência continua alta (partida, oscilação de alimentação).
  const aKwhtClpRodando = aKwhtClp.filter(r => {
    const p = aPot.find(p => Math.abs(p.ts - r.ts) < 10000);
    return p ? p.v > 50 : false;
  });
  const kwhtClpMedia = aKwhtClpRodando.length > 0
    ? parseFloat((aKwhtClpRodando.reduce((s, r) => s + r.v, 0) / aKwhtClpRodando.length).toFixed(2))
    : (potMediaTotal != null && tonHMedia != null && tonHMedia > 0)
      ? parseFloat((potMediaTotal / tonHMedia).toFixed(2))
      : null;
  const kwht = (tonTotal != null && tonTotal > 0) ? parseFloat((kwhTotal / tonTotal).toFixed(2)) : null;

  // ── Disponibilidade ────────────────────────────────────────────────────────
  const disponibilidade = tTotalH > 0
    ? parseFloat(((tRodandoH / tTotalH) * 100).toFixed(1)) : null;

  // ── Performance ────────────────────────────────────────────────────────────
  // T/h nominal do EQUIPAMENTO (NOMINAL_MOINHO_TH, fixo, ver definição
  // acima), não mais configurável por query param — consistente com
  // /api/boletim e o index.html.
  const nominalTonH = NOMINAL_MOINHO_TH;
  const performance = tonHMedia != null
    ? parseFloat(Math.min(100, (tonHMedia / nominalTonH) * 100).toFixed(1)) : null;

  // ── OEE ───────────────────────────────────────────────────────────────────
  const oee = (disponibilidade != null && performance != null)
    ? parseFloat(((disponibilidade / 100) * (performance / 100) * 100).toFixed(1)) : null;

  // ── Temperatura mancais ────────────────────────────────────────────────────
  const mancalIds = ln === '501'
    ? ['ea_501bm140_tt_u04','ea_501bm140_tt_u05','ea_501bm140_tt_u06','ea_501bm140_tt_u07']
    : ['ea_502bm140_tt_u04','ea_502bm140_tt_u05','ea_502bm140_tt_u06','ea_502bm140_tt_u07'];
  const tempsMancal     = mancalIds.map(id => dadosAtuais[id]?.valor).filter(v => v != null);
  const tempMancalMedia = tempsMancal.length
    ? tempsMancal.reduce((a, b) => a + b, 0) / tempsMancal.length : null;
  const tempMancalMax   = tempsMancal.length ? Math.max(...tempsMancal) : null;

  // ── Histórico horário — kWh via acumulador do CLP (com trapézio de fallback) ─
  const historicoHorario = [];
  const MS_HORA = 3600000;
  for (let t = inicio; t < fim; t += MS_HORA) {
    const tFim  = Math.min(t + MS_HORA, fim);
    const potsH = (db.amostras[ID_POT] || []).filter(r => r.ts >= t && r.ts < tFim);
    const tonsH = (db.amostras[ID_TON] || []).filter(r => r.ts >= t && r.ts < tFim);
    if (!potsH.length) continue;

    const kwhAccH = (db.amostras[ID_KWH_ACC] || []).filter(r => r.ts >= t && r.ts < tFim);
    const kwhReconstH = reconstituirKwhAcumulado(kwhAccH);

    let kwhCalcH = 0, hRod = 0;
    for (let i = 1; i < potsH.length; i++) {
      const dt = (potsH[i].ts - potsH[i-1].ts) / 3600000;
      if (potsH[i-1].v > 50) {
        hRod += dt;
        kwhCalcH += ((potsH[i].v + potsH[i-1].v) / 2) * dt; // ID_POT já é potência total
      }
    }
    const kwhH = kwhReconstH ?? kwhCalcH;

    const potsRod     = potsH.filter(r => r.v > 50);
    const potMedTotal = potsRod.length
      ? parseFloat((potsRod.reduce((s, r) => s + r.v, 0) / potsRod.length).toFixed(1)) : null;

    const tonsRod = tonsH.filter(r => {
      const p = potsH.find(p => Math.abs(p.ts - r.ts) < 10000);
      return p ? p.v > 50 : false;
    });
    const tonhM = tonsRod.length
      ? parseFloat((tonsRod.reduce((s, r) => s + r.v, 0) / tonsRod.length).toFixed(2)) : null;

    // kW/t por hora: usar amostras do CLP (tag kwhtClp) quando moinho rodando
    // Fallback: potMedTotal / tonhM se não houver amostras CLP suficientes
    const kwhtClpH = (db.amostras[ID_KWHT] || []).filter(r => r.ts >= t && r.ts < tFim);
    const kwhtClpHrod = kwhtClpH.filter(r => {
      const p = potsH.find(p => Math.abs(p.ts - r.ts) < 10000);
      return p ? p.v > 50 : false;
    });
    const kwhtH = kwhtClpHrod.length > 0
      ? parseFloat((kwhtClpHrod.reduce((s, r) => s + r.v, 0) / kwhtClpHrod.length).toFixed(2))
      : (potMedTotal != null && tonhM != null && tonhM > 0)
        ? parseFloat((potMedTotal / tonhM).toFixed(2))
        : null;

    historicoHorario.push({
      ts:   t,
      kwh:  parseFloat(kwhH.toFixed(1)),
      tonh: tonhM,
      kwht: kwhtH,       // kW/t: média tag CLP por hora (ou potMedTotal/tonhM como fallback)
      pot:  potMedTotal,
      hRod: parseFloat(hRod.toFixed(3)),
    });
  }

  // ── Valores atuais ─────────────────────────────────────────────────────────
  const potAtual = dadosAtuais[ID_POT]?.valor ?? null;
  const atual = {
    pot:      dadosAtuais[ID_POT_MOTOR]?.valor ?? null, // só o motor, informativo
    potTotal: potAtual,                                  // já vem somado do CLP (AJUSTE[11])
    tonh:     dadosAtuais[ID_TON]?.valor  ?? null,
    db:       dadosAtuais[ID_DB]?.valor   ?? null,
    corr:     dadosAtuais[ID_IT]?.valor   ?? null,
    kwht:     dadosAtuais[ID_KWHT]?.valor ?? null, // kW/t instantâneo do CLP
    kwhTurno: dadosAtuais[ID_KWH_ACC]?.valor ?? null, // kWh acumulado do turno em andamento, ao vivo
  };

  const qualidadeInfo = await calcQualidadeMedia(ln, inicio, fim);
  const oeeCompleto = (disponibilidade != null && performance != null && qualidadeInfo.qualidade != null)
    ? parseFloat(((disponibilidade/100) * (performance/100) * (qualidadeInfo.qualidade/100) * 100).toFixed(1))
    : null;

  res.json({
    linha:  ln,
    periodo: {
      inicio, fim,
      horas:         parseFloat(horas.toFixed(2)),
      horasRodando:  parseFloat(horasRodando.toFixed(2)),
    },
    atual,
    potMedia,          // média(potência total, já somada pelo CLP) quando rodando
    kwhTotal,          // kWh do período: reconstituído do acumulador do CLP (fonteKwh='CLP'), ou trapézio calculado como fallback
    fonteKwh,          // 'CLP' | 'calculado'
    tonHMedia,         // t/h médio quando rodando
    tonTotal,          // tonHMedia × horasRodando
    kwht,              // kW/t do CLP (média quando rodando)
    kwhtClpMedia,      // idem (alias explícito)
    disponibilidade,
    performance,
    oee,               // OEE parcial (Disponibilidade × Performance) — mantido por compatibilidade
    qualidade:        qualidadeInfo.qualidade,        // % (0-100), null se não há medição de malha/Blaine no período
    qualidadeDias:    qualidadeInfo.diasComMedicao,    // quantos dias do período tinham malha/Blaine lançado
    qualidadeDiasPeriodo: qualidadeInfo.diasNoPeriodo, // quantos dias do período existem registro no Firestore (com ou sem medição)
    malhaMedia:       qualidadeInfo.malhaMedia,  // % retido médio no período (todas as medições, diurno+noturno)
    blaineMedia:      qualidadeInfo.blaineMedia, // Blaine médio (cm²/g) no período
    qualidadeDetalhe: qualidadeInfo.detalhe,     // 1 item por dia com medição — data, receita e malha/Blaine por turno
    oeeCompleto,       // OEE de 3 fatores (Disponibilidade × Performance × Qualidade) — null se qualidade indisponível
    tempMancalMedia:   tempMancalMedia != null ? parseFloat(tempMancalMedia.toFixed(1)) : null,
    tempMancalMax:     tempMancalMax   != null ? parseFloat(tempMancalMax.toFixed(1))   : null,
    nominalTonH,
    historicoHorario,
  });
});

// ─── API Status Digitais ────────────────────────────────────────────────────
app.get('/api/status', (req, res) => {
  const r = {};
  DINT_TAGS.forEach(t => { r[t.id] = { ...t, ...dadosDigitais[t.id] }; });
  res.json(r);
});

// Snapshot WS inclui digitais
// (já enviado via broadcast individual, mas incluir no snapshot inicial)

// ─── EtherNet/IP ──────────────────────────────────────────────────────────────
// Timeout por leitura para não travar o ciclo se o CLP não responder
function lerDintComTimeout(PLC, dt, timeoutMs = 1000) {
  return new Promise(resolve => {
    const timer = setTimeout(() => resolve(null), timeoutMs);
    const tag = new Tag(dt.tagName);
    PLC.readTag(tag).then(() => {
      clearTimeout(timer);
      resolve(tag.value != null ? parseInt(tag.value) : null);
    }).catch(() => { clearTimeout(timer); resolve(null); });
  });
}

function lerTagComTimeout(PLC, cfg, timeoutMs = 1500) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => resolve(false), timeoutMs);
    const tag = new Tag(cfg.tagName);
    PLC.readTag(tag).then(() => {
      clearTimeout(timer);
      if (tag.value != null) resolve(tag.value);
      else resolve(false);
    }).catch(e => {
      clearTimeout(timer);
      // Propagar Micro Queue para o ciclo tratar reconexão
      if (e && e.message && e.message.includes('Micro Queue')) reject(e);
      else resolve(false);
    });
  });
}

// ── Coleta de Processo — snapshot a cada 5 minutos ──────────────────────────
const COLETA_FILE_CORRENTE  = path.join(__dirname, 'coleta_processo_corrente.json');
const COLETA_FILE_ANTERIOR  = path.join(__dirname, 'coleta_processo_anterior.json');

function mesAtualColeta() {
  const now = horaCLP().data;
  return `${now.getFullYear()}-${String(now.getMonth()+1).padStart(2,'0')}`;
}

let coletaCorrente = { mes: mesAtualColeta(), registros: [] };
try {
  const raw = JSON.parse(fs.readFileSync(COLETA_FILE_CORRENTE, 'utf8'));
  if (raw.mes === mesAtualColeta()) coletaCorrente = raw;
  else {
    // Mês virou — salvar como anterior e reiniciar
    fs.writeFileSync(COLETA_FILE_ANTERIOR, JSON.stringify(raw, null, 2), 'utf8');
    console.log('[COLETA] Novo mês — arquivo anterior preservado.');
  }
} catch(e) {}

// Cache em memória do mês anterior — antes isso era lido do disco de forma
// SÍNCRONA a cada nova conexão WebSocket (toda vez que alguém abria/
// recarregava a página mobile, ou reconectava por rede instável). Com
// vários clientes reconectando (ou reconexões em rajada após uma queda de
// rede), isso multiplicava leituras de arquivo síncronas de tamanho
// crescente — candidato forte pra travamentos que não seguem um padrão fixo
// de tempo. Agora lido uma vez no boot e atualizado só quando o mês vira.
let coletaAnteriorCache = null;
try { coletaAnteriorCache = JSON.parse(fs.readFileSync(COLETA_FILE_ANTERIOR, 'utf8')); } catch(e) {}

// Assíncrona: coletaCorrente.registros cresce o mês inteiro (chega a
// alguns MB perto do fim do mês) e isso roda a cada 5 min — escrita
// síncrona bloquearia o event loop por tempo crescente ao longo do mês,
// mesma classe de bug já corrigida em outros pontos.
let _salvandoColetaAsync = false;
function salvarColeta() {
  if (_salvandoColetaAsync) return;
  _salvandoColetaAsync = true;
  fs.writeFile(COLETA_FILE_CORRENTE, JSON.stringify(coletaCorrente), 'utf8', (err) => {
    _salvandoColetaAsync = false;
    if (err) console.error('[COLETA] Erro ao salvar (async):', err.message);
  });
}

function g(id) { return dadosAtuais[id]?.valor ?? null; }

function registrarColeta() {
  const mes = mesAtualColeta();
  if (coletaCorrente.mes !== mes) {
    // Virada de mês — acontece no máximo 1x por mês, escrita síncrona aqui não é problema
    try { fs.writeFileSync(COLETA_FILE_ANTERIOR, JSON.stringify(coletaCorrente, null, 2), 'utf8'); } catch(e) {}
    coletaAnteriorCache = coletaCorrente; // atualiza o cache em memória usado nas conexões WS
    coletaCorrente = { mes, registros: [] };
    console.log(`[COLETA] Novo mês: ${mes}`);
  }

  const now = horaCLP().data;
  const reg = {
    ts:   now.toISOString(),
    data: now.toLocaleDateString('pt-BR'),
    hora: now.getHours(),
    min:  now.getMinutes(),
    // Moinho 501
    pot501:       g('ea_501bm140_jt_u01'),      // potência SÓ do motor (AJUSTE[9])
    potTotal501:  g('ea_501bm140_pot_total'),   // potência TOTAL: motor+separador+exaustor+20kW (AJUSTE[11])
    kwhTurno501:  g('ea_501bm140_kwh_turno'),   // kWh acumulado do turno em andamento, direto do CLP (AJUSTE[13])
    pfiltro501: g('ea_501bf040_pt_u01'),
    tsaida501:  g('ea_501bm140_tt_u09'),
    th_wf005_501: g('th_501wf005'),
    th_wf010_501: g('th_501wf010'),
    tmnh_la501:   g('ea_501bm140_tt_u06'),
    tmnh_loa501:  g('ea_501bm140_tt_u07'),
    tmtr_la501:   g('ea_501bm140_tt_u04'),
    tmtr_loa501:  g('ea_501bm140_tt_u05'),
    fsep501:    g('ea_501md070_st_u01'),
    fexh501:    g('ea_rede_501fn042_v'),
    ielev501:   g('ea_rede_501be060_c'),
    retorno501: g('ea_501bm140_wt_u01'),
    // Moinho 502
    potTotal502:  g('ea_502bm140_pot_total'),   // potência TOTAL: motor+separador+exaustor+20kW (AJUSTE[11])
    kwhTurno502:  g('ea_502bm140_kwh_turno'),   // kWh acumulado do turno em andamento, direto do CLP (AJUSTE[13])
    pfiltro502: g('ea_502bf040_pt_u01'),
    tsaida502:  g('ea_502bm140_tt_u09'),
    th_wf005_502: g('th_502wf005'),
    th_wf010_502: g('th_502wf010'),
    tmnh_la502:   g('ea_502bm140_tt_u06'),
    tmnh_loa502:  g('ea_502bm140_tt_u07'),
    tmtr_la502:   g('ea_502bm140_tt_u04'),
    tmtr_loa502:  g('ea_502bm140_tt_u05'),
    fsep502:    g('ea_502md070_st_u01'),
    fexh502:    g('ea_rede_502fn042_v'),
    ielev502:   g('ea_rede_502be060_c'),
    retorno502: g('ea_502bm140_wt_u01'),
    // Ensacadeira / Expedição
    sacosExpedidos: g('ct_700rf001_sacos'), // contador do dia (zera à meia-noite no CLP)
  };

  // Evitar duplicatas — checar se já existe registro para data+hora+min
  const jaExiste = coletaCorrente.registros.some(
    r => r.data === reg.data && r.hora === reg.hora && r.min === reg.min
  );
  if (jaExiste) {
    console.log(`[COLETA] Duplicata ignorada — ${reg.data} ${String(reg.hora).padStart(2,'0')}:${String(reg.min).padStart(2,'0')}`);
    return;
  }

  coletaCorrente.registros.push(reg);
  salvarColeta();
  broadcast({ tipo: 'coleta_novo_registro', registro: reg });
  console.log(`[COLETA] Registro salvo — ${reg.data} ${String(reg.hora).padStart(2,'0')}:${String(reg.min).padStart(2,'0')}`);
}

// ── Nomes dos produtos (STRING) — lidos a cada 30s e broadcast para tela PA ──
const TAGS_NOMES = [
  { ln:'501', i:1, tagName:'NOME_PRODUTO01_501' },
  { ln:'501', i:2, tagName:'NOME_PRODUTO02_501' },
  { ln:'501', i:3, tagName:'NOME_PRODUTO03_501' },
  { ln:'501', i:4, tagName:'NOME_PRODUTO04_501' },
  { ln:'501', i:5, tagName:'NOME_PRODUTO05_501' },
  { ln:'501', i:6, tagName:'NOME_PRODUTO06_501' },
  { ln:'501', i:7, tagName:'NOME_PRODUTO07_501' },
  { ln:'501', i:8, tagName:'NOME_PRODUTO08_501' },
  { ln:'501', i:9, tagName:'NOME_PRODUTO09_501' },
  { ln:'502', i:1, tagName:'NOME_PRODUTO01_502' },
  { ln:'502', i:2, tagName:'NOME_PRODUTO02_502' },
  { ln:'502', i:3, tagName:'NOME_PRODUTO03_502' },
  { ln:'502', i:4, tagName:'NOME_PRODUTO04_502' },
  { ln:'502', i:5, tagName:'NOME_PRODUTO05_502' },
  { ln:'502', i:6, tagName:'NOME_PRODUTO06_502' },
  { ln:'502', i:7, tagName:'NOME_PRODUTO07_502' },
  { ln:'502', i:8, tagName:'NOME_PRODUTO08_502' },
  { ln:'502', i:9, tagName:'NOME_PRODUTO09_502' },
];

let nomesCache = { 501: Array(9).fill(''), 502: Array(9).fill('') };

async function lerNomesProdutos(PLC) {
  try {
    const nomes = { 501: [...nomesCache[501]], 502: [...nomesCache[502]] };
    for (const nt of TAGS_NOMES) {
      const tag = new Tag(nt.tagName);
      try {
        await PLC.readTag(tag);
        const raw = tag.value;
        // STRING do CLP: objeto com .value (string) ou string direta
        const str = typeof raw === 'string' ? raw
          : (raw?.value ?? raw?.toString() ?? '');
        // Remover caracteres nulos e de controle
        nomes[parseInt(nt.ln)][nt.i - 1] = str.replace(/[\x00-\x1F\x7F]/g, '').trim();
      } catch(e) { /* tag indisponível, manter valor anterior */ }
    }
    nomesCache = nomes;
    broadcast({ tipo: 'nomes_produtos', nomes });
  } catch(e) {
    console.warn('[PA] Erro ao ler nomes produtos:', e.message);
  }
}

async function lerTodosOsTags(PLC) {
  const ts = Date.now(); let ok = 0, erros = 0;

  // Leitura sequencial — st-ethernet-ip nao suporta leituras paralelas
  // na mesma conexao PLC (compartilham o mesmo socket TCP interno)
  for (const cfg of TAGS_CONFIG) {
    const raw = await lerTagComTimeout(PLC, cfg, 1500);
    if (raw !== false) {
      const valor = parseFloat(Number(raw).toFixed(2));
      const al = processarAlarme(cfg, valor, ts);
      dadosAtuais[cfg.id] = { valor, qualidade: 'Good', ts, alarme: al };
      registrarLeitura(cfg.id, valor, ts);
      broadcast({ tipo: 'atualizacao', tagId: cfg.id, valor, qualidade: 'Good', ts, alarme: al });
      ok++;
    } else { erros++; }
  }

  // Leitura DINT sequencial
  for (const dt of DINT_TAGS) {
    const raw = await lerDintComTimeout(PLC, dt, 1000);
    if (raw !== null) {
      const isLQ = !!(dt.isLQ);
      const parsed = parseDint(raw, isLQ, dt.bit);
      dadosDigitais[dt.id] = { ...parsed, ts };
      broadcast({ tipo: 'digital', id: dt.id, ...parsed, ts });
    }
  }


  registrarSacosExpedidos(dadosAtuais['ct_700rf001_sacos']?.valor ?? null, ts);
  registrarEnergiaClp('501', dadosAtuais['ea_501bm140_kwh_turno']?.valor ?? null);
  registrarEnergiaClp('502', dadosAtuais['ea_502bm140_kwh_turno']?.valor ?? null);
  registrarMinTotalDia('501', dadosAtuais['tot501_min_total_dia']?.valor ?? null);
  registrarMinTotalDia('502', dadosAtuais['tot502_min_total_dia']?.valor ?? null);
  registrarTonTotalDia('501', dadosAtuais['tot501_ton_total']?.valor ?? null);
  registrarTonTotalDia('502', dadosAtuais['tot502_ton_total']?.valor ?? null);
  registrarAmostraTurno(ts);
  console.log(`[CLP] ${new Date(ts).toLocaleTimeString('pt-BR')} — ${ok} OK / ${erros} erros — ${Date.now()-ts}ms — relogio=${horaCLP().fonte}`);
  return { ok, erros };
}

// ─── Sistema de Produção por Turnos ──────────────────────────────────────────
// Turnos conforme lógica do CLP (do L5K):
//   DIURNO:  07:00 → 18:00  (reset CLP às 06:59:55)
//   NOTURNO: 18:00 → 07:00  (CLP salva 2º turno às 18:01:57)
const PROD_FILE = path.join(__dirname, 'producao.json');
const TURNO_FILE = path.join(__dirname, 'turno_corrente.json');

let producao = { turnos: [] };
if (fs.existsSync(PROD_FILE)) {
  try { producao = JSON.parse(fs.readFileSync(PROD_FILE, 'utf8')); } catch(e) {}
}
if (producao.turnoCorrente) delete producao.turnoCorrente; // migração: não fica mais aqui (ver TURNO_FILE)

// Turno em andamento salvo antes de um restart anterior (se houver) — usado
// em iniciarTurnoCorrente() pra restaurar as amostras acumuladas em vez de
// começar do zero, especificamente pra não perder o cálculo de energia
// (kWh), que não tem totalizador no CLP e depende 100% dessas amostras.
// FICA EM ARQUIVO PRÓPRIO (não dentro de producao.json) — os arrays de
// amostras crescem o turno inteiro (até ~13 mil por linha em 11h), e
// producao.json é salvo de forma SÍNCRONA a cada ~1min pelo checkpoint de
// fecharTurnoCorrente(); se turnoCorrente estivesse junto nesse objeto, esse
// salvamento síncrono ficaria cada vez mais lento ao longo do turno e
// bloqueava o event loop — foi a causa dos travamentos mesmo depois da
// primeira correção (que só resolveu o salvamento de alta frequência, não
// esse checkpoint de 1 min).
let turnoCorrenteRestaurar = null;
if (fs.existsSync(TURNO_FILE)) {
  try { turnoCorrenteRestaurar = JSON.parse(fs.readFileSync(TURNO_FILE, 'utf8')); } catch(e) {}
}
function salvarProducao() {
  try { fs.writeFileSync(PROD_FILE, JSON.stringify(producao), 'utf8'); } catch(e) {}
}
// Versão não-bloqueante — usada nos pontos de alta frequência (a cada poucas
// amostras do CLP) pra não travar o event loop do Node enquanto escreve.
let _salvandoProducaoAsync = false;
function salvarProducaoAsync() {
  if (_salvandoProducaoAsync) return; // já tem uma escrita em andamento — pula esse ciclo, o próximo pega o estado atualizado
  _salvandoProducaoAsync = true;
  fs.writeFile(PROD_FILE, JSON.stringify(producao), 'utf8', (err) => {
    _salvandoProducaoAsync = false;
    if (err) console.warn('[PROD] Falha ao salvar producao.json (async):', err.message);
  });
}
let _salvandoTurnoAsync = false;
function salvarTurnoCorrenteAsync() {
  if (_salvandoTurnoAsync) return;
  _salvandoTurnoAsync = true;
  fs.writeFile(TURNO_FILE, JSON.stringify(turnoCorrente), 'utf8', (err) => {
    _salvandoTurnoAsync = false;
    if (err) console.warn('[TURNO] Falha ao salvar turno_corrente.json (async):', err.message);
  });
}

// ─── Sacos expedidos (ensacadeira) — total diário confiável ─────────────────
// O contador COUNTER_700RF001_U01.ACC zera sozinho no CLP à meia-noite
// (23:59:55). Amostras genéricas (1/min, 7 dias) podem perder o valor exato
// bem antes do reset — aqui detectamos o próprio reset (valor cai) e
// guardamos o ÚLTIMO valor visto antes de cair como o total definitivo
// daquele dia, sem depender de acertar o timing de uma amostra solta.
const SACOS_FILE = path.join(__dirname, 'sacos.json');
let sacosState = { ultimoValor: null, historico: [] }; // historico: [{data:'DD/MM/YYYY', total, ts}]
if (fs.existsSync(SACOS_FILE)) {
  try { sacosState = JSON.parse(fs.readFileSync(SACOS_FILE, 'utf8')); } catch(e) {}
}
let _salvandoSacosAsync = false;
function salvarSacosAsync() {
  if (_salvandoSacosAsync) return;
  _salvandoSacosAsync = true;
  fs.writeFile(SACOS_FILE, JSON.stringify(sacosState), 'utf8', (err) => {
    _salvandoSacosAsync = false;
    if (err) console.warn('[SACOS] Falha ao salvar sacos.json:', err.message);
  });
}
function registrarSacosExpedidos(valor, ts) {
  if (valor == null) return;
  if (sacosState.ultimoValor != null && valor < sacosState.ultimoValor) {
    // Contador caiu — o CLP resetou. O último valor visto era o total do
    // dia que terminou. Se a queda foi detectada de madrugada (reset normal
    // à meia-noite), o total pertence ao dia ANTERIOR à data de agora.
    const agora = horaCLP().data;
    const dataFechamento = new Date(agora);
    if (agora.getHours() < 2) dataFechamento.setDate(dataFechamento.getDate() - 1);
    const dataStr = `${String(dataFechamento.getDate()).padStart(2,'0')}/${String(dataFechamento.getMonth()+1).padStart(2,'0')}/${dataFechamento.getFullYear()}`;
    sacosState.historico = sacosState.historico.filter(h => h.data !== dataStr); // evita duplicar se já tinha algo pra esse dia
    sacosState.historico.unshift({ data: dataStr, total: sacosState.ultimoValor, ts });
    if (sacosState.historico.length > 90) sacosState.historico.splice(90);
    console.log(`[SACOS] Dia fechado: ${dataStr} = ${sacosState.ultimoValor} sacos`);
  }
  if (valor !== sacosState.ultimoValor) {
    sacosState.ultimoValor = valor;
    salvarSacosAsync(); // só grava quando o valor realmente muda — não a cada ciclo de 3s
  }
}
app.get('/api/sacos/historico', (req, res) => {
  res.json({ atual: sacosState.ultimoValor, historico: sacosState.historico });
});

// Relógio do CLP (LocalDateTime, GSV WallClockTime da Rockwell) — usado como
// fonte de verdade pra tudo que decide turno/dia (turnoAtual, dataInicioTurno,
// janela de graça, reset de sacos, coleta de processo), em vez do relógio do
// Node.js. Elimina risco de desalinhamento entre o relógio do servidor e o
// relógio real do controlador (drift de NTP, fuso, servidor com hora errada
// após restart, etc.) bem na borda das trocas de turno/dia.
// Fallback pro relógio do sistema se o CLP ainda não tiver lido essas tags
// (ex: logo após o boot, antes do 1º ciclo) ou se a leitura EtherNet/IP tiver
// falhado — nunca trava a lógica de turno por falta desse dado.
function horaCLP() {
  const ano  = dadosAtuais['clp_ano']?.valor;
  const mes  = dadosAtuais['clp_mes']?.valor;
  const dia  = dadosAtuais['clp_dia']?.valor;
  const hora = dadosAtuais['clp_hora']?.valor;
  const min  = dadosAtuais['clp_min']?.valor;
  const seg  = dadosAtuais['clp_seg']?.valor;
  if (ano == null || mes == null || dia == null || hora == null || min == null || seg == null) {
    return { data: new Date(), fonte: 'sistema' };
  }
  return { data: new Date(ano, mes - 1, dia, hora, min, seg), fonte: 'CLP' };
}

// Turno atual: DIURNO = 07:00-17:59 | NOTURNO = 18:00-06:59
function turnoAtual() {
  const h = horaCLP().data.getHours();
  return (h >= 7 && h < 18) ? 'diurno' : 'noturno';
}

// ── Energia acumulada do turno, direto do CLP (EA_xxxBM140_JT_U01_AJUSTE[13]) ──
// O CLP zera esse acumulador duas vezes por dia, SEM guardar o valor em
// nenhum outro tag antes (confirmado no L5K):
//   • ~06:59:55 (hora=6) fecha o NOTURNO — 5s ANTES da troca oficial de
//     turno do server (07:00)! Se o server só reagisse depois de detectar a
//     virada de turno, o valor já teria sido zerado e seria perdido de vez.
//   • ~18:01:57 (hora=18) fecha o DIURNO — quase 2min DEPOIS da troca do
//     server pra noturno (18:00). Nesse instante turnoCorrente.key já é o
//     noturno novo, então o valor pertence ao turnoAnteriorKey, não ao atual.
// Solução: rastreia o valor a cada ciclo de leitura (3s) e, quando ele CAI
// (reset do CLP), guarda o ÚLTIMO valor visto antes da queda — associado à
// chave de turno correta conforme a hora em que a queda foi detectada,
// independente de estar sincronizado com o instante exato da troca de turno.
let energiaClpState = {
  ultimo501: null, ultimo502: null, // não precisa persistir: pior caso de perder no restart
  // é só não detectar 1 reset se ele cair bem nos ~3s seguintes ao restart —
  // no read cycle seguinte já estabelece nova referência normalmente.
  fechados: {}, // chave: `${turnoKey}_${linha}` -> kWh capturado no reset — ISSO precisa persistir
};

// TOTALIZADOR_501/502[84] — tempo TOTAL de moinho rodando no dia (diurno +
// noturno combinados), confirmado pelo usuário. Reseta uma vez por dia
// (06:59:55, junto com [33]/[27]/[38]) — quando isso acontece, o valor que
// tinha antes da queda é o total definitivo do dia que ACABOU de fechar
// (diurno de ontem + noturno que começou ontem às 18h e terminou agora —
// mesma convenção de "noturno pertence ao dia em que começou" já usada no
// resto do sistema). Usado como fonte principal do "Min. Rodando" total do
// dia no boletim (detalhamento por dia), no lugar de somar os dois
// registros de turno separados.
let minTotalDiaState = { ultimo501: null, ultimo502: null, historico: {} };
const MIN_TOTAL_DIA_FILE = path.join(__dirname, 'min_total_dia_state.json');
if (fs.existsSync(MIN_TOTAL_DIA_FILE)) {
  try {
    const r = JSON.parse(fs.readFileSync(MIN_TOTAL_DIA_FILE, 'utf8'));
    minTotalDiaState.historico = r.historico || {};
    minTotalDiaState.ultimo501 = r.ultimo501 ?? null;
    minTotalDiaState.ultimo502 = r.ultimo502 ?? null;
  } catch(e) {}
}
let ultimaGravacaoMinTotalDia = 0;
function salvarMinTotalDiaAsync(forcar) {
  // Poda entradas com mais de 35 dias — não precisa crescer pra sempre.
  const limite = Date.now() - 35*86400000;
  for (const k of Object.keys(minTotalDiaState.historico)) {
    const [d,m,y] = k.split('_')[0].split('/');
    if (new Date(`${y}-${m}-${d}`).getTime() < limite) delete minTotalDiaState.historico[k];
  }
  // 'ultimo501/502' muda a cada ciclo de leitura (3s) — gravar isso toda
  // vez seria I/O demais. Throttle de 60s, exceto quando 'forcar' (usado só
  // na captura de um reset de verdade, que é rara, ~2x/dia).
  const agora = Date.now();
  if (!forcar && agora - ultimaGravacaoMinTotalDia < 60000) return;
  ultimaGravacaoMinTotalDia = agora;
  fs.writeFile(MIN_TOTAL_DIA_FILE, JSON.stringify({
    historico: minTotalDiaState.historico,
    ultimo501: minTotalDiaState.ultimo501,
    ultimo502: minTotalDiaState.ultimo502,
  }), 'utf8', (err) => {
    if (err) console.warn('[MIN_TOTAL_DIA] Falha ao salvar min_total_dia_state.json:', err.message);
  });
}
function registrarMinTotalDia(linha, valorAtual) {
  if (valorAtual == null) return;
  const kUlt = 'ultimo' + linha;
  if (minTotalDiaState[kUlt] != null && valorAtual < minTotalDiaState[kUlt]) {
    const valorFechado = minTotalDiaState[kUlt];
    const ontem = new Date(horaCLP().data);
    ontem.setDate(ontem.getDate() - 1);
    const dataStr = `${String(ontem.getDate()).padStart(2,'0')}/${String(ontem.getMonth()+1).padStart(2,'0')}/${ontem.getFullYear()}`;
    minTotalDiaState.historico[`${dataStr}_${linha}`] = valorFechado;
    console.log(`[MIN_TOTAL_DIA] CLP fechou ${linha}: ${valorFechado} min (dia ${dataStr})`);
    minTotalDiaState[kUlt] = valorAtual;
    salvarMinTotalDiaAsync(true);
    return;
  }
  minTotalDiaState[kUlt] = valorAtual;
  salvarMinTotalDiaAsync(false);
}

// TOTALIZADOR_501/502[38] — TONELADA total do dia (diurno + noturno
// combinados) — "TOTAL ALIM. DIA". Mesmo tag/reset que já usamos pra
// reconstituir a tonelada do noturno do 501 (ver calcAcum), mas aqui é o
// valor bruto do dia inteiro, capturado no reset das 06:59:55 — mesmo
// mecanismo do minTotalDiaState acima, espelhado pra tonelada. Confirmado
// pelo usuário contra o supervisório: "ONTEM" da tela bate com esse total
// quando capturado direito, mas a soma dos dois registros de turno
// (diurno.tonProduzida + noturno.tonProduzida) pode ficar um pouco abaixo
// — por isso ter o total do dia direto do CLP, sem depender da soma.
let tonTotalDiaState = { ultimo501: null, ultimo502: null, historico: {} };
const TON_TOTAL_DIA_FILE = path.join(__dirname, 'ton_total_dia_state.json');
if (fs.existsSync(TON_TOTAL_DIA_FILE)) {
  try {
    const r = JSON.parse(fs.readFileSync(TON_TOTAL_DIA_FILE, 'utf8'));
    tonTotalDiaState.historico = r.historico || {};
    tonTotalDiaState.ultimo501 = r.ultimo501 ?? null;
    tonTotalDiaState.ultimo502 = r.ultimo502 ?? null;
  } catch(e) {}
}
let ultimaGravacaoTonTotalDia = 0;
function salvarTonTotalDiaAsync(forcar) {
  const limite = Date.now() - 35*86400000;
  for (const k of Object.keys(tonTotalDiaState.historico)) {
    const [d,m,y] = k.split('_')[0].split('/');
    if (new Date(`${y}-${m}-${d}`).getTime() < limite) delete tonTotalDiaState.historico[k];
  }
  const agora = Date.now();
  if (!forcar && agora - ultimaGravacaoTonTotalDia < 60000) return;
  ultimaGravacaoTonTotalDia = agora;
  fs.writeFile(TON_TOTAL_DIA_FILE, JSON.stringify({
    historico: tonTotalDiaState.historico,
    ultimo501: tonTotalDiaState.ultimo501,
    ultimo502: tonTotalDiaState.ultimo502,
  }), 'utf8', (err) => {
    if (err) console.warn('[TON_TOTAL_DIA] Falha ao salvar ton_total_dia_state.json:', err.message);
  });
}
function registrarTonTotalDia(linha, valorAtual) {
  if (valorAtual == null) return;
  const kUlt = 'ultimo' + linha;
  if (tonTotalDiaState[kUlt] != null && valorAtual < tonTotalDiaState[kUlt]) {
    const valorFechado = tonTotalDiaState[kUlt];
    const ontem = new Date(horaCLP().data);
    ontem.setDate(ontem.getDate() - 1);
    const dataStr = `${String(ontem.getDate()).padStart(2,'0')}/${String(ontem.getMonth()+1).padStart(2,'0')}/${ontem.getFullYear()}`;
    tonTotalDiaState.historico[`${dataStr}_${linha}`] = valorFechado;
    console.log(`[TON_TOTAL_DIA] CLP fechou ${linha}: ${valorFechado} t (dia ${dataStr})`);
    tonTotalDiaState[kUlt] = valorAtual;
    salvarTonTotalDiaAsync(true);
    return;
  }
  tonTotalDiaState[kUlt] = valorAtual;
  salvarTonTotalDiaAsync(false);
}

// Persistência de energiaClpState.fechados (só essa parte, ver comentário
// acima) + turnoAnteriorKey — pequeno, muda raramente (só quando um reset é
// detectado, 2x/dia por linha), então salvar em disco toda vez que isso
// muda não pesa nada. Sem isso, um restart bem na janela entre a detecção
// do reset e o consumo por fecharTurnoCorrente()/api/producao perderia a
// captura exata daquele turno específico (cairia no trapézio calculado).
const ENERGIA_CLP_FILE = path.join(__dirname, 'energia_clp_state.json');
if (fs.existsSync(ENERGIA_CLP_FILE)) {
  try {
    const restaurado = JSON.parse(fs.readFileSync(ENERGIA_CLP_FILE, 'utf8'));
    energiaClpState.fechados = restaurado.fechados || {};
  } catch(e) {}
}
function salvarEnergiaClpStateAsync() {
  fs.writeFile(ENERGIA_CLP_FILE, JSON.stringify({ fechados: energiaClpState.fechados, turnoAnteriorKey }), 'utf8', (err) => {
    if (err) console.warn('[ENERGIA] Falha ao salvar energia_clp_state.json:', err.message);
  });
}

function registrarEnergiaClp(linha, valorAtual) {
  if (valorAtual == null) return;
  const kUlt = 'ultimo' + linha;
  if (energiaClpState[kUlt] != null && valorAtual < energiaClpState[kUlt]) {
    const valorFechado = energiaClpState[kUlt];
    const horaReset = horaCLP().data.getHours();
    // hora=18 -> reset das 18:01:57, fecha o DIURNO que já não é mais o
    // turno atual do server (ver comentário acima). Qualquer outra hora
    // (na prática só hora=6, reset das 06:59:55) -> fecha o turno ainda
    // ativo no server (o noturno que está prestes a virar).
    const chaveTurno = horaReset === 18 ? turnoAnteriorKey : turnoCorrente.key;
    if (chaveTurno) {
      energiaClpState.fechados[`${chaveTurno}_${linha}`] = valorFechado;
      console.log(`[ENERGIA] CLP fechou ${linha}: ${valorFechado} kWh (turno ${chaveTurno})`);
      salvarEnergiaClpStateAsync();
    }
  }
  energiaClpState[kUlt] = valorAtual;
}

// Evita que energiaClpState.fechados cresça pra sempre — mantém só o que
// ainda pode ser consultado (chave atual + chave anterior), descarta o resto.
function podarEnergiaClpFechados() {
  const validas = new Set([turnoAnteriorKey, turnoCorrente.key].filter(Boolean));
  for (const k of Object.keys(energiaClpState.fechados)) {
    const chaveDoTurno = k.replace(/_50[12]$/, '');
    if (!validas.has(chaveDoTurno)) delete energiaClpState.fechados[k];
  }
}

function turnoKey(dt, turno) {
  // Para noturno que passa da meia-noite, usa a data do início (dia do 18:00)
  return `${dt.toISOString().slice(0,10)}_${turno}`;
}

function dataInicioTurno(turno) {
  const agora = horaCLP().data;
  const d = new Date(agora);
  if (turno === 'diurno') {
    // Diurno começa às 07:00 do dia corrente
    d.setHours(7,0,0,0);
    // Se ainda não chegou às 07:00, o turno diurno é do dia anterior
    if (agora.getHours() < 7) d.setDate(d.getDate()-1);
  } else {
    // Noturno começa às 18:00
    d.setHours(18,0,0,0);
    // Se estamos entre 00:00 e 06:59, o noturno começou ontem às 18:00
    if (agora.getHours() < 7) d.setDate(d.getDate()-1);
    // Se estamos entre 07:00 e 17:59 não estamos no noturno (mas para robustez):
    // dataInicioTurno só é chamado para o turno atual
  }
  return d;
}

// Acumuladores em memória para o turno corrente
let turnoCorrente = { key: '', turno: '', inicio: null, acum501: [], acum502: [] };

// Chave do turno que estava aberto ANTES da transição mais recente — usada
// só por registrarEnergiaClp() pra saber a quem atribuir o reset das 18:01:57
// (ver comentário lá; o reset do diurno chega DEPOIS do server já ter
// trocado a chave pra noturno, então nesse instante "o turno atual" não é
// mais quem o valor pertence — é o anterior).
let turnoAnteriorKey = null;
if (fs.existsSync(ENERGIA_CLP_FILE)) {
  try { turnoAnteriorKey = JSON.parse(fs.readFileSync(ENERGIA_CLP_FILE, 'utf8')).turnoAnteriorKey || null; } catch(e) {}
}

function iniciarTurnoCorrente() {
  const turno = turnoAtual();
  const inicio = dataInicioTurno(turno);
  const key = turnoKey(inicio, turno);
  if (turnoCorrente.key !== key) {
    if (turnoCorrente.key) {
      turnoAnteriorKey = turnoCorrente.key;
      podarEnergiaClpFechados();
      salvarEnergiaClpStateAsync();
      fecharTurnoCorrente(); // fecha o turno anterior
    }

    // Restaura do disco se for exatamente o mesmo turno que já estava em
    // andamento antes de um restart — evita zerar as amostras acumuladas.
    // O kWh agora pode vir do acumulador nativo do CLP (AJUSTE[13], ver
    // registrarEnergiaClp), mas o trapézio calculado ainda é o fallback, e
    // esse fallback depende das amostras acumuladas aqui.
    if (turnoCorrenteRestaurar && turnoCorrenteRestaurar.key === key) {
      turnoCorrente = turnoCorrenteRestaurar;
      console.log(`[TURNO] Restaurado do disco após reinício — ${key} | ${turnoCorrente.acum501.length} amostras 501, ${turnoCorrente.acum502.length} amostras 502`);
      turnoCorrenteRestaurar = null;
      return;
    }
    turnoCorrenteRestaurar = null; // não bate com o turno atual — descarta, não tenta de novo

    // 501 noturno: ton via delta TOT[38]_fim - inicio (nao tem tag separado por turno)
    // 502 noturno: ton via TOTALIZADOR_502[85] (tag direto do CLP)
    const ton501_inicio = turno === 'noturno' ? (dadosAtuais['tot501_ton_total']?.valor ?? null) : null;
    turnoCorrente = {
      key, turno, inicio: inicio.getTime(),
      acum501: [], acum502: [],
      ton501_inicio,   // snapshot do TOT[38] no inicio do noturno para calcular delta
      // Contadores internos de minutos rodando — usados APENAS durante o noturno ao vivo.
      // Zerados no inicio do turno noturno; incrementados a cada ciclo (readInterval) com pot > 50.
      // No fechamento noturno sao substituidos pelos registros CLP [87].
      minRodando501: 0,
      minRodando502: 0,
    };
    console.log(`[TURNO] Iniciando ${turno} — ${key} | ton501_inicio=${ton501_inicio}`);
  }
}

// Calcula métricas de um turno usando kW/t do CLP como base de energia
// kWh = média(kW/t_CLP quando moinho rodando) × tonProduzida
// Apenas amostras com potência > 50 kW (moinho rodando) entram na média de kW/t

// ── Carga fixa auxiliar por linha ─────────────────────────────────────────────
// P(kW) = √3 × 380V × I(A) × FP / 1000  →  I × 0,5587 kW/A  (FP = 0,85)
// Equipamentos considerados: FN042 (exaustor) + MD070 (separador)
// Mais 20 kW fixos adicionais (lubrificação, ventiladores menores, etc.)
const SQRT3    = 1.7321;
const TENSAO_V = 380;
const FP_MOT   = 0.85;
const FATOR_KW = SQRT3 * TENSAO_V * FP_MOT / 1000; // ≈ 0.5587 kW/A
const EXTRA_KW = 20; // kW fixos adicionais

function cargaFixa(ln) {
  const iFn  = dadosAtuais[`ea_rede_${ln}fn042_c`]?.valor ?? 0;
  const iMd  = dadosAtuais[`ea_${ln}md070_it_u01`]?.valor ?? 0;
  return parseFloat((iFn * FATOR_KW + iMd * FATOR_KW + EXTRA_KW).toFixed(1));
}

function calcAcum(acum, clpTon, clpTh, clpMin, ln='501', kwhClp=null) {
  // CF de fallback caso a amostra nao tenha o campo cf (amostras antigas)
  const CF_fallback = cargaFixa(ln);

  // Amostras com moinho rodando — critério principal é o bit de campo
  // M501/502BM140.6 (retorno de ligado), não mais o limiar de potência
  // (pot > 50kW). pot > 50 fica só como reserva se o bit não tiver sido lido
  // (ex: falha pontual de leitura do DINT, ou amostra antiga sem o campo).
  const rodando = a => a.ligado === true || (a.ligado == null && a.pot != null && a.pot > 50);
  const amostrasRodando = acum.filter(rodando);

  // Se o bit de campo (M501/502BM140.6) esteve OFF em TODAS as amostras do
  // turno (confirmado, não apenas "não lido"), o moinho realmente não rodou
  // nesse turno — mesmo que as tags [87]/[13]/[38] do CLP mostrem um valor
  // "congelado" da última vez que rodaram (confirmado na prática: essas tags
  // só se atualizam quando há produção, e ficam paradas no último valor
  // válido quando o moinho para — não voltam a zero sozinhas). Nesse caso,
  // ignora qualquer coisa que as tags digam e força tudo pra 0.
  const nuncaRodou = acum.length > 0 && acum.every(a => a.ligado === false);

  // Minutos rodando: hierarquia de fontes
  // 1) clpMin: diurno = tag [33]/[27] lido no fechamento; noturno = tag [87] no fechamento
  //            ou contador interno (turnoCorrente.minRodandoXXX) durante o turno ao vivo
  // 2) maxAmostrasMin: pico do campo 'min' nas amostras (diurno, fallback se CLP falhar)
  // 3) estimativa por contagem de amostras rodando x intervalo
  const maxAmostrasMin = acum.reduce((mx, a) => (a.min != null && a.min > mx) ? a.min : mx, 0);
  const minRodando = nuncaRodou ? 0 : (clpMin != null
    ? clpMin
    : maxAmostrasMin > 0
      ? maxAmostrasMin
      : Math.round(amostrasRodando.length * (CLP_CONFIG.readInterval / 1000) / 60));
  const fonteMin = nuncaRodou ? 'bit_desligado' : (clpMin != null ? 'CLP' : maxAmostrasMin > 0 ? 'amostras' : 'estimado');

  // Toneladas produzidas: hierarquia de tres fontes
  // 1) clpTon: totalizador CLP (diurno = TOTALIZADOR[38]; noturno = delta[38])
  // 2) ultimoTonAcum: ultimo valor acumulado nao-nulo das amostras (tonAcum)
  // 3) fallback calculado: t/h media x horas rodando
  const ultimoTonAcum = acum.slice().reverse().find(a => a.tonAcum != null)?.tonAcum ?? null;
  const thMediaAmostras = amostrasRodando.filter(a => a.ton != null).length
    ? amostrasRodando.filter(a => a.ton != null).reduce((s, a) => s + a.ton, 0) /
      amostrasRodando.filter(a => a.ton != null).length
    : 0;
  const thMedia = clpTh != null ? clpTh : thMediaAmostras;
  const tonProd = nuncaRodou ? 0 : (clpTon != null
    ? clpTon
    : ultimoTonAcum != null
      ? ultimoTonAcum
      : parseFloat((thMedia * (minRodando / 60)).toFixed(1)));
  const fonteTon = nuncaRodou ? 'bit_desligado' : (clpTon != null ? 'CLP' : ultimoTonAcum != null ? 'tonAcum' : 'calculado');

  // kWh via integracao trapezoidal (Pot_motor + CF) por intervalo — usado
  // como FALLBACK, só quando o acumulador nativo do CLP (AJUSTE[13]) não foi
  // capturado nesse fechamento (ex: server reiniciou no meio do turno e
  // perdeu a janela de detecção do reset, ou falha de comunicação EtherNet/IP
  // bem na hora do reset).
  let kwhCalc = 0;
  for (let i = 1; i < acum.length; i++) {
    if (acum[i].pot != null && rodando(acum[i-1])) {
      const dt  = (acum[i].ts - acum[i-1].ts) / 3600000;
      const cf0 = acum[i-1].cf ?? CF_fallback;
      const cf1 = acum[i].cf  ?? CF_fallback;
      kwhCalc += ((acum[i].pot + acum[i-1].pot) / 2 + (cf0 + cf1) / 2) * dt;
    }
  }
  kwhCalc = parseFloat(kwhCalc.toFixed(1));

  // kWh do turno: prefere o acumulador nativo do CLP (EA_xxxBM140_JT_U01_AJUSTE[13]),
  // capturado no instante do reset por registrarEnergiaClp() — soma a cada
  // ~1s no próprio CLP, mais preciso que o trapézio (que só amostra a cada
  // 3s aqui no server). Cai pro cálculo só se o CLP não tiver sido capturado.
  const kwh = nuncaRodou ? 0 : (kwhClp != null ? parseFloat(kwhClp.toFixed(1)) : kwhCalc);
  const fonteKwh = nuncaRodou ? 'bit_desligado' : (kwhClp != null ? 'CLP' : 'calculado');

  // Potencia media total (motor + CF) - so quando rodando
  const potMediaTotal = amostrasRodando.length
    ? parseFloat((amostrasRodando.reduce((s, a) => s + a.pot + (a.cf ?? CF_fallback), 0)
        / amostrasRodando.length).toFixed(1))
    : null;

  // kW/t = (Pot_motor + CF) / t/h — usado só como FALLBACK, quando o CLP
  // não tiver amostras válidas de kwhtClp no turno (ver kwhtClpMedia abaixo)
  const kwhtCalc = (potMediaTotal != null && thMedia > 0)
    ? parseFloat((potMediaTotal / thMedia).toFixed(2))
    : null;

  // kW/t REAL do CLP (TOTALIZADOR[47]/[39]) — média das amostras do turno
  // com moinho rodando. Preferido sobre o cálculo acima quando disponível.
  const amostrasKwhtClpRodando = amostrasRodando.filter(a => a.kwhtClp != null);
  const kwhtClpReal = amostrasKwhtClpRodando.length > 0
    ? parseFloat((amostrasKwhtClpRodando.reduce((s, a) => s + a.kwhtClp, 0)
        / amostrasKwhtClpRodando.length).toFixed(2))
    : null;
  const fonteKwht = kwhtClpReal != null ? 'CLP' : (kwhtCalc != null ? 'calculado' : null);

  // kWh/t = kWh / toneladas
  const kwht = (kwh > 0 && tonProd > 0)
    ? parseFloat((kwh / tonProd).toFixed(2))
    : null;

  return {
    kwh,
    fonteKwh,        // 'CLP' | 'calculado' — de onde veio o kWh do turno
    potMedia:        potMediaTotal,
    thMedia:         parseFloat((thMedia||0).toFixed(2)),
    tonProduzida:    parseFloat(Number(tonProd).toFixed(1)),
    minRodando:      Math.round(minRodando),
    fonteMin,        // 'CLP' | 'amostras' | 'estimado'
    kwht,
    kwhtClpMedia:    kwhtClpReal ?? kwhtCalc, // agora usa CLP quando disponível; calculado só como fallback
    fonteKwht,       // 'CLP' | 'calculado' | null — de onde veio o kwhtClpMedia acima
    amostrasRodando: amostrasRodando.length,
    amostras:        acum.length,
    fonteDados:      fonteTon,  // 'CLP' | 'tonAcum' | 'calculado'
  };
}

function fecharTurnoCorrente() {
  // Guard separado para 501 e 502: um pode ter dados mesmo que o outro esteja vazio
  if (!turnoCorrente.key) return;
  if (!turnoCorrente.acum501.length && !turnoCorrente.acum502.length) return;
  const isDiurno = turnoCorrente.turno === 'diurno';

  // Diurno: totalizadores CLP validos (acumulam em tempo real).
  // Noturno: ton via delta TOTALIZADOR[38]_fim - inicio; min/th via amostras
  // (tot_t2 so e salvo pelo CLP as 18:01:57 do dia seguinte, invalido ao fechar as 07:00).
  const clp501 = isDiurno ? {
    ton: dadosAtuais['tot501_ton_total']?.valor ?? null,
    th:  dadosAtuais['totalizador_501']?.valor  ?? null,
    min: dadosAtuais['tot501_min_dia']?.valor   ?? null,
  } : {
    ton: (() => {
      // TOTALIZADOR_501[38] ("TOTAL ALIM. DIA 501") reseta à MEIA-NOITE — e
      // o turno noturno (18h–07h) atravessa a meia-noite TODA noite. O
      // cálculo antigo (fim - ini) exigia fim >= ini, e isso falha sempre
      // que o reset acontece no meio do turno — ou seja, falhava
      // sistematicamente, não só às vezes. Corrigido: reconstitui somando
      // os incrementos entre as amostras já salvas do turno (mesma técnica
      // já usada pro acumulador de energia AJUSTE[13] — ver
      // reconstituirKwhAcumulado no /api/indicadores). Quando o valor CAI
      // de uma amostra pra outra (reset detectado), soma o valor novo
      // inteiro, que já é o total acumulado desde o reset.
      const amostrasTon = turnoCorrente.acum501.map(a => a.tonAcum).filter(v => v != null);
      if (amostrasTon.length < 2) return null;
      let total = 0;
      for (let i = 1; i < amostrasTon.length; i++) {
        total += amostrasTon[i] >= amostrasTon[i-1]
          ? (amostrasTon[i] - amostrasTon[i-1])
          : amostrasTon[i];
      }
      return parseFloat(total.toFixed(1));
    })(),
    th:  null,
    // TOTALIZADOR_501[87] é um delta calculado DENTRO do próprio CLP
    // ([84]-[83], onde [83] é tirado no início do turno às 18:01:57 e [84]
    // é um checkpoint que avança durante o turno) — confirmado na ladder e
    // pelo usuário: atualiza minuto a minuto do início ao fim do turno
    // noturno, sem pausa. Confiável como fonte principal. Contador interno
    // fica só como reserva se a leitura da tag falhar.
    min: dadosAtuais['tot501_min_t2']?.valor ?? Math.round(turnoCorrente.minRodando501 || 0),
  };
  const clp502 = isDiurno ? {
    ton: dadosAtuais['tot502_ton_diurno']?.valor ?? null, // TOTALIZADOR_502[88]
    th:  dadosAtuais['totalizador_502']?.valor  ?? null,
    min: dadosAtuais['tot502_min_dia']?.valor   ?? null, // TOTALIZADOR_502[27]
  } : {
    ton: dadosAtuais['tot502_prod_t2']?.valor   ?? null, // TOTALIZADOR_502[85]
    th:  null,
    // Mesmo caso do 501 (ver comentário acima) — TOTALIZADOR_502[87] atualiza
    // minuto a minuto do início ao fim do turno, confiável como fonte principal.
    min: dadosAtuais['tot502_min_t2']?.valor ?? Math.round(turnoCorrente.minRodando502 || 0),
  };

  console.log(
    `[TURNO] Fechando ${turnoCorrente.key} (${turnoCorrente.turno})` +
    ` | CLP 501 ton=${clp501.ton} th=${clp501.th} min=${clp501.min}` +
    ` | CLP 502 ton=${clp502.ton} th=${clp502.th} min=${clp502.min}` +
    ` | acum501=${turnoCorrente.acum501.length} acum502=${turnoCorrente.acum502.length}`
  );

  // kWh do turno: prioridade —
  //  1) valor CAPTURADO no reset (energiaClpState.fechados) — o mais preciso,
  //     só existe DEPOIS que o turno realmente fechou no CLP.
  //  2) valor AO VIVO do próprio tag AJUSTE[13] — durante o turno ainda em
  //     andamento (a maior parte do tempo), é isso que soma direto no CLP a
  //     cada ~1s; ler direto daqui é o que faz a tela bater com o que você
  //     vê no CLP em tempo real, em vez de usar o trapézio calculado.
  //  3) trapézio calculado — só se nenhum dos dois acima estiver disponível
  //     (falha de comunicação com o tag).
  const kwhAoVivo501 = dadosAtuais['ea_501bm140_kwh_turno']?.valor ?? null;
  const kwhAoVivo502 = dadosAtuais['ea_502bm140_kwh_turno']?.valor ?? null;
  const kwhClp501 = energiaClpState.fechados[`${turnoCorrente.key}_501`] ?? kwhAoVivo501;
  const kwhClp502 = energiaClpState.fechados[`${turnoCorrente.key}_502`] ?? kwhAoVivo502;
  const l501 = calcAcum(turnoCorrente.acum501, clp501.ton, clp501.th, clp501.min, '501', kwhClp501);
  const l502 = calcAcum(turnoCorrente.acum502, clp502.ton, clp502.th, clp502.min, '502', kwhClp502);
  const total = {
    kwh: parseFloat(((l501.kwh||0)+(l502.kwh||0)).toFixed(1)),
    tonProduzida: parseFloat((l501.tonProduzida+l502.tonProduzida).toFixed(1)),
    thMedia: parseFloat(((l501.thMedia+l502.thMedia)).toFixed(2)),
    kwht: (l501.tonProduzida+l502.tonProduzida)>0 && (l501.kwh||l502.kwh)
      ? parseFloat(((l501.kwh||0)+(l502.kwh||0))/(l501.tonProduzida+l502.tonProduzida)).toFixed(2)
      : null,
  };
  const registro = {
    id: turnoCorrente.key,
    data: new Date(turnoCorrente.inicio).toLocaleDateString('pt-BR'),
    turno: turnoCorrente.turno,
    inicio: turnoCorrente.inicio,
    fim: Date.now(),
    linha501: l501,
    linha502: l502,
    total,
  };
  producao.turnos = producao.turnos.filter(t=>t.id!==registro.id);
  producao.turnos.unshift(registro);
  if (producao.turnos.length > 90) producao.turnos.splice(90);
  salvarProducao();
  broadcast({ tipo: 'turno_fechado', registro });
  console.log(
    `[TURNO] Fechado: ${registro.id}` +
    ` | 501: ${l501.tonProduzida}t  ${l501.minRodando}min[${l501.fonteMin}]  ${l501.kwh}kWh[${l501.fonteKwh}]  ${l501.kwhtClpMedia}kW/t[${l501.fonteKwht}]  src=${l501.fonteDados}` +
    ` | 502: ${l502.tonProduzida}t  ${l502.minRodando}min[${l502.fonteMin}]  ${l502.kwh}kWh[${l502.fonteKwh}]  ${l502.kwhtClpMedia}kW/t[${l502.fonteKwht}]  src=${l502.fonteDados}`
  );
}

// Adiciona amostra ao turno corrente a cada ciclo de leitura
function registrarAmostraTurno(ts) {
  iniciarTurnoCorrente();
  const isDiurnoAgora = turnoAtual() === 'diurno';
  const pot501 = dadosAtuais['ea_501bm140_jt_u01']?.valor ?? null;
  const pot502 = dadosAtuais['ea_502bm140_jt_u01']?.valor ?? null;
  const intervaloMin = CLP_CONFIG.readInterval / 60000; // intervalo em minutos

  // Noturno ao vivo: incrementar contador interno se moinho rodando (pot > 50 kW).
  // O contador e zerado no inicio do noturno e substituido pelo tag [87] no fechamento.
  if (!isDiurnoAgora) {
    if (dadosDigitais['m501bm140']?.ligado === true) turnoCorrente.minRodando501 = (turnoCorrente.minRodando501 || 0) + intervaloMin;
    if (dadosDigitais['m502bm140']?.ligado === true) turnoCorrente.minRodando502 = (turnoCorrente.minRodando502 || 0) + intervaloMin;
  }

  const amost501 = {
    ts,
    pot:     pot501,
    cf:      cargaFixa('501'),
    ton:     dadosAtuais['totalizador_501']?.valor  ?? null, // t/h media
    tonAcum: dadosAtuais['tot501_ton_total']?.valor ?? null, // ton acumuladas do dia (fallback fechamento)
    // min: diurno = tag CLP [33]; noturno = nao usado (contador em turnoCorrente.minRodando501)
    min: isDiurnoAgora ? (dadosAtuais['tot501_min_dia']?.valor ?? null) : null,
    kwhtClp: dadosAtuais['tot501_kwht_clp']?.valor ?? null, // kW/t direto do CLP (TOTALIZADOR_501[47])
    ligado: dadosDigitais['m501bm140']?.ligado ?? null, // M501BM140.6 — retorno de ligado
  };
  const amost502 = {
    ts,
    pot:     pot502,
    cf:      cargaFixa('502'),
    ton:     dadosAtuais['totalizador_502']?.valor  ?? null, // t/h media
    tonAcum: isDiurnoAgora
      ? (dadosAtuais['tot502_ton_diurno']?.valor ?? null)  // TOTALIZADOR_502[88]
      : (dadosAtuais['tot502_prod_t2']?.valor   ?? null),  // TOTALIZADOR_502[85]
    // min: diurno = tag CLP [27]; noturno = nao usado (contador em turnoCorrente.minRodando502)
    min: isDiurnoAgora ? (dadosAtuais['tot502_min_dia']?.valor ?? null) : null,
    kwhtClp: dadosAtuais['tot502_kwht_clp']?.valor ?? null, // kW/t direto do CLP (TOTALIZADOR_502[39])
    ligado: dadosDigitais['m502bm140']?.ligado ?? null, // M502BM140.6 — retorno de ligado
  };
  turnoCorrente.acum501.push(amost501);
  turnoCorrente.acum502.push(amost502);
  // Persiste o turno em andamento periodicamente — NÃO a cada amostra, e num
  // arquivo PRÓPRIO (turno_corrente.json), separado de producao.json — ver
  // comentário acima de TURNO_FILE pra entender por quê. Assíncrono, e só a
  // cada 300 amostras (15 min, a 3s por amostra) — em troca, um restart pode
  // perder até 15 min do turno em andamento, em vez do turno inteiro como
  // antes da correção original.
  if (turnoCorrente.acum501.length % 300 === 0) {
    salvarTurnoCorrenteAsync();
  }
  // Salvar parcial a cada 20 amostras (~1min) — só grava producao.turnos
  // (pequeno, ~90 registros), nunca mais os arrays de turnoCorrente.
  if (turnoCorrente.acum501.length % 20 === 0) fecharTurnoCorrente();
}

// Produção agregada POR PRODUTO (não por linha) — junta m1+m2, classifica
// pela receita real de cada meio-turno (mesma lógica de classRecQ já usada
// na Qualidade, e mesma fórmula ton/th por meio-turno já usada no app
// Cimento Gaúcho pra "T/h Média CPIV/Pozo"). Depende do Firestore (só lá
// tem a receita usada — o CLP não sabe qual produto está rodando).
app.get('/api/producao/por-produto', async (req, res) => {
  const inicio = parseInt(req.query.inicio) || 0;
  const fim    = parseInt(req.query.fim)    || Date.now();
  const payload = await getPayloadFirestore();
  if (!payload || !Array.isArray(payload.months)) return res.json({ produtos: [] });

  const acum = {}; // { 'CPIV': {ton, horas}, 'Pozomix': {...}, ... }
  function acumular(produto, ton, th) {
    if (!produto || !(ton > 0) || !(th > 0)) return;
    if (!acum[produto]) acum[produto] = { ton: 0, horas: 0 };
    acum[produto].ton   += ton;
    acum[produto].horas += ton / th;
  }
  for (let t = inicio; t < fim; t += 86400000) {
    const d = new Date(t);
    const chaveMes = `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}`;
    const dataDia  = `${String(d.getDate()).padStart(2,'0')}/${String(d.getMonth()+1).padStart(2,'0')}`;
    const mes = payload.months.find(m => m.key === chaveMes);
    if (!mes) continue;
    for (const chaveM of ['m1','m2']) {
      const rows = mes[chaveM];
      if (!Array.isArray(rows)) continue;
      const row = rows.find(r => r.data === dataDia);
      if (!row) continue;
      acumular(classRecQ(row.receitaD || row.receita), parseFloat(row.dProd)||0, parseFloat(row.dTh)||0);
      acumular(classRecQ(row.receitaN || row.receita), parseFloat(row.nProd)||0, parseFloat(row.nTh)||0);
    }
  }
  const produtos = Object.keys(acum).map(nome => {
    const a = acum[nome];
    return {
      produto:  nome,
      tonTotal: parseFloat(a.ton.toFixed(1)),
      thMedio:  a.horas > 0 ? parseFloat((a.ton/a.horas).toFixed(2)) : null,
    };
  }).sort((a,b) => b.tonTotal - a.tonTotal);
  res.json({ produtos });
});

// API produção
app.get('/api/producao', (req, res) => {
  iniciarTurnoCorrente();
  const turno = turnoAtual();
  const inicio = dataInicioTurno(turno);
  const key = turnoKey(inicio, turno);
  const isDir = turnoAtual() === 'diurno';
  // Diurno: usa totalizadores do CLP (acumulam em tempo real durante o dia)
  // Noturno: usa cálculo interno do acum — os tot_t2 do CLP só são válidos
  //          após 18:01:57 do dia seguinte, portanto não disponíveis durante o turno
  const clp501live = isDir ? {
    ton: dadosAtuais['tot501_ton_total']?.valor ?? null,
    th:  dadosAtuais['totalizador_501']?.valor  ?? null,
    min: dadosAtuais['tot501_min_dia']?.valor   ?? null,
  } : {
    // Noturno ao vivo: reconstitui somando os incrementos entre amostras já
    // salvas do turno (trata o reset de meia-noite de TOTALIZADOR_501[38] —
    // ver comentário completo em fecharTurnoCorrente), em vez de fim-ini
    // (que falhava sistematicamente sempre que o reset caía no meio do turno).
    ton: (() => {
      const amostrasTon = turnoCorrente.acum501.map(a => a.tonAcum).filter(v => v != null);
      if (amostrasTon.length < 2) return null;
      let total = 0;
      for (let i = 1; i < amostrasTon.length; i++) {
        total += amostrasTon[i] >= amostrasTon[i-1]
          ? (amostrasTon[i] - amostrasTon[i-1])
          : amostrasTon[i];
      }
      return parseFloat(total.toFixed(1));
    })(),
    th: null,
    // [87] é confiável (delta calculado no próprio CLP), atualiza minuto a
    // minuto do início ao fim do turno. Contador interno só como reserva.
    min: dadosAtuais['tot501_min_t2']?.valor ?? (turnoCorrente.minRodando501 > 0 ? parseFloat(turnoCorrente.minRodando501.toFixed(1)) : null),
  };
  const clp502live = isDir ? {
    ton: dadosAtuais['tot502_ton_diurno']?.valor ?? null, // TOTALIZADOR_502[88]
    th:  dadosAtuais['totalizador_502']?.valor  ?? null,
    min: dadosAtuais['tot502_min_dia']?.valor   ?? null, // TOTALIZADOR_502[27]
  } : {
    ton: dadosAtuais['tot502_prod_t2']?.valor   ?? null, // TOTALIZADOR_502[85]
    th: null,
    // Mesmo caso do 501 (ver comentário lá) — [87] confiável, minuto a minuto.
    min: dadosAtuais['tot502_min_t2']?.valor ?? (turnoCorrente.minRodando502 > 0 ? parseFloat(turnoCorrente.minRodando502.toFixed(1)) : null),
  };
  // Mesma prioridade usada em fecharTurnoCorrente(): capturado no reset >
  // ao vivo (tag ainda não resetou) > trapézio calculado como último recurso.
  // Faltava aqui — esse é o card "ao vivo" da página Dados de Produção,
  // ainda caía sempre no trapézio antes desse ajuste.
  const kwhAoVivo501live = dadosAtuais['ea_501bm140_kwh_turno']?.valor ?? null;
  const kwhAoVivo502live = dadosAtuais['ea_502bm140_kwh_turno']?.valor ?? null;
  const kwhClp501live = energiaClpState.fechados[`${key}_501`] ?? kwhAoVivo501live;
  const kwhClp502live = energiaClpState.fechados[`${key}_502`] ?? kwhAoVivo502live;
  const corrente501 = calcAcum(turnoCorrente.acum501, clp501live.ton, clp501live.th, clp501live.min, '501', kwhClp501live);
  const corrente502 = calcAcum(turnoCorrente.acum502, clp502live.ton, clp502live.th, clp502live.min, '502', kwhClp502live);
  // DIAGNOSTICO: mostra fontes dos dados ao vivo - remover apos confirmar em producao
  console.log(
    `[DIAG 501] ton=${corrente501.tonProduzida}[${corrente501.fonteDados}] min=${corrente501.minRodando}[${corrente501.fonteMin}] kwht=${corrente501.kwhtClpMedia}` +
    ` | CLP live: ton=${clp501live.ton} th=${clp501live.th} min=${clp501live.min}` +
    ` | acum=${turnoCorrente.acum501.length} rodando=${corrente501.amostrasRodando}`
  );
  console.log(
    `[DIAG 502] ton=${corrente502.tonProduzida}[${corrente502.fonteDados}] min=${corrente502.minRodando}[${corrente502.fonteMin}] kwht=${corrente502.kwhtClpMedia}` +
    ` | CLP live: ton=${clp502live.ton} th=${clp502live.th} min=${clp502live.min}` +
    ` | acum=${turnoCorrente.acum502.length} rodando=${corrente502.amostrasRodando}`
  );
  // kW/t instantâneo do CLP (ao vivo, para exibição no card)
  corrente501.kwhtClp = dadosAtuais['tot501_kwht_clp']?.valor ?? null;
  corrente502.kwhtClp = dadosAtuais['tot502_kwht_clp']?.valor ?? null;
  const correnteTotal = {
    kwh: parseFloat(((corrente501.kwh||0)+(corrente502.kwh||0)).toFixed(1)),
    tonProduzida: parseFloat((corrente501.tonProduzida+corrente502.tonProduzida).toFixed(1)),
    thMedia: parseFloat((corrente501.thMedia+corrente502.thMedia).toFixed(2)),
    kwht: (corrente501.tonProduzida+corrente502.tonProduzida)>0 && (corrente501.kwh||corrente502.kwh)
      ? parseFloat(((corrente501.kwh||0)+(corrente502.kwh||0))/(corrente501.tonProduzida+corrente502.tonProduzida)).toFixed(2)
      : null,
  };
  const correnteRegistro = {
    id: key, data: new Date(inicio).toLocaleDateString('pt-BR'),
    turno, inicio: inicio.getTime(), fim: Date.now(), corrente: true,
    linha501: corrente501, linha502: corrente502, total: correnteTotal,
  };
  // Histórico sem o corrente
  const historico = producao.turnos.filter(t=>t.id!==key);
  res.json({ corrente: correnteRegistro, turnos: historico });
});

// ─── Correção manual de dados de produção ──────────────────────────────────
// Turnos já fechados carregam os bugs que existiam NA HORA que fecharam —
// cada correção de cálculo feita no server só vale pra turnos que fecham
// DEPOIS do deploy dela. Esses dois endpoints deixam corrigir diretamente
// um registro já salvo (turno específico) ou o total capturado de um dia
// (quando o problema está no valor batido direto do CLP no reset, não na
// soma dos turnos) — sem precisar de mim toda vez que aparecer um valor
// visivelmente errado num turno antigo.

// GET  /api/producao/turno/:id                — ver um turno específico
// POST /api/producao/turno/:id/corrigir       — corrigir campos dele
//   body: { linha: '501'|'502', campos: { tonProduzida?, minRodando?, kwh? } }
app.get('/api/producao/turno/:id', (req, res) => {
  const turno = producao.turnos.find(t => t.id === req.params.id);
  if (!turno) return res.status(404).json({ erro: 'Turno não encontrado' });
  res.json(turno);
});

app.post('/api/producao/turno/:id/corrigir', (req, res) => {
  const { linha, campos } = req.body || {};
  if (!['501','502','total'].includes(linha)) return res.status(400).json({ erro: "linha deve ser '501', '502' ou 'total'" });
  if (!campos || typeof campos !== 'object') return res.status(400).json({ erro: 'campos é obrigatório (objeto)' });

  const turno = producao.turnos.find(t => t.id === req.params.id);
  if (!turno) return res.status(404).json({ erro: 'Turno não encontrado' });
  const chaveLinha = linha === 'total' ? 'total' : `linha${linha}`;
  if (!turno[chaveLinha]) return res.status(404).json({ erro: `Turno não tem dado de ${chaveLinha}` });

  // 'total' não tem minRodando (é um total combinado 501+502, não um
  // registro de turno com tempo próprio) — os demais campos valem igual.
  const CAMPOS_PERMITIDOS = linha === 'total'
    ? ['tonProduzida', 'kwh', 'thMedia', 'kwht']
    : ['tonProduzida', 'minRodando', 'kwh', 'thMedia', 'kwht', 'kwhtClpMedia'];
  const alterado = {};
  for (const campo of CAMPOS_PERMITIDOS) {
    if (campos[campo] === undefined) continue;
    const antes = turno[chaveLinha][campo];
    const novo = campos[campo] === null ? null : parseFloat(campos[campo]);
    turno[chaveLinha][campo] = novo;
    alterado[campo] = { antes, depois: novo };
  }
  if (Object.keys(alterado).length === 0) {
    return res.status(400).json({ erro: `Nenhum campo reconhecido. Use: ${CAMPOS_PERMITIDOS.join(', ')}` });
  }
  turno[chaveLinha].fonteCorrecaoManual = true;

  console.log(`[ADMIN] Correção manual em turno ${turno.id} (${chaveLinha}):`, alterado);
  salvarProducaoAsync();
  res.json({ ok: true, turnoId: turno.id, linha: chaveLinha, alterado });
});

// GET  /api/producao/dia-capturado/:linha/:data       — ver overrides de um dia (DD/MM/YYYY)
// POST /api/producao/dia-capturado/:linha/:data/corrigir — definir/remover o override
//   body: { tonProduzida?: number|null, minRodando?: number|null }
//   null = remove o override (volta a usar a soma dos turnos daquele dia)
app.get('/api/producao/dia-capturado/:linha/:data', (req, res) => {
  const { linha, data } = req.params;
  if (!['501','502'].includes(linha)) return res.status(400).json({ erro: "linha deve ser '501' ou '502'" });
  const chave = `${data}_${linha}`;
  res.json({
    linha, data,
    tonProduzida: tonTotalDiaState.historico[chave] ?? null,
    minRodando:   minTotalDiaState.historico[chave] ?? null,
  });
});

app.post('/api/producao/dia-capturado/:linha/:data/corrigir', (req, res) => {
  const { linha, data } = req.params;
  if (!['501','502'].includes(linha)) return res.status(400).json({ erro: "linha deve ser '501' ou '502'" });
  if (!/^\d{2}\/\d{2}\/\d{4}$/.test(data)) return res.status(400).json({ erro: 'data deve estar no formato DD/MM/YYYY' });
  const { tonProduzida, minRodando } = req.body || {};
  if (tonProduzida === undefined && minRodando === undefined) {
    return res.status(400).json({ erro: 'Informe tonProduzida e/ou minRodando (null pra remover o override)' });
  }
  const chave = `${data}_${linha}`;
  const alterado = {};
  if (tonProduzida !== undefined) {
    if (tonProduzida === null) { delete tonTotalDiaState.historico[chave]; alterado.tonProduzida = 'removido (volta a somar os turnos)'; }
    else { tonTotalDiaState.historico[chave] = parseFloat(tonProduzida); alterado.tonProduzida = tonTotalDiaState.historico[chave]; }
    salvarTonTotalDiaAsync(true);
  }
  if (minRodando !== undefined) {
    if (minRodando === null) { delete minTotalDiaState.historico[chave]; alterado.minRodando = 'removido (volta a somar os turnos)'; }
    else { minTotalDiaState.historico[chave] = parseFloat(minRodando); alterado.minRodando = minTotalDiaState.historico[chave]; }
    salvarMinTotalDiaAsync(true);
  }
  console.log(`[ADMIN] Correção manual do dia capturado ${chave}:`, alterado);
  res.json({ ok: true, chave, alterado });
});

app.get('/api/producao/mensal', (req, res) => {
  const mes = req.query.mes || horaCLP().data.toISOString().slice(0,7);
  const turnos = producao.turnos.filter(t=>{
    const d = new Date(t.inicio);
    return d.toISOString().slice(0,7) === mes;
  });
  // Agregar por dia — kW/t = kWh do dia ÷ toneladas do dia (divisão direta
  // das variáveis do CLP, mesma convenção do /api/boletim e do
  // /api/indicadores). Antes fazia média de kW/t por turno (kwhtClpMedia/
  // kwht individuais), que diverge da razão real quando um turno roda com
  // t/h baixo e potência alta.
  const porDia = {};
  turnos.forEach(t=>{
    const dia = new Date(t.inicio).toLocaleDateString('pt-BR');
    if(!porDia[dia]) porDia[dia]={dia,turnos:[],kwh501:0,kwh502:0,ton501:0,ton502:0};
    porDia[dia].turnos.push(t.turno);
    porDia[dia].kwh501 += t.linha501.kwh||0;
    porDia[dia].kwh502 += t.linha502.kwh||0;
    porDia[dia].ton501 += t.linha501.tonProduzida||0;
    porDia[dia].ton502 += t.linha502.tonProduzida||0;
  });
  const dias = Object.values(porDia).map(d=>{
    const tonTotalDia = parseFloat((d.ton501+d.ton502).toFixed(1));
    const kwhTotalDia = parseFloat((d.kwh501+d.kwh502).toFixed(1));
    return {
      ...d,
      kwht501m:  d.ton501 > 0 ? parseFloat((d.kwh501/d.ton501).toFixed(2)) : null,
      kwht502m:  d.ton502 > 0 ? parseFloat((d.kwh502/d.ton502).toFixed(2)) : null,
      kwhtMedia: tonTotalDia > 0 ? parseFloat((kwhTotalDia/tonTotalDia).toFixed(2)) : null,
      tonTotal:  tonTotalDia,
      kwhTotal:  kwhTotalDia,
    };
  });
  // Minutos rodando do mês: soma direta de todos os turnos (igual T/h Médio
  // faz com a tonelada — nada de média simples aqui, é soma mesmo).
  const minMes501 = Math.round(turnos.reduce((s,t)=>s+(t.linha501.minRodando||0),0));
  const minMes502 = Math.round(turnos.reduce((s,t)=>s+(t.linha502.minRodando||0),0));
  const ton501Total = dias.reduce((s,d)=>s+d.ton501,0);
  const ton502Total = dias.reduce((s,d)=>s+d.ton502,0);
  const kwh501Total = dias.reduce((s,d)=>s+d.kwh501,0);
  const kwh502Total = dias.reduce((s,d)=>s+d.kwh502,0);
  // T/h médio do mês: soma de toneladas ÷ soma de horas — mesma
  // metodologia ponderada usada em todo o resto (não é média das médias
  // diárias, que penaliza dias com bastante e pouca produção igual).
  const thMes501 = minMes501>0 ? parseFloat((ton501Total/(minMes501/60)).toFixed(2)) : null;
  const thMes502 = minMes502>0 ? parseFloat((ton502Total/(minMes502/60)).toFixed(2)) : null;
  // kW/t do mês: mesma metodologia ponderada — soma de kWh ÷ soma de toneladas.
  const kwhtMes501 = ton501Total>0 ? parseFloat((kwh501Total/ton501Total).toFixed(2)) : null;
  const kwhtMes502 = ton502Total>0 ? parseFloat((kwh502Total/ton502Total).toFixed(2)) : null;
  const tonMesTotal = ton501Total + ton502Total;
  const kwhMesTotal = kwh501Total + kwh502Total;
  const totMes = {
    kwh501:   parseFloat(kwh501Total.toFixed(0)),
    kwh502:   parseFloat(kwh502Total.toFixed(0)),
    ton501:   parseFloat(ton501Total.toFixed(0)),
    ton502:   parseFloat(ton502Total.toFixed(0)),
    tonTotal: parseFloat(tonMesTotal.toFixed(0)),
    kwhTotal: parseFloat(kwhMesTotal.toFixed(0)),
    kwht501:  kwhtMes501,
    kwht502:  kwhtMes502,
    kwht:     tonMesTotal > 0 ? parseFloat((kwhMesTotal/tonMesTotal).toFixed(2)) : null,
    min501:   minMes501,
    min502:   minMes502,
    th501:    thMes501,
    th502:    thMes502,
  };
  res.json({ mes, dias, totMes, turnos });
});

async function iniciarEthernetIP() {
  const PLC = new Controller();
  PLC.on('error', err => { console.error('[EIP]', err.message); broadcast({ tipo: 'status', online: false }); });
  // Trava contra reconexões concorrentes: existiam 3 pontos independentes
  // (falha de connect, Micro Queue cheia, 0 leituras OK) chamando
  // setTimeout(conectar,...) sem coordenação entre si. Se dois disparassem
  // perto um do outro, duas chamadas de conectar() rodavam ao mesmo tempo
  // sobre o mesmo objeto PLC — suspeita de ser a causa do travamento
  // "silencioso" (sem erro, sem SYN, sem crash) que vimos hoje.
  let reconectando = false;
  function agendarReconexao(ms) {
    if (reconectando) return; // já tem uma reconexão agendada/em andamento
    reconectando = true;
    setTimeout(() => { conectar(); }, ms);
  }
  async function conectar() {
    try {
      await PLC.connect(CLP_CONFIG.ip, CLP_CONFIG.slot);
      reconectando = false;
      console.log(`[EIP] Conectado: ${PLC.properties?.name || 'CLP'}`);
      broadcast({ tipo: 'status', online: true });
      await lerNomesProdutos(PLC); // primeira leitura de nomes
      await lerTodosOsTags(PLC);   // primeiro ciclo de dados

      // Coleta de processo — alinhada ao relógio, a cada 5 minutos
      function agendarColeta() {
        const now = new Date();
        const msAteProximo5min = (5 - (now.getMinutes() % 5)) * 60000
          - now.getSeconds() * 1000 - now.getMilliseconds();
        setTimeout(() => {
          registrarColeta();
          setInterval(registrarColeta, 5 * 60 * 1000);
        }, msAteProximo5min);
      }
      agendarColeta();

      // Sincronização com o Firestore (boletim + produção do turno em
      // andamento) a cada 5 minutos — assim o cimento_gaucho.html já abre
      // preenchido, sem precisar rodar nada manualmente.
      setTimeout(() => {
        sincronizarBoletimFirebase();
        setInterval(sincronizarBoletimFirebase, 5 * 60 * 1000);
      }, 60000); // espera 1min do boot pra garantir que já há amostras/turnos coletados

      // chained setTimeout — garante que o próximo ciclo só começa
      // APÓS o anterior terminar completamente, evitando acúmulo de
      // promises na fila interna do st-ethernet-ip (Micro Queue overflow)
      function agendarProximoCiclo() {
        setTimeout(async () => {
          try {
            const { ok, erros } = await lerTodosOsTags(PLC);
            // Mesma auto-cura que o server-pa2.js já tinha: se o ciclo inteiro
            // vier com 0 leituras OK, a sessão CIP morreu silenciosamente
            // (cada tag individual só dá timeout, sem lançar exceção — por
            // isso o catch abaixo nunca disparava nesse caso). Sem isso, o
            // ciclo seguia rodando ~170s por vez (113 tags × 1.5s de timeout
            // cada) pra sempre, sem nunca reconectar sozinho.
            if (ok === 0 && erros > 0) {
              console.warn('[EIP] 0 leituras OK no ciclo — sessão CIP morta, destruindo e reconectando...');
              try { PLC.destroy(); } catch(_) {}
              broadcast({ tipo: 'status', online: false });
              agendarReconexao(5000);
              return; // para o loop — conectar() vai reiniciar tudo
            }
          } catch(e) {
            console.error('[EIP] Erro no ciclo:', e.message);
            // Micro Queue cheia = conexão CIP corrompida — reconectar limpo
            if (e.message && e.message.includes('Micro Queue')) {
              console.warn('[EIP] Micro Queue cheia — destruindo conexão e reconectando...');
              try { PLC.destroy(); } catch(_) {}
              broadcast({ tipo: 'status', online: false });
              agendarReconexao(5000);
              return; // para o loop — conectar() vai reiniciar tudo
            }
          }
          agendarProximoCiclo();
        }, CLP_CONFIG.readInterval);
      }
      agendarProximoCiclo();

      // Nomes a cada 30s — também chained para não conflitar
      let _nomesTimer;
      function agendarNomes() {
        _nomesTimer = setTimeout(async () => {
          try { await lerNomesProdutos(PLC); } catch(e) {}
          agendarNomes();
        }, 30000);
      }
      agendarNomes();
    } catch(err) {
      console.warn('[EIP] Falha:', err.message, '— retentando em 10s');
      console.warn(err.stack);
      reconectando = false; // libera a trava antes de reagendar, senão trava pra sempre
      agendarReconexao(10000);
    }
  }
  await conectar();
  async function shutdownGracioso(sinal) {
    console.log(`[SHUTDOWN] ${sinal} recebido — fechando sessão CIP e salvando dados...`);
    try { fecharTurnoCorrente(); } catch(_) {}
    try { fs.writeFileSync(DB_FILE, JSON.stringify(db), 'utf8'); } catch(_) {}
    try { salvarLogAlarmes(); } catch(_) {}
    try { salvarProducao(); } catch(_) {}
    // PLC.destroy() envia o fechamento correto da sessão CIP pro controlador
    // (ForwardClose/UnregisterSession). Sem isso, o CLP mantém a conexão
    // "presa" até o próprio timeout dele expirar — é o que estava enchendo
    // o limite de conexões do 1756-EN2TR a cada restart/crash.
    // Timeout de segurança: se o CLP não responder, não trava o shutdown.
    try {
      await Promise.race([
        PLC.destroy(),
        new Promise(resolve => setTimeout(resolve, 3000)),
      ]);
    } catch(_) {}
    process.exit(0);
  }
  process.on('SIGINT',  () => shutdownGracioso('SIGINT'));
  process.on('SIGTERM', () => shutdownGracioso('SIGTERM'));

  // Restart automático a cada 33 minutos — feito pelo PRÓPRIO processo, não
  // pelo cron do PM2 (que só divide com precisão dentro de "minutos de 0 a
  // 59 por hora" — não dá pra expressar exato um intervalo que não é
  // múltiplo de 60). O PM2 já relança o processo sozinho assim que ele sai
  // (comportamento padrão em fork mode, contanto que ninguém tenha rodado
  // "pm2 stop"), então um shutdown limpo aqui funciona como restart
  // automático preciso. Recomeça a contar do zero a cada novo processo, então
  // continua valendo depois de qualquer restart (manual, por cron antigo, ou
  // por esse próprio timer).
  setTimeout(() => shutdownGracioso('RESTART_PROGRAMADO_33MIN'), 33 * 60 * 1000);
}

// SO_REUSEADDR via listen options — permite reusar porta em TIME_WAIT sem retry
// O PM2 gerencia reinicios automaticamente; não precisamos de retry manual
server.on('error', err => {
  if (err.code === 'EADDRINUSE') {
    console.error('[ERRO] Porta 3000 em uso. Encerrando para o PM2 reiniciar.');
  } else {
    console.error('[ERRO] Servidor HTTP:', err.message);
  }
  process.exit(1);
});

server.listen({ port: 3000, exclusive: false }, async () => {
  console.log('\nDashboard: http://localhost:3000\n');
  await iniciarEthernetIP();
});

