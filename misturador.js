/**
 * Misturador 600MX022 (silo 7) — SOMENTE LEITURA do CLP, para a tela
 * public/mobile-misturador.html. Nada aqui escreve no CLP.
 *
 * Tags conferidas no L5K MOAGEM_MNL62_14092026:
 *   EA_600WF023_WT_U01_AJUSTE[9]           REAL, peso da balança do misturador (kg) — MOV do indicador ALFA
 *   M600MX022                              DINT, .5 pronto, .6 retorno de ligado, .8 disjuntor OK (0 = trip)
 *   EA_REDE_600MX022_VELOCIDADE_AJUSTE[9]  REAL, Hz do inversor CFW500
 *   EA_REDE_600MX022_CORRENTE_AJUSTE[9]    REAL, A do inversor CFW500
 *   V600FGxxx                              DINT da válvula (rotinas GR9_600FGxxx):
 *                                            .0 fechada (ZT_U02), .1 aberta (ZT_U01), .2 nenhum fim de curso,
 *                                            .4 selecionada
 *   O600FGxxxV01C01                        BOOL, comando de abrir enviado à válvula
 *   Cápsulas: 7.1 = 600FG044, 7.2 = 600FG038, 7.3 = 600FG040, 7.4 = 600FG039.
 *   Descarga: 600FG051 e 600FG052.
 *   Seletora "dosagem habilitada": tag definido pela variável de ambiente
 *   MISTURADOR_SELETORA (ainda não existe no L5K) — sem ela a tela mostra "não configurada".
 *
 * Só lê quando alguém está com a tela aberta (GET nos últimos 60 s), pra não
 * aumentar o ciclo de leitura à toa. Roda dentro do ciclo do server.js (sem
 * acesso concorrente ao socket do CLP).
 */
'use strict';

const fs      = require('fs');
const path    = require('path');
const express = require('express');

const CAPSULAS  = [
  { id: '7.1', fg: '600FG044' },
  { id: '7.2', fg: '600FG038' },
  { id: '7.3', fg: '600FG040' },
  { id: '7.4', fg: '600FG039' },
];
const DESCARGAS = ['600FG051', '600FG052'];
const TAG_PESO  = 'EA_600WF023_WT_U01_AJUSTE[9]';
const TAG_HZ    = 'EA_REDE_600MX022_VELOCIDADE_AJUSTE[9]';
const TAG_A     = 'EA_REDE_600MX022_CORRENTE_AJUSTE[9]';
const TAG_MX    = 'M600MX022';
const BALANCA_MAX = 6000, BATELADA_PADRAO = 1000, ROTULO_MAX = 20;

const bit = (v, b) => ((Number(v) | 0) & (1 << b)) !== 0;
function estadoValvula(v, cmd) {
  const aberta = bit(v, 1), fechada = bit(v, 0);
  return { aberta, fechada, indefinida: !aberta && !fechada, comandoAbrir: !!cmd,
           // comando de abrir sem fim de curso de aberta (ou o contrário) = em movimento ou falha
           divergente: (!!cmd && !aberta) || (!cmd && aberta) };
}

