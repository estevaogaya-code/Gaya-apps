/**
 * Receitas dos moinhos 501/502 — leitura e escrita dos percentuais no CLP.
 *
 * Tags (escopo controlador, L = 501 | 502), conferidas no L5K MOAGEM_MNL62_14092026:
 *   PERCENTUAL_RECEITA_L[1..9]   REAL, fração 0–1 de cada componente (ESCRITA)
 *   PERCENTUAL_RECEITA_L[0]      REAL, soma calculada pelo CLP (CPT em GR4_RECEITA_L) — só leitura
 *   PERCENTUAL_RECEITA_L[10]     REAL, base de massa do lote (12000) — só leitura
 *   VA_RECEITAS_LWF005[2]        REAL, nº de componentes (PRE do contador = [2]+1) (ESCRITA)
 *                                Também lido em GR7_50xDG004_SILO_4: GEQ([2],4) de QUALQUER
 *                                linha participa do comando O501DG004V01C01 e O502DG004V01C01.
 *   VD_MR_RECEITAS_L[0]          INT, .2 = ciclo de pesagem WF005, .5 = ciclo WF010
 *                                (OTL no início do lote, OTU no fim .3 ou reset .15)
 *   COUNTER_MR_RECEITAS_L[0].ACC componente em dosagem (0 = parado) — só leitura
 *   VA_L_UMIDADE[1..9]           REAL, umidade em % do componente i (ESCRITA, junto com a receita).
 *                                Entra no setpoint: [i] * [10] * (1 + umidade/100), então mudar
 *                                no meio do lote tem o mesmo efeito de mudar o percentual.
 *   NOME_PRODUTO0i_L             STRING (LEN DINT + DATA SINT[82]). Não é usado em lógica do
 *                                ladder, só exibido. Gravado membro a membro (.DATA[k] SINT e
 *                                .LEN DINT), porque o st-ethernet-ip lê STRING como estrutura
 *                                crua e não grava com Tag simples.
 *
 * Acesso ao CLP: NUNCA direto. Toda operação entra numa fila que o server.js
 * drena no próprio ciclo de leitura (processarFila(PLC) logo antes de
 * lerTodosOsTags), então não há acesso concorrente ao socket com o ciclo
 * principal. A leitura de nomes (timer de 30 s) usa exclusivo() pra não
 * cruzar com a drenagem. O CLP não valida nada da receita — toda a
 * validação está aqui.
 */
'use strict';

const fs      = require('fs');
const path    = require('path');
const crypto  = require('crypto');
const express = require('express');

const LINHAS = ['501', '502'];
const N_COMP = 9;
const UMIDADE_MAX = 30;      // % — faixa aceita para a umidade de cada componente
const NOME_MAX = 12;         // caracteres — o que o supervisório exibe
const STRING_DATA_MAX = 82;  // tamanho do DATA do STRING padrão Logix
const SINT = 0xC2, DINT = 0xC4; // códigos CIP, para criar tags sem leitura prévia

// ─── Funções puras ────────────────────────────────────────────────────────────

function tagsDaLinha(L) {
  return {
    pct:    Array.from({ length: 11 }, (_, i) => `PERCENTUAL_RECEITA_${L}[${i}]`),
    n:      `VA_RECEITAS_${L}WF005[2]`,
    estado: `VD_MR_RECEITAS_${L}[0]`,
    passo:  `COUNTER_MR_RECEITAS_${L}[0].ACC`,
    umidade: Array.from({ length: N_COMP }, (_, i) => `VA_${L}_UMIDADE[${i + 1}]`),
    nome:   i => `NOME_PRODUTO0${i + 1}_${L}`,
  };
}

// Percentual (0–100, até 2 casas) -> inteiro em centésimos; null se inválido
function paraCentesimos(v) {
  if (typeof v !== 'number' || !Number.isFinite(v)) return null;
  const c = Math.round(v * 100);
  if (Math.abs(v * 100 - c) > 1e-6) return null;
  return c === 0 ? 0 : c; // normaliza -0
}

// Fração lida do CLP -> centésimos de percentual
function fracaoParaCentesimos(f) { return Math.round(Number(f) * 10000); }

function validarPct(pct) {
  if (!Array.isArray(pct) || pct.length !== N_COMP)
    return { erro: 'pct_invalido', detalhe: `pct deve ter ${N_COMP} valores` };
  const c = [];
  for (let i = 0; i < N_COMP; i++) {
    const v = paraCentesimos(pct[i]);
    if (v === null) return { erro: 'pct_invalido', detalhe: `Componente ${i + 1}: número com até 2 casas decimais` };
    if (v < 0 || v > 10000) return { erro: 'pct_fora_da_faixa', detalhe: `Componente ${i + 1}: deve estar entre 0 e 100` };
    c.push(v);
  }
  const soma = c.reduce((a, b) => a + b, 0);
  if (soma !== 10000)
    return { erro: 'soma_diferente_de_100', detalhe: `Soma = ${(soma / 100).toFixed(2)}% (precisa ser 100,00%)` };
  let n = 0;
  for (let i = 0; i < N_COMP; i++) if (c[i] > 0) n = i + 1;
  for (let i = 0; i < n; i++)
    if (c[i] === 0) return { erro: 'componente_vazio_no_meio', detalhe: `Componente ${i + 1} está em 0 antes do componente ${n} — os componentes usados precisam ser contíguos a partir do 1` };
  return { ok: true, centesimos: c, n };
}