function criarMisturador(opts) {
  const {
    Tag,
    dir = __dirname,
    env = process.env,
    agora = () => Date.now(),
    leituraMs = 1000,
    interesseMs = 60000,
    pinConfere = () => false,
    ipConfiavel = () => false,
  } = opts;
  const ARQ = path.join(dir, 'misturador_config.json');
  let config = { capsulas: {}, batelada: BATELADA_PADRAO };
  try { config = { ...config, ...JSON.parse(fs.readFileSync(ARQ, 'utf8')) }; } catch (_) {}

  let ultimoInteresse = 0;
  let estado = null; // última leitura completa

  function lerTag(PLC, nome) {
    const tag = new Tag(nome);
    return new Promise(resolve => {
      const t = setTimeout(() => resolve(null), leituraMs);
      Promise.resolve(PLC.readTag(tag)).then(() => { clearTimeout(t); resolve(tag.value); }, () => { clearTimeout(t); resolve(null); });
    });
  }

  // Chamado pelo server.js a cada ciclo; só lê se a tela estiver aberta
  async function lerCiclo(PLC) {
    if (agora() - ultimoInteresse > interesseMs) return;
    const n = v => (v == null ? null : Number(v));
    const peso = n(await lerTag(PLC, TAG_PESO));
    const hz = n(await lerTag(PLC, TAG_HZ));
    const a = n(await lerTag(PLC, TAG_A));
    const mx = n(await lerTag(PLC, TAG_MX));
    const valvula = async fg => {
      const v = await lerTag(PLC, `V${fg}`);
      const c = await lerTag(PLC, `O${fg}V01C01`);
      return v == null ? { fg, erro: true } : { fg, ...estadoValvula(v, c) };
    };
    const capsulas = [];
    for (const c of CAPSULAS) capsulas.push({ id: c.id, rotulo: config.capsulas[c.id] || '', ...(await valvula(c.fg)) });
    const descargas = [];
    for (const fg of DESCARGAS) descargas.push(await valvula(fg));
    let seletora = null;
    if (env.MISTURADOR_SELETORA) { const s = await lerTag(PLC, env.MISTURADOR_SELETORA); seletora = s == null ? null : !!s; }
    estado = {
      ts: agora(),
      peso: peso == null ? null : Math.round(peso * 10) / 10,
      misturador: mx == null ? null : { ligado: bit(mx, 6), pronto: bit(mx, 5), falha: !bit(mx, 8),
        hz: hz == null ? null : Math.round(hz * 10) / 10, a: a == null ? null : Math.round(a * 10) / 10 },
      capsulas, descargas,
      seletora: { configurada: !!env.MISTURADOR_SELETORA, habilitada: seletora },
    };
  }

  const router = express.Router();
  router.get('/api/misturador', (req, res) => {
    ultimoInteresse = agora();
    const idade = estado ? agora() - estado.ts : null;
    res.json({ ok: true, estado, idadeMs: idade, balancaMax: BALANCA_MAX, batelada: config.batelada,
      capsulas: CAPSULAS.map(c => ({ ...c, rotulo: config.capsulas[c.id] || '' })) });
  });

  // Nome do material de cada cápsula (muda conforme a demanda) — PIN das receitas + rede confiável
  router.post('/api/misturador/rotulos', (req, res) => {
    const ip = String((req.socket && req.socket.remoteAddress) || '').replace(/^::ffff:/, '');
    if (env.RECEITAS_RESTRINGIR_REDE !== '0' && !ipConfiavel(ip))
      return res.status(403).json({ ok: false, erro: 'rede_nao_confiavel', mensagem: 'Só a partir da rede da fábrica / Tailscale.' });
    if (!env.RECEITAS_PIN || !pinConfere(req.get('x-receitas-pin'), env.RECEITAS_PIN))
      return res.status(401).json({ ok: false, erro: 'pin_invalido', mensagem: 'PIN incorreto.' });
    const b = req.body || {};
    const novos = {};
    for (const c of CAPSULAS) {
      if (b.rotulos == null || !(c.id in b.rotulos)) { novos[c.id] = config.capsulas[c.id] || ''; continue; }
      const t = String(b.rotulos[c.id] || '').trim().toUpperCase();
      if (t.length > ROTULO_MAX || !/^[\x20-\x7EÀ-ÿ]*$/.test(t))
        return res.status(400).json({ ok: false, erro: 'rotulo_invalido', mensagem: `Nome da cápsula ${c.id}: até ${ROTULO_MAX} caracteres.` });
      novos[c.id] = t;
    }
    config.capsulas = novos;
    fs.writeFile(ARQ, JSON.stringify(config, null, 2), 'utf8', e => { if (e) console.error('[MISTURADOR] erro salvando config:', e.message); });
    if (estado) estado.capsulas.forEach(c => { c.rotulo = novos[c.id]; });
    res.json({ ok: true, rotulos: novos });
  });

  return { router, lerCiclo, _config: () => config };
}

module.exports = criarMisturador;
module.exports.estadoValvula = estadoValvula;
module.exports.CAPSULAS = CAPSULAS;