// Umidade (0–UMIDADE_MAX %, até 2 casas) -> centésimos. Ausente = não altera.
function validarUmidade(u) {
  if (!Array.isArray(u) || u.length !== N_COMP)
    return { erro: 'umidade_invalida', detalhe: `umidade deve ter ${N_COMP} valores` };
  const c = [];
  for (let i = 0; i < N_COMP; i++) {
    const v = paraCentesimos(u[i]);
    if (v === null) return { erro: 'umidade_invalida', detalhe: `Umidade do componente ${i + 1}: número com até 2 casas decimais` };
    if (v < 0 || v > UMIDADE_MAX * 100) return { erro: 'umidade_fora_da_faixa', detalhe: `Umidade do componente ${i + 1}: deve estar entre 0 e ${UMIDADE_MAX}%` };
    c.push(v);
  }
  return { ok: true, centesimos: c };
}

// Nome de componente: ASCII imprimível, maiúsculas, até NOME_MAX (vazio permitido)
function normalizarNome(s) {
  if (typeof s !== 'string') return null;
  const t = s.trim().toUpperCase();
  if (t.length > NOME_MAX || !/^[\x20-\x7E]*$/.test(t)) return null;
  return t;
}

// Bytes lidos do DATA do STRING -> texto, com a mesma limpeza do server.js
function textoDeBytes(bytes) {
  return String.fromCharCode(...bytes.map(b => b & 0xFF)).replace(/[\x00-\x1F\x7F]/g, '').trim();
}

// Normaliza IP do socket (remove prefixo IPv4-mapeado)
function normalizarIp(ip) {
  if (!ip) return '';
  ip = String(ip);
  if (ip.startsWith('::ffff:') && ip.includes('.')) ip = ip.slice(7);
  return ip;
}

function ipv4ParaInt(ip) {
  const p = ip.split('.');
  if (p.length !== 4) return null;
  let n = 0;
  for (const s of p) {
    if (!/^\d{1,3}$/.test(s)) return null;
    const v = Number(s);
    if (v > 255) return null;
    n = n * 256 + v;
  }
  return n;
}

const REDES_V4 = [
  ['127.0.0.0', 8], ['10.0.0.0', 8], ['172.16.0.0', 12], ['192.168.0.0', 16],
  ['100.64.0.0', 10], // Tailscale (CGNAT)
].map(([base, bits]) => ({ base: ipv4ParaInt(base), bits }));

function ipConfiavel(ipBruto) {
  const ip = normalizarIp(ipBruto);
  if (ip === '::1') return true;
  if (ip.includes(':')) {
    // Tailscale IPv6 (fd7a:115c:a1e0::/48)
    return /^fd7a:115c:a1e0:/i.test(ip);
  }
  const n = ipv4ParaInt(ip);
  if (n === null) return false;
  return REDES_V4.some(({ base, bits }) => {
    const div = 2 ** (32 - bits);
    return Math.floor(n / div) === Math.floor(base / div);
  });
}

function pinConfere(recebido, esperado) {
  if (typeof recebido !== 'string' || !esperado) return false;
  const a = crypto.createHash('sha256').update(recebido).digest();
  const b = crypto.createHash('sha256').update(String(esperado)).digest();
  return crypto.timingSafeEqual(a, b);
}

function erroHttp(status, erro, extra = {}) {
  const e = new Error(extra.mensagem || erro);
  e.status = status; e.corpo = { ok: false, erro, ...extra };
  return e;
}

// ─── Fábrica ──────────────────────────────────────────────────────────────────

function criarReceitas(opts) {
  const {
    Tag, TagGroup,
    dir           = __dirname,
    getNomes      = () => ({}),
    onNomes       = () => {},  // (linha, nomes[9]) depois de gravar nomes — atualiza o cache do server.js
    env           = process.env,
    agora         = () => Date.now(),
    timeoutMs     = 25000,   // tempo máximo de uma operação (fila + execução)
    offlineMs     = 30000,   // sem drenagem há mais que isso => CLP considerado offline
    esperaLockMs  = 5000,    // drenagem desiste do ciclo se a leitura de nomes não liberar
    leituraMs     = 2000,    // timeout por leitura de tag
    pausaReleituraMs = 300,
    maxFalhasPin  = 5,
    bloqueioMs    = 10 * 60 * 1000,
  } = opts;

  const ARQ_PRESETS = path.join(dir, 'receitas_presets.json');
  const ARQ_LOG     = path.join(dir, 'receitas_log.jsonl');
  const restringirRede = env.RECEITAS_RESTRINGIR_REDE !== '0';
  const pinEsperado = () => (env.RECEITAS_PIN ? String(env.RECEITAS_PIN) : '');
  const sleep = ms => new Promise(r => setTimeout(r, ms));

  // ── Exclusão mútua com a leitura de nomes ──────────────────────────────────
  let emUso = null;
  async function exclusivo(fn, esperaMax = Infinity) {
    const limite = agora() + esperaMax;
    while (emUso) {
      const resta = limite - agora();
      if (resta <= 0) return { naoAdquirido: true };
      await Promise.race([emUso.catch(() => {}), esperaMax === Infinity ? new Promise(() => {}) : sleep(resta)]);
    }
    const p = Promise.resolve().then(fn);
    emUso = p;
    try { return await p; } finally { if (emUso === p) emUso = null; }
  }

  // ── Fila de operações no CLP ───────────────────────────────────────────────
  const fila = [];
  let ultimoDreno = 0;
  const clpOnline = () => ultimoDreno > 0 && agora() - ultimoDreno < offlineMs;

  function enfileirar(nome, fn) {
    if (!clpOnline()) return Promise.reject(erroHttp(504, 'clp_offline', { mensagem: 'CLP offline ou sem comunicação — nada foi executado.' }));
    return new Promise((resolve, reject) => {
      const job = { nome, fn, estado: 'fila', cancelado: false, escritaEnviada: false, fim: false };
      const terminar = (cb, v) => { if (job.fim) return; job.fim = true; clearTimeout(job.timer); cb(v); };
      job.ok = v => terminar(resolve, v);
      job.falha = e => terminar(reject, e);
      job.timer = setTimeout(() => {
        job.cancelado = true;
        if (job.estado === 'fila') {
          const i = fila.indexOf(job); if (i >= 0) fila.splice(i, 1);
          job.falha(erroHttp(504, 'timeout', { executado: false, etapa: 'fila', mensagem: 'Tempo esgotado aguardando o CLP — a operação foi cancelada e NÃO será executada.' }));
        } else {
          job.falha(erroHttp(504, 'timeout', {
            executado: job.escritaEnviada ? 'incerto' : false,
            mensagem: job.escritaEnviada
              ? 'Tempo esgotado depois de enviar a escrita — o resultado é incerto. Confira no supervisório e no log.'
              : 'Tempo esgotado — a escrita não foi enviada e não será.',
          }));
        }
      }, timeoutMs);
      fila.push(job);
    });
  }

  async function processarFila(PLC) {
    ultimoDreno = agora();
    if (!fila.length) return;
    try {
      const r = await exclusivo(async () => {
        while (fila.length) {
          const job = fila.shift();
          if (job.cancelado) continue;
          job.estado = 'executando';
          try { job.ok(await job.fn(PLC, job)); }
          catch (e) { job.falha(e); }
        }
      }, esperaLockMs);
      if (r && r.naoAdquirido) console.warn('[RECEITAS] leitura de nomes ocupando o CLP — fila fica pro próximo ciclo');
    } catch (e) {
      console.error('[RECEITAS] erro drenando fila:', e.message);
    }
    ultimoDreno = agora();
  }

  // ── Leitura ────────────────────────────────────────────────────────────────
  function lerTag(PLC, nome) {
    const tag = new Tag(nome);
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(erroHttp(504, 'falha_leitura_clp', { tag: nome, mensagem: `Timeout lendo ${nome}` })), leituraMs);
      Promise.resolve(PLC.readTag(tag)).then(() => {
        clearTimeout(t);
        if (tag.value == null || (typeof tag.value === 'number' && !Number.isFinite(tag.value)))
          return reject(erroHttp(504, 'falha_leitura_clp', { tag: nome, mensagem: `Valor inválido em ${nome}` }));
        resolve(tag);
      }, e => {
        clearTimeout(t);
        reject(erroHttp(504, 'falha_leitura_clp', { tag: nome, mensagem: `Erro lendo ${nome}: ${e && e.message}` }));
      });
    });
  }

  async function lerLinha(PLC, L) {
    const nomes = tagsDaLinha(L);
    const pctTags = [];
    for (const n of nomes.pct) pctTags.push(await lerTag(PLC, n));
    const nTag = await lerTag(PLC, nomes.n);
    const estTag = await lerTag(PLC, nomes.estado);
    const umTags = [];
    for (const n of nomes.umidade) umTags.push(await lerTag(PLC, n));
    const valores = extrairValores(pctTags, nTag, estTag);
    valores.umidadeBruta = umTags.map(t => Number(t.value));
    valores.umidadeC = valores.umidadeBruta.map(v => Math.round(v * 100));
    return { tags: { pct: pctTags, n: nTag, estado: estTag, umidade: umTags }, valores };
  }

  function extrairValores(pctTags, nTag, estTag) {
    const fr = pctTags.map(t => Number(t.value));
    const est = Number(estTag.value) | 0;
    const wf005 = (est & 0x04) !== 0, wf010 = (est & 0x20) !== 0;
    return {
      fracoes: fr.slice(1, 10),
      centesimos: fr.slice(1, 10).map(fracaoParaCentesimos),
      n: Number(nTag.value),
      soma: fr[0],
      base: fr[10],
      pesando: wf005 || wf010, pesandoWf005: wf005, pesandoWf010: wf010,
    };
  }

  function formatarLinha(L, v) {
    return {
      linha: L,
      pct: v.centesimos.map(c => c / 100),
      n: Number.isInteger(v.n) ? v.n : Number(v.n.toFixed(3)),
      soma: v.soma,
      somaPct: Math.round(v.soma * 10000) / 100,
      base: v.base,
      umidade: (v.umidadeC || []).map(c => c / 100),
      pesando: v.pesando, pesandoWf005: v.pesandoWf005, pesandoWf010: v.pesandoWf010,
      nomes: ((getNomes() || {})[L] || []).slice(0, N_COMP),
    };
  }

  // ── Presets ────────────────────────────────────────────────────────────────
  let presets = { 501: [], 502: [] };
  try {
    if (fs.existsSync(ARQ_PRESETS)) {
      const p = JSON.parse(fs.readFileSync(ARQ_PRESETS, 'utf8'));
      for (const L of LINHAS) if (Array.isArray(p[L])) presets[L] = p[L];
    }
  } catch (e) { console.warn('[RECEITAS] presets ilegíveis, ignorando:', e.message); }

  let gravandoPresets = Promise.resolve();
  function salvarPresets() {
    const dados = JSON.stringify(presets, null, 2);
    gravandoPresets = gravandoPresets.then(async () => {
      const tmp = ARQ_PRESETS + '.tmp';
      await fs.promises.writeFile(tmp, dados, 'utf8');
      await fs.promises.rename(tmp, ARQ_PRESETS);
    }).catch(e => console.error('[RECEITAS] erro salvando presets:', e.message));
    return gravandoPresets;
  }

  // ── Auditoria ──────────────────────────────────────────────────────────────
  let gravandoLog = Promise.resolve();
  function registrarLog(reg) {
    const linha = JSON.stringify({ ts: new Date(agora()).toISOString(), ...reg }) + '\n';
    gravandoLog = gravandoLog.then(() => fs.promises.appendFile(ARQ_LOG, linha, 'utf8'))
      .catch(e => console.error('[RECEITAS] erro gravando log:', e.message));
    return gravandoLog;
  }

  async function lerLog(limit) {
    let txt = '';
    try { txt = await fs.promises.readFile(ARQ_LOG, 'utf8'); } catch (_) { return []; }
    const linhas = txt.split('\n').filter(Boolean);
    const out = [];
    for (let i = linhas.length - 1; i >= 0 && out.length < limit; i--) {
      try { out.push(JSON.parse(linhas[i])); } catch (_) {}
    }
    return out;
  }

  // ── Segurança ──────────────────────────────────────────────────────────────
  const tentativas = new Map(); // ip -> { falhas, ultima, bloqueadoAte }

  const ipDe = req => normalizarIp(req.socket && req.socket.remoteAddress);

  function redeConfiavel(req, res, next) {
    if (restringirRede && !ipConfiavel(ipDe(req)))
      return res.status(403).json({ ok: false, erro: 'rede_nao_confiavel', mensagem: 'Escrita permitida só a partir da rede local/Tailscale.' });
    next();
  }

  function exigirPin(req, res, next) {
    if (!pinEsperado())
      return res.status(503).json({ ok: false, erro: 'escrita_desabilitada', mensagem: 'RECEITAS_PIN não configurado no servidor — escrita desabilitada.' });
    const ip = ipDe(req);
    const t = tentativas.get(ip);
    if (t && t.bloqueadoAte > agora()) {
      const seg = Math.ceil((t.bloqueadoAte - agora()) / 1000);
      res.set('Retry-After', String(seg));
      return res.status(429).json({ ok: false, erro: 'bloqueado', segundos: seg, mensagem: `Muitas tentativas de PIN. Tente de novo em ${Math.ceil(seg / 60)} min.` });
    }
    if (pinConfere(req.get('x-receitas-pin'), pinEsperado())) {
      tentativas.delete(ip);
      return next();
    }
    const reg = (t && agora() - t.ultima < bloqueioMs) ? t : { falhas: 0, ultima: 0, bloqueadoAte: 0 };
    reg.falhas++; reg.ultima = agora();
    if (reg.falhas >= maxFalhasPin) { reg.bloqueadoAte = agora() + bloqueioMs; reg.falhas = 0; }
    tentativas.set(ip, reg);
    registrarLog({ ip, rota: req.path, resultado: 'pin_invalido' });
    if (reg.bloqueadoAte > agora()) {
      res.set('Retry-After', String(Math.ceil(bloqueioMs / 1000)));
      return res.status(429).json({ ok: false, erro: 'bloqueado', segundos: Math.ceil(bloqueioMs / 1000), mensagem: 'PIN incorreto. Acesso bloqueado por 10 min.' });
    }
    return res.status(401).json({ ok: false, erro: 'pin_invalido', restantes: maxFalhasPin - reg.falhas, mensagem: 'PIN incorreto.' });
  }

  const protegido = [redeConfiavel, exigirPin];

  function responderErro(res, e) {
    if (e && e.status) return res.status(e.status).json(e.corpo);
    console.error('[RECEITAS]', e);
    return res.status(500).json({ ok: false, erro: 'erro_interno', mensagem: e && e.message });
  }

  const validarLinha = L => LINHAS.includes(String(L));

  // ── Operação de gravação (roda dentro da fila) ─────────────────────────────
  async function gravar(PLC, job, { L, alvo, umAlvo, anterior, forcar, usuario, ip }) {
    const log = { ip, usuario, linha: L, depois: { pct: alvo.centesimos.map(c => c / 100), n: alvo.n }, forcar: !!forcar };
    if (umAlvo) log.depois.umidade = umAlvo.map(c => c / 100);
    let antes;
    try { antes = await lerLinha(PLC, L); }
    catch (e) { registrarLog({ ...log, resultado: 'falha_leitura' }); throw e; }
    const va = antes.valores;
    log.antes = { pct: va.centesimos.map(c => c / 100), n: va.n, umidade: va.umidadeC.map(c => c / 100), soma: va.soma, pesando: va.pesando };
    const atual = formatarLinha(L, va);

    // Controle otimista: o que a tela mostrava ainda é o que está no CLP?
    const antC = anterior.pct.map(paraCentesimos);
    const mudou = antC.some((c, i) => c !== va.centesimos[i]) || !(Math.abs(Number(anterior.n) - va.n) < 1e-3)
      || (umAlvo && anterior.umidade.map(paraCentesimos).some((c, i) => c !== va.umidadeC[i]));
    if (mudou) {
      registrarLog({ ...log, resultado: 'conflito' });
      throw erroHttp(409, 'conflito', { atual, mensagem: 'A receita no CLP mudou desde que a tela foi carregada (alguém alterou). Recarregue e confira.' });
    }

    if (va.pesando) {
      if (!forcar) {
        registrarLog({ ...log, resultado: 'pesagem_em_andamento' });
        throw erroHttp(409, 'pesagem_em_andamento', { atual, mensagem: 'Pesagem em andamento — os componentes ainda não dosados deste lote usariam a nova receita.' });
      }
      // Forçando no meio do lote com nº de componentes diferente: se o passo
      // atual já passou do novo PRE (n+1), EQU(ACC,PRE) nunca fecha o lote.
      if (alvo.n !== Math.round(va.n)) {
        let passo = null;
        try { passo = Number((await lerTag(PLC, tagsDaLinha(L).passo)).value); } catch (_) {}
        if (passo === null || !Number.isFinite(passo) || passo > alvo.n) {
          registrarLog({ ...log, passo, resultado: 'forcar_recusado_passo' });
          throw erroHttp(409, 'forcar_recusado_passo', { atual, passo, mensagem: passo === null
            ? 'Não foi possível ler o componente em dosagem — não dá pra forçar mudança do nº de componentes durante a pesagem.'
            : `O lote está no componente ${passo}; com ${alvo.n} componente(s) o CLP não fecharia o lote. Aguarde o fim da pesagem.` });
        }
        log.passo = passo;
      }
    }

    if (job.cancelado) {
      registrarLog({ ...log, resultado: 'timeout_nao_executado' });
      throw erroHttp(504, 'timeout', { executado: false, mensagem: 'Tempo esgotado — a escrita não foi enviada.' });
    }

    // Escrita única: pct[1..9] + nº de componentes
    const grupo = new TagGroup();
    for (let i = 1; i <= N_COMP; i++) {
      const t = antes.tags.pct[i];
      t.value = Math.fround(alvo.centesimos[i - 1] / 10000);
      grupo.add(t);
    }
    antes.tags.n.value = alvo.n;
    grupo.add(antes.tags.n);
    if (umAlvo) for (let i = 0; i < N_COMP; i++) {
      const t = antes.tags.umidade[i];
      t.value = Math.fround(umAlvo[i] / 100);
      grupo.add(t);
    }

    let erroEscrita = null;
    job.escritaEnviada = true;
    try { await PLC.writeTagGroup(grupo); }
    catch (e) { erroEscrita = (e && e.message) || String(e) || 'erro'; if (typeof e === 'object' && e && e.generalStatusCode != null) erroEscrita = `CIP status ${e.generalStatusCode}${e.extendedStatus ? ' ' + JSON.stringify(e.extendedStatus) : ''}`; }

    await sleep(pausaReleituraMs);

    let depois = null, erroReleitura = null;
    try { depois = (await lerLinha(PLC, L)).valores; }
    catch (e) { erroReleitura = e.corpo ? e.corpo.mensagem : e.message; }

    const divergencias = [];
    if (depois) {
      for (let i = 0; i < N_COMP; i++) {
        const esperado = Math.fround(alvo.centesimos[i] / 10000);
        if (Math.fround(depois.fracoes[i]) !== esperado)
          divergencias.push({ componente: i + 1, esperado: alvo.centesimos[i] / 100, lido: Math.round(depois.fracoes[i] * 1e6) / 1e4 });
      }
      if (Math.fround(depois.n) !== Math.fround(alvo.n)) divergencias.push({ campo: 'n', esperado: alvo.n, lido: depois.n });
      if (umAlvo) for (let i = 0; i < N_COMP; i++)
        if (Math.fround(depois.umidadeBruta[i]) !== Math.fround(umAlvo[i] / 100))
          divergencias.push({ campo: 'umidade', componente: i + 1, esperado: umAlvo[i] / 100, lido: Math.round(depois.umidadeBruta[i] * 1e4) / 1e4 });
      if (!(Math.abs(depois.soma - 1) <= 1e-4)) divergencias.push({ campo: 'soma_clp', esperado: 1, lido: depois.soma });
    }

    const lido = depois ? formatarLinha(L, depois) : null;
    const resultado = erroEscrita ? 'falha_escrita' : !depois ? 'falha_releitura' : divergencias.length ? 'divergencia' : 'ok';
    registrarLog({ ...log, resultado, erroEscrita, erroReleitura, divergencias,
      leitura: depois ? { pct: lido.pct, n: depois.n, umidade: lido.umidade, soma: depois.soma } : null });

    if (resultado === 'ok') return { ok: true, linha: L, lido, mensagem: 'Receita gravada e conferida no CLP.' };
    throw erroHttp(502, resultado, {
      lido, divergencias, erroEscrita, erroReleitura,
      mensagem: 'A gravação NÃO foi confirmada. Confira a receita no supervisório antes de seguir. (Não há desfazer automático.)',
    });
  }

  // ── Nomes dos componentes (roda dentro da fila) ────────────────────────────
  // Lê LEN e DATA[0..LEN-1] do STRING pelos membros atômicos.
  async function lerNome(PLC, L, i) {
    const base = tagsDaLinha(L).nome(i);
    const lenTag = await lerTag(PLC, `${base}.LEN`);
    const len = Math.max(0, Math.min(STRING_DATA_MAX, Number(lenTag.value) | 0));
    const dataTags = [];
    for (let k = 0; k < len; k++) dataTags.push(await lerTag(PLC, `${base}.DATA[${k}]`));
    return { lenTag, dataTags, len, texto: textoDeBytes(dataTags.map(t => Number(t.value))) };
  }

  async function gravarNomes(PLC, job, { L, alvo, anterior, usuario, ip }) {
    const atuais = [...(((getNomes() || {})[L]) || Array(N_COMP).fill(''))].slice(0, N_COMP);
    while (atuais.length < N_COMP) atuais.push('');
    const mudar = [];
    for (let i = 0; i < N_COMP; i++) if (alvo[i] !== anterior[i]) mudar.push(i);
    const log = { ip, usuario, linha: L, rota: 'nomes', antes: {}, depois: {} };
    mudar.forEach(i => { log.antes[i + 1] = anterior[i]; log.depois[i + 1] = alvo[i]; });

    // Lê do CLP só os que vão mudar e confere com o que a tela mostrava
    const lidos = {};
    try { for (const i of mudar) lidos[i] = await lerNome(PLC, L, i); }
    catch (e) { registrarLog({ ...log, resultado: 'falha_leitura' }); throw e; }
    // Conflito só se o nome mudou de verdade. A tela mostra o cache do server.js,
    // que monta o texto com o DATA inteiro (sem olhar o LEN); aqui a leitura é
    // pelo LEN. Quando LEN e DATA não batem (ex.: LEN 0 com texto antigo no
    // DATA), os dois diferem para sempre — então vale o que a tela mostrava
    // se ainda é o que está no cache.
    const cache = atuais.map(x => String(x || '').trim());
    const conflitos = mudar.filter(i => lidos[i].texto !== anterior[i] && cache[i] !== anterior[i]);
    const detalhes = mudar.filter(i => lidos[i].texto !== anterior[i])
      .map(i => ({ componente: i + 1, tela: anterior[i], clp: lidos[i].texto, len: lidos[i].len, cache: cache[i] }));
    if (detalhes.length) log.leituraClp = detalhes;
    if (conflitos.length) {
      conflitos.forEach(i => { atuais[i] = lidos[i].texto; });
      registrarLog({ ...log, resultado: 'conflito' });
      throw erroHttp(409, 'conflito', { nomes: atuais, detalhes: detalhes.filter(d => conflitos.includes(d.componente - 1)),
        mensagem: 'O nome no CLP mudou desde que a tela foi carregada (alguém alterou). Recarregue e confira.' });
    }
    if (job.cancelado) {
      registrarLog({ ...log, resultado: 'timeout_nao_executado' });
      throw erroHttp(504, 'timeout', { executado: false, mensagem: 'Tempo esgotado — a escrita não foi enviada.' });
    }

    // DATA[k] = caractere novo e 0 no resto dos 82 (o server.js monta o nome
    // lendo o DATA inteiro, sem olhar o LEN); depois LEN
    const grupo = new TagGroup();
    for (const i of mudar) {
      const base = tagsDaLinha(L).nome(i), { lenTag, dataTags } = lidos[i];
      const bytes = Buffer.from(alvo[i], 'latin1');
      for (let k = 0; k < STRING_DATA_MAX; k++) {
        const t = k < dataTags.length ? dataTags[k] : new Tag(`${base}.DATA[${k}]`, null, SINT);
        t.value = k < bytes.length ? bytes[k] : 0;
        grupo.add(t);
      }
      lenTag.value = bytes.length;
      grupo.add(lenTag);
    }
    let erroEscrita = null;
    job.escritaEnviada = true;
    try { await PLC.writeTagGroup(grupo); }
    catch (e) { erroEscrita = (e && e.generalStatusCode != null) ? `CIP status ${e.generalStatusCode}` : ((e && e.message) || String(e)); }

    await sleep(pausaReleituraMs);
    const divergencias = [];
    let erroReleitura = null;
    try {
      for (const i of mudar) {
        const r = await lerNome(PLC, L, i);
        atuais[i] = r.texto;
        if (r.texto !== alvo[i] || r.len !== alvo[i].length) divergencias.push({ componente: i + 1, esperado: alvo[i], lido: r.texto });
      }
    } catch (e) { erroReleitura = e.corpo ? e.corpo.mensagem : e.message; }

    const resultado = erroEscrita ? 'falha_escrita' : erroReleitura ? 'falha_releitura' : divergencias.length ? 'divergencia' : 'ok';
    registrarLog({ ...log, resultado, erroEscrita, erroReleitura, divergencias });
    if (!erroReleitura) { try { onNomes(L, atuais); } catch (e) { console.error('[RECEITAS] onNomes:', e.message); } }
    if (resultado === 'ok') return { ok: true, linha: L, nomes: atuais, mensagem: 'Nomes gravados e conferidos no CLP.' };
    throw erroHttp(502, resultado, { nomes: erroReleitura ? null : atuais, divergencias, erroEscrita, erroReleitura,
      mensagem: 'A gravação dos nomes NÃO foi confirmada. Confira no supervisório.' });
  }

  // ── Rotas ──────────────────────────────────────────────────────────────────
  const router = express.Router();

  router.get('/api/receitas', async (req, res) => {
    try {
      const linhas = await enfileirar('ler', async PLC => {
        const out = {};
        for (const L of LINHAS) out[L] = formatarLinha(L, (await lerLinha(PLC, L)).valores);
        return out;
      });
      res.json({ ok: true, ts: agora(), linhas, presets, escritaHabilitada: !!pinEsperado() });
    } catch (e) { responderErro(res, e); }
  });

  router.get('/api/receitas/log', async (req, res) => {
    const limit = Math.min(Math.max(parseInt(req.query.limit) || 100, 1), 1000);
    res.json(await lerLog(limit));
  });

  router.get('/api/receitas/presets', (req, res) => res.json(presets));

  router.post('/api/receitas/auth', ...protegido, (req, res) => res.json({ ok: true }));

  router.post('/api/receitas/presets/:linha', ...protegido, async (req, res) => {
    const L = req.params.linha;
    if (!validarLinha(L)) return res.status(400).json({ ok: false, erro: 'linha_invalida' });
    const b = req.body || {};
    const nome = typeof b.nome === 'string' ? b.nome.trim() : '';
    const usuario = typeof b.usuario === 'string' ? b.usuario.trim() : '';
    if (!nome || nome.length > 40) return res.status(400).json({ ok: false, erro: 'nome_invalido', mensagem: 'Nome do preset: 1 a 40 caracteres.' });
    if (!usuario || usuario.length > 60) return res.status(400).json({ ok: false, erro: 'usuario_obrigatorio', mensagem: 'Informe o operador.' });
    const v = validarPct(b.pct);
    if (!v.ok) return res.status(400).json({ ok: false, ...v, mensagem: v.detalhe });
    let vu = null;
    if (b.umidade !== undefined) {
      vu = validarUmidade(b.umidade);
      if (!vu.ok) return res.status(400).json({ ok: false, ...vu, mensagem: vu.detalhe });
    }
    const lista = presets[L].filter(p => p.nome !== nome);
    if (lista.length >= 50) return res.status(400).json({ ok: false, erro: 'limite_presets', mensagem: 'Máximo de 50 presets por linha.' });
    const preset = { nome, pct: v.centesimos.map(c => c / 100), n: v.n, usuario, ts: new Date(agora()).toISOString() };
    if (vu) preset.umidade = vu.centesimos.map(c => c / 100);
    lista.push(preset);
    lista.sort((a, b2) => a.nome.localeCompare(b2.nome, 'pt-BR'));
    presets = { ...presets, [L]: lista };
    await salvarPresets();
    registrarLog({ ip: ipDe(req), usuario, linha: L, rota: 'preset_salvar', preset: nome, resultado: 'ok' });
    res.json({ ok: true, presets });
  });

  router.delete('/api/receitas/presets/:linha/:nome', ...protegido, async (req, res) => {
    const L = req.params.linha;
    if (!validarLinha(L)) return res.status(400).json({ ok: false, erro: 'linha_invalida' });
    const nome = req.params.nome;
    if (!presets[L].some(p => p.nome === nome)) return res.status(404).json({ ok: false, erro: 'preset_nao_encontrado' });
    presets = { ...presets, [L]: presets[L].filter(p => p.nome !== nome) };
    await salvarPresets();
    const usuario = typeof req.query.usuario === 'string' ? req.query.usuario.slice(0, 60) : '';
    registrarLog({ ip: ipDe(req), usuario, linha: L, rota: 'preset_excluir', preset: nome, resultado: 'ok' });
    res.json({ ok: true, presets });
  });

  const registrarChegada = (req, res, next) => {
    console.log(`[RECEITAS] ${req.method} ${req.path} de ${ipDe(req)}`);
    next();
  };

  // Nomes não entram em lógica do ladder: não exige pesagem parada
  router.post('/api/receitas/:linha/nomes', registrarChegada, ...protegido, async (req, res) => {
    const L = req.params.linha;
    if (!validarLinha(L)) return res.status(400).json({ ok: false, erro: 'linha_invalida', mensagem: 'Linha deve ser 501 ou 502.' });
    const b = req.body || {};
    const usuario = typeof b.usuario === 'string' ? b.usuario.trim() : '';
    if (!usuario || usuario.length > 60) return res.status(400).json({ ok: false, erro: 'usuario_obrigatorio', mensagem: 'Informe o operador (até 60 caracteres).' });
    if (!Array.isArray(b.nomes) || b.nomes.length !== N_COMP || !Array.isArray(b.anterior) || b.anterior.length !== N_COMP || b.anterior.some(x => typeof x !== 'string'))
      return res.status(400).json({ ok: false, erro: 'nomes_invalidos', mensagem: `Envie nomes[${N_COMP}] e anterior[${N_COMP}].` });
    // Só normaliza o que o usuário mexeu; nome não alterado fica exatamente como está no CLP
    const anterior = b.anterior.map(x => x.trim());
    const alvo = b.nomes.map((x, i) => (typeof x === 'string' && x.trim() === anterior[i]) ? anterior[i] : normalizarNome(x));
    const ruim = alvo.findIndex(x => x === null);
    if (ruim >= 0) return res.status(400).json({ ok: false, erro: 'nome_invalido', componente: ruim + 1, mensagem: `Nome do componente ${ruim + 1}: até ${NOME_MAX} caracteres, só letras, números e símbolos comuns (sem acento).` });
    if (alvo.every((x, i) => x === anterior[i])) return res.status(400).json({ ok: false, erro: 'sem_alteracao', mensagem: 'Nenhum nome mudou.' });
    const ip = ipDe(req);
    try {
      res.json(await enfileirar(`nomes ${L}`, (PLC, job) => gravarNomes(PLC, job, { L, alvo, anterior, usuario, ip })));
    } catch (e) {
      const c = e && e.corpo;
      if (c && (c.erro === 'clp_offline' || (c.erro === 'timeout' && c.etapa === 'fila')))
        registrarLog({ ip, usuario, linha: L, rota: 'nomes', resultado: c.erro === 'clp_offline' ? 'clp_offline' : 'timeout_nao_executado' });
      responderErro(res, e);
    }
  });

  router.post('/api/receitas/:linha', registrarChegada, ...protegido, async (req, res) => {
    const L = req.params.linha;
    if (!validarLinha(L)) return res.status(400).json({ ok: false, erro: 'linha_invalida', mensagem: 'Linha deve ser 501 ou 502.' });
    const b = req.body || {};
    const usuario = typeof b.usuario === 'string' ? b.usuario.trim() : '';
    if (!usuario || usuario.length > 60) return res.status(400).json({ ok: false, erro: 'usuario_obrigatorio', mensagem: 'Informe o operador (até 60 caracteres).' });
    const alvo = validarPct(b.pct);
    if (!alvo.ok) return res.status(400).json({ ok: false, ...alvo, mensagem: alvo.detalhe });
    const ant = b.anterior;
    if (!ant || !Array.isArray(ant.pct) || ant.pct.length !== N_COMP || ant.pct.some(x => paraCentesimos(x) === null) || !Number.isFinite(Number(ant.n)))
      return res.status(400).json({ ok: false, erro: 'anterior_obrigatorio', mensagem: 'Envie a receita anterior (pct[9] e n) que a tela mostrava.' });
    // Umidade é opcional (compatibilidade): se vier, precisa vir a anterior também
    let umAlvo = null;
    if (b.umidade !== undefined) {
      const vu = validarUmidade(b.umidade);
      if (!vu.ok) return res.status(400).json({ ok: false, ...vu, mensagem: vu.detalhe });
      if (!Array.isArray(ant.umidade) || ant.umidade.length !== N_COMP || ant.umidade.some(x => paraCentesimos(x) === null))
        return res.status(400).json({ ok: false, erro: 'anterior_obrigatorio', mensagem: 'Envie a umidade anterior que a tela mostrava.' });
      umAlvo = vu.centesimos;
    }
    const antC = ant.pct.map(paraCentesimos);
    const umIgual = !umAlvo || ant.umidade.map(paraCentesimos).every((c, i) => c === umAlvo[i]);
    if (antC.every((c, i) => c === alvo.centesimos[i]) && Math.round(Number(ant.n)) === alvo.n && umIgual)
      return res.status(400).json({ ok: false, erro: 'sem_alteracao', mensagem: 'Nada mudou em relação à receita atual.' });

    const ip = ipDe(req);
    try {
      const r = await enfileirar(`gravar ${L}`, (PLC, job) => gravar(PLC, job, { L, alvo, umAlvo, anterior: ant, forcar: b.forcar === true, usuario, ip }));
      res.json(r);
    } catch (e) {
      // Os demais desfechos são registrados dentro de gravar(); aqui só o que não chegou a executar
      const c = e && e.corpo;
      if (c && (c.erro === 'clp_offline' || (c.erro === 'timeout' && c.etapa === 'fila')))
        registrarLog({ ip, usuario, linha: L, depois: { pct: alvo.centesimos.map(x => x / 100), n: alvo.n }, resultado: c.erro === 'clp_offline' ? 'clp_offline' : 'timeout_nao_executado' });
      responderErro(res, e);
    }
  });

  return {
    router, processarFila, exclusivo,
    // para testes / diagnóstico
    _estado: () => ({ fila: fila.length, ultimoDreno, online: clpOnline() }),
    _aguardarArquivos: () => Promise.all([gravandoLog, gravandoPresets]),
  };
}

module.exports = criarReceitas;
module.exports.validarPct = validarPct;
module.exports.paraCentesimos = paraCentesimos;
module.exports.ipConfiavel = ipConfiavel;
module.exports.pinConfere = pinConfere;
module.exports.tagsDaLinha = tagsDaLinha;
module.exports.validarUmidade = validarUmidade;
module.exports.normalizarNome = normalizarNome;
