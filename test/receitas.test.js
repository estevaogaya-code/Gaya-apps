// Testes de receitas.js com PLC falso.  Rodar:  node --test test/
'use strict';
const test    = require('node:test');
const assert  = require('node:assert/strict');
const fs      = require('fs');
const os      = require('os');
const path    = require('path');
const express = require('express');
const criarReceitas = require('../receitas');
const { validarPct, ipConfiavel, pinConfere, validarUmidade, normalizarNome } = criarReceitas;

const sleep = ms => new Promise(r => setTimeout(r, ms));
const PIN = '4321';

// ─── PLC falso ────────────────────────────────────────────────────────────────
class FakeTag {
  constructor(name) { this.name = name; this.value = null; this.controller_value = null; }
}
class FakeGroup {
  constructor() { this.tags = []; }
  add(t) { if (!this.tags.includes(t)) this.tags.push(t); }
}
// STRING Logix pelos membros: .LEN (DINT) e .DATA[0..81] (SINT)
function setNome(mem, L, i, txt, len) {
  const b = `NOME_PRODUTO0${i}_${L}`;
  for (let k = 0; k < 82; k++) mem[`${b}.DATA[${k}]`] = k < txt.length ? txt.charCodeAt(k) : 0;
  mem[`${b}.LEN`] = len != null ? len : txt.length;
}
function lerNomeMem(mem, L, i) {
  const b = `NOME_PRODUTO0${i}_${L}`, n = mem[`${b}.LEN`];
  let t = ''; for (let k = 0; k < n; k++) t += String.fromCharCode(mem[`${b}.DATA[${k}]`]);
  return t.replace(/\x00/g, '');
}
class FakePLC {
  constructor() {
    this.mem = {};
    for (const L of ['501', '502']) {
      const pct = [1, 0.75, 0.25, 0, 0, 0, 0, 0, 0, 0, 12000];
      pct.forEach((v, i) => { this.mem[`PERCENTUAL_RECEITA_${L}[${i}]`] = Math.fround(v); });
      this.mem[`VA_RECEITAS_${L}WF005[2]`] = 2;
      this.mem[`VD_MR_RECEITAS_${L}[0]`] = 0;
      this.mem[`COUNTER_MR_RECEITAS_${L}[0].ACC`] = 0;
      for (let i = 1; i <= 9; i++) this.mem[`VA_${L}_UMIDADE[${i}]`] = Math.fround(i === 1 ? 9 : 0);
      for (let i = 1; i <= 9; i++) setNome(this.mem, L, i, '');
    }
    setNome(this.mem, '501', 1, 'BRASKEM'); setNome(this.mem, '501', 2, 'PAMPA');
    setNome(this.mem, '501', 3, '', 11); // como no L5K: LEN 11 com DATA zerado
    this.escritas = []; this.ignorar = new Set(); this.falharEscrita = null; this.atrasoLeitura = 0;
    this.ativas = 0; this.maxAtivas = 0;
  }
  async readTag(t) {
    this.ativas++; this.maxAtivas = Math.max(this.maxAtivas, this.ativas);
    try {
      if (this.atrasoLeitura) await sleep(this.atrasoLeitura);
      if (!(t.name in this.mem)) throw new Error('tag inexistente ' + t.name);
      t.value = this.mem[t.name]; t.controller_value = t.value;
    } finally { this.ativas--; }
  }
  async writeTagGroup(g) {
    this.escritas.push(g.tags.map(t => [t.name, t.value]));
    if (this.falharEscrita) throw this.falharEscrita;
    for (const t of g.tags) if (!this.ignorar.has(t.name)) this.mem[t.name] = /\.(LEN|DATA)/.test(t.name) ? t.value : Math.fround(t.value);
    for (const L of ['501', '502']) { // CPT do CLP: [0] = soma de [1..9] em REAL
      let s = Math.fround(0);
      for (let i = 1; i <= 9; i++) s = Math.fround(s + this.mem[`PERCENTUAL_RECEITA_${L}[${i}]`]);
      this.mem[`PERCENTUAL_RECEITA_${L}[0]`] = s;
    }
  }
}

// ─── Montagem ─────────────────────────────────────────────────────────────────
async function montar(o = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'receitas-'));
  const plc = new FakePLC();
  const env = 'env' in o ? o.env : { RECEITAS_PIN: PIN };
  const cache = { nomes: { 501: ['BRASKEM', 'PAMPA', '', '', '', '', '', '', ''], 502: Array(9).fill('') } };
  const r = criarReceitas({
    Tag: FakeTag, TagGroup: FakeGroup, dir, env,
    getNomes: () => cache.nomes,
    onNomes: (L, nomes) => { cache.nomes = { ...cache.nomes, [L]: nomes }; },
    timeoutMs: 400, pausaReleituraMs: 5, leituraMs: 1000, esperaLockMs: 100, bloqueioMs: 300,
    ...o.opts,
  });
  const app = express();
  app.use(express.json());
  if (o.ipRemoto) app.use((req, res, next) => { Object.defineProperty(req.socket, 'remoteAddress', { value: o.ipRemoto, configurable: true }); next(); });
  app.use(r.router);
  const srv = await new Promise(res => { const s = app.listen(0, '127.0.0.1', () => res(s)); });
  const base = `http://127.0.0.1:${srv.address().port}`;

  // "ciclo" do server.js: drena a fila a cada 15 ms, encadeado (sem sobreposição)
  let rodando = o.semCiclo ? false : true, parado = false;
  (async function ciclo() { while (!parado) { if (rodando) await r.processarFila(plc); await sleep(15); } })();

  async function req(metodo, url, corpo, pin = PIN) {
    const h = { 'content-type': 'application/json' };
    if (pin != null) h['x-receitas-pin'] = pin;
    const resp = await fetch(base + url, { method: metodo, headers: h, body: corpo ? JSON.stringify(corpo) : undefined });
    return { status: resp.status, corpo: await resp.json() };
  }
  const fechar = async () => { parado = true; await r._aguardarArquivos(); await new Promise(res => srv.close(res)); fs.rmSync(dir, { recursive: true, force: true }); };
  const log = () => { const f = path.join(dir, 'receitas_log.jsonl'); return fs.existsSync(f) ? fs.readFileSync(f, 'utf8').split('\n').filter(Boolean).map(JSON.parse) : []; };
  return { r, plc, req, fechar, log, dir, cache, setCiclo: v => { rodando = v; } };
}

const ANT = { pct: [75, 25, 0, 0, 0, 0, 0, 0, 0], n: 2 };
const corpo = (pct, extra = {}) => ({ usuario: 'Teste', pct, anterior: ANT, ...extra });

// ─── Funções puras ────────────────────────────────────────────────────────────
test('validarPct — matriz', () => {
  assert.deepEqual(validarPct([75, 25, 0, 0, 0, 0, 0, 0, 0]).n, 2);
  assert.equal(validarPct([33.33, 33.33, 33.34, 0, 0, 0, 0, 0, 0]).n, 3);
  assert.equal(validarPct([100, 0, 0, 0, 0, 0, 0, 0, 0]).n, 1);
  assert.equal(validarPct([11.11, 11.11, 11.11, 11.11, 11.11, 11.11, 11.11, 11.11, 11.12]).n, 9);
  assert.equal(validarPct([0.1, 99.9, 0, 0, 0, 0, 0, 0, 0]).n, 2);
  assert.equal(validarPct([75, 24.99, 0, 0, 0, 0, 0, 0, 0]).erro, 'soma_diferente_de_100');
  assert.equal(validarPct([75, 25.01, 0, 0, 0, 0, 0, 0, 0]).erro, 'soma_diferente_de_100');
  assert.equal(validarPct([50, 0, 50, 0, 0, 0, 0, 0, 0]).erro, 'componente_vazio_no_meio');
  assert.equal(validarPct([0, 100, 0, 0, 0, 0, 0, 0, 0]).erro, 'componente_vazio_no_meio');
  assert.equal(validarPct([110, -10, 0, 0, 0, 0, 0, 0, 0]).erro, 'pct_fora_da_faixa');
  assert.equal(validarPct([75.001, 24.999, 0, 0, 0, 0, 0, 0, 0]).erro, 'pct_invalido');
  assert.equal(validarPct(['75', 25, 0, 0, 0, 0, 0, 0, 0]).erro, 'pct_invalido');
  assert.equal(validarPct([NaN, 25, 0, 0, 0, 0, 0, 0, 0]).erro, 'pct_invalido');
  assert.equal(validarPct([75, 25]).erro, 'pct_invalido');
  assert.equal(validarPct(null).erro, 'pct_invalido');
  assert.equal(validarPct([0, 0, 0, 0, 0, 0, 0, 0, 0]).erro, 'soma_diferente_de_100');
});

test('ipConfiavel — matriz', () => {
  for (const ip of ['127.0.0.1', '::1', '::ffff:127.0.0.1', '10.0.0.222', '172.16.0.1', '172.31.255.255', '192.168.1.5', '100.64.0.1', '100.127.255.254', 'fd7a:115c:a1e0::1'])
    assert.equal(ipConfiavel(ip), true, ip);
  for (const ip of ['8.8.8.8', '172.32.0.1', '172.15.255.255', '100.128.0.1', '100.63.255.255', '192.169.0.1', '11.0.0.1', '2001:db8::1', '', undefined, '10.0.0', '10.0.0.256'])
    assert.equal(ipConfiavel(ip), false, String(ip));
});

test('pinConfere', () => {
  assert.equal(pinConfere('4321', '4321'), true);
  assert.equal(pinConfere('4322', '4321'), false);
  assert.equal(pinConfere('', '4321'), false);
  assert.equal(pinConfere(undefined, '4321'), false);
  assert.equal(pinConfere('4321', ''), false);
});

// ─── Leitura ──────────────────────────────────────────────────────────────────
test('GET sem ciclo (CLP offline) → 504', async () => {
  const t = await montar({ semCiclo: true });
  try {
    const r = await t.req('GET', '/api/receitas');
    assert.equal(r.status, 504); assert.equal(r.corpo.erro, 'clp_offline');
  } finally { await t.fechar(); }
});

test('GET lê as duas linhas', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('GET', '/api/receitas');
    assert.equal(r.status, 200);
    const l = r.corpo.linhas['501'];
    assert.deepEqual(l.pct, [75, 25, 0, 0, 0, 0, 0, 0, 0]);
    assert.equal(l.n, 2); assert.equal(l.soma, 1); assert.equal(l.somaPct, 100); assert.equal(l.base, 12000);
    assert.equal(l.pesando, false); assert.equal(l.nomes[0], 'BRASKEM');
    assert.ok(r.corpo.linhas['502']); assert.equal(r.corpo.escritaHabilitada, true);
    assert.ok(r.corpo.presets && typeof r.corpo.ts === 'number');
  } finally { await t.fechar(); }
});

// ─── Escrita ──────────────────────────────────────────────────────────────────
test('POST grava num único writeTagGroup, relê e registra', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('POST', '/api/receitas/501', corpo([60, 30, 10, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.deepEqual(r.corpo.lido.pct, [60, 30, 10, 0, 0, 0, 0, 0, 0]);
    assert.equal(r.corpo.lido.n, 3);
    assert.equal(t.plc.escritas.length, 1);
    const nomes = t.plc.escritas[0].map(e => e[0]);
    assert.ok(nomes.includes('PERCENTUAL_RECEITA_501[1]') && nomes.includes('VA_RECEITAS_501WF005[2]'));
    assert.ok(!nomes.includes('PERCENTUAL_RECEITA_501[0]') && !nomes.includes('PERCENTUAL_RECEITA_501[10]'));
    assert.equal(t.plc.mem['PERCENTUAL_RECEITA_501[1]'], Math.fround(0.6));
    assert.equal(t.plc.mem['VA_RECEITAS_501WF005[2]'], 3);
    assert.equal(t.plc.mem['PERCENTUAL_RECEITA_502[1]'], Math.fround(0.75), '502 intocada');
    assert.ok(t.plc.maxAtivas <= 1, 'sem leituras concorrentes');
    await t.r._aguardarArquivos();
    const ult = t.log().pop();
    assert.equal(ult.resultado, 'ok'); assert.equal(ult.usuario, 'Teste'); assert.equal(ult.linha, '501');
    assert.deepEqual(ult.antes.pct, [75, 25, 0, 0, 0, 0, 0, 0, 0]); assert.equal(ult.leitura.n, 3);
    const lg = await t.req('GET', '/api/receitas/log?limit=5');
    assert.equal(lg.corpo[0].resultado, 'ok');
  } finally { await t.fechar(); }
});

test('POST com 3 casas de 33,33 — soma do CLP ≈ 1', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('POST', '/api/receitas/502', corpo([33.33, 33.33, 33.34, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.deepEqual(r.corpo.lido.pct, [33.33, 33.33, 33.34, 0, 0, 0, 0, 0, 0]);
  } finally { await t.fechar(); }
});

test('POST — erros de validação (400) não tocam o CLP', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const casos = [
      ['/api/receitas/503', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]), 'linha_invalida'],
      ['/api/receitas/501', corpo([60, 39, 0, 0, 0, 0, 0, 0, 0]), 'soma_diferente_de_100'],
      ['/api/receitas/501', corpo([60, 0, 40, 0, 0, 0, 0, 0, 0]), 'componente_vazio_no_meio'],
      ['/api/receitas/501', corpo([75, 25, 0, 0, 0, 0, 0, 0, 0]), 'sem_alteracao'],
      ['/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0], { usuario: '  ' }), 'usuario_obrigatorio'],
      ['/api/receitas/501', { usuario: 'x', pct: [60, 40, 0, 0, 0, 0, 0, 0, 0] }, 'anterior_obrigatorio'],
    ];
    for (const [url, c, erro] of casos) {
      const r = await t.req('POST', url, c);
      assert.equal(r.status, 400, erro); assert.equal(r.corpo.erro, erro);
    }
    assert.equal(t.plc.escritas.length, 0);
  } finally { await t.fechar(); }
});

test('409 otimista — CLP mudou desde a tela', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.mem['PERCENTUAL_RECEITA_501[1]'] = Math.fround(0.7);
    t.plc.mem['PERCENTUAL_RECEITA_501[2]'] = Math.fround(0.3);
    let r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'conflito');
    assert.deepEqual(r.corpo.atual.pct.slice(0, 2), [70, 30]);
    // n diferente também é conflito
    t.plc.mem['PERCENTUAL_RECEITA_501[1]'] = Math.fround(0.75);
    t.plc.mem['PERCENTUAL_RECEITA_501[2]'] = Math.fround(0.25);
    t.plc.mem['VA_RECEITAS_501WF005[2]'] = 3;
    r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'conflito');
    assert.equal(t.plc.escritas.length, 0);
  } finally { await t.fechar(); }
});

test('pesagem em andamento: 409, forçar, e recusa quando o passo passou do novo n', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.mem['VD_MR_RECEITAS_501[0]'] = 0x04; // .2 WF005 pesando
    let r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'pesagem_em_andamento');
    t.plc.mem['VD_MR_RECEITAS_501[0]'] = 0x20; // .5 WF010 pesando
    r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'pesagem_em_andamento');
    // forçar com mesmo n → grava
    r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0], { forcar: true }));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    const ant2 = { pct: [60, 40, 0, 0, 0, 0, 0, 0, 0], n: 2 };
    // forçar reduzindo n com o lote já no componente 2 → recusa
    t.plc.mem['COUNTER_MR_RECEITAS_501[0].ACC'] = 2;
    r = await t.req('POST', '/api/receitas/501', { usuario: 'T', pct: [100, 0, 0, 0, 0, 0, 0, 0, 0], anterior: ant2, forcar: true });
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'forcar_recusado_passo'); assert.equal(r.corpo.passo, 2);
    // aumentando n com o lote no componente 2 → permitido
    r = await t.req('POST', '/api/receitas/501', { usuario: 'T', pct: [50, 30, 20, 0, 0, 0, 0, 0, 0], anterior: ant2, forcar: true });
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.equal(t.plc.escritas.length, 2);
  } finally { await t.fechar(); }
});

test('divergência na releitura → 502 com estado real lido, sem rollback', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.ignorar.add('PERCENTUAL_RECEITA_501[2]'); // CLP "não aceita" o componente 2
    const r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 502); assert.equal(r.corpo.erro, 'divergencia');
    assert.deepEqual(r.corpo.lido.pct.slice(0, 2), [60, 25]);
    assert.ok(r.corpo.divergencias.some(d => d.componente === 2));
    assert.ok(r.corpo.divergencias.some(d => d.campo === 'soma_clp'));
    assert.equal(t.plc.escritas.length, 1, 'sem rollback automático');
    await t.r._aguardarArquivos();
    assert.equal(t.log().pop().resultado, 'divergencia');
  } finally { await t.fechar(); }
});

test('falha na escrita → 502 falha_escrita com leitura real', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.falharEscrita = { generalStatusCode: 30, extendedStatus: [] };
    const r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 502); assert.equal(r.corpo.erro, 'falha_escrita');
    assert.match(r.corpo.erroEscrita, /CIP status 30/);
    assert.deepEqual(r.corpo.lido.pct.slice(0, 2), [75, 25]);
  } finally { await t.fechar(); }
});

test('timeout na fila: 504 e a operação NÃO roda depois', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.setCiclo(false); // ciclo "travado" (CLP lento), mas ainda dentro da janela online
    const r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 504); assert.equal(r.corpo.erro, 'timeout'); assert.equal(r.corpo.executado, false);
    t.setCiclo(true);
    await sleep(80);
    assert.equal(t.plc.escritas.length, 0);
    assert.equal(t.plc.mem['PERCENTUAL_RECEITA_501[1]'], Math.fround(0.75));
    await t.r._aguardarArquivos();
    assert.equal(t.log().pop().resultado, 'timeout_nao_executado');
  } finally { await t.fechar(); }
});

test('timeout durante a leitura: a escrita não é enviada', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.atrasoLeitura = 40; // 13 leituras × 40 ms > 400 ms
    const r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 504); assert.equal(r.corpo.executado, false);
    await sleep(500);
    assert.equal(t.plc.escritas.length, 0);
  } finally { await t.fechar(); }
});

test('offline (sem drenagem recente) → 504 imediato', async () => {
  const t = await montar({ opts: { offlineMs: 50 } });
  try {
    await sleep(30);
    t.setCiclo(false);
    await sleep(100);
    const r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 504); assert.equal(r.corpo.erro, 'clp_offline');
  } finally { await t.fechar(); }
});

// ─── Segurança ────────────────────────────────────────────────────────────────
test('sem RECEITAS_PIN → escrita desabilitada (503); leitura segue', async () => {
  const t = await montar({ env: {} });
  try {
    await sleep(30);
    let r = await t.req('POST', '/api/receitas/auth', {});
    assert.equal(r.status, 503); assert.equal(r.corpo.erro, 'escrita_desabilitada');
    r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 503);
    r = await t.req('GET', '/api/receitas');
    assert.equal(r.status, 200); assert.equal(r.corpo.escritaHabilitada, false);
  } finally { await t.fechar(); }
});

test('bloqueio após 5 PINs errados (429), libera após o prazo', async () => {
  const t = await montar();
  try {
    let r = await t.req('POST', '/api/receitas/auth', {});
    assert.equal(r.status, 200);
    for (let i = 1; i <= 4; i++) {
      r = await t.req('POST', '/api/receitas/auth', {}, '0000');
      assert.equal(r.status, 401); assert.equal(r.corpo.restantes, 5 - i);
    }
    r = await t.req('POST', '/api/receitas/auth', {}, '0000');
    assert.equal(r.status, 429);
    r = await t.req('POST', '/api/receitas/auth', {}); // PIN certo, mas bloqueado
    assert.equal(r.status, 429);
    r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 429);
    await sleep(350); // bloqueioMs = 300 no teste
    r = await t.req('POST', '/api/receitas/auth', {});
    assert.equal(r.status, 200);
    r = await t.req('POST', '/api/receitas/auth', {}, null); // sem cabeçalho
    assert.equal(r.status, 401);
  } finally { await t.fechar(); }
});

test('rede não confiável → 403 mesmo com PIN certo; desligável por variável', async () => {
  let t = await montar({ ipRemoto: '8.8.8.8' });
  try {
    await sleep(30);
    let r = await t.req('POST', '/api/receitas/auth', {});
    assert.equal(r.status, 403); assert.equal(r.corpo.erro, 'rede_nao_confiavel');
    r = await t.req('POST', '/api/receitas/501', corpo([60, 40, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 403);
    r = await t.req('POST', '/api/receitas/presets/501', { nome: 'x', usuario: 'u', pct: [100, 0, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.status, 403);
    r = await t.req('GET', '/api/receitas');
    assert.equal(r.status, 200, 'leitura não é restringida');
  } finally { await t.fechar(); }
  t = await montar({ ipRemoto: '8.8.8.8', env: { RECEITAS_PIN: PIN, RECEITAS_RESTRINGIR_REDE: '0' } });
  try {
    const r = await t.req('POST', '/api/receitas/auth', {});
    assert.equal(r.status, 200);
  } finally { await t.fechar(); }
  t = await montar({ ipRemoto: '100.101.102.103' }); // Tailscale
  try {
    const r = await t.req('POST', '/api/receitas/auth', {});
    assert.equal(r.status, 200);
  } finally { await t.fechar(); }
});

// ─── Presets ──────────────────────────────────────────────────────────────────
test('presets: salvar, listar, sobrescrever, excluir, validar; não tocam o CLP', async () => {
  const t = await montar();
  try {
    await sleep(30);
    let r = await t.req('POST', '/api/receitas/presets/501', { nome: 'CP-II', usuario: 'Ana', pct: [70, 30, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.status, 200); assert.equal(r.corpo.presets['501'][0].n, 2);
    r = await t.req('POST', '/api/receitas/presets/501', { nome: 'CP-II', usuario: 'Ana', pct: [65, 35, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.corpo.presets['501'].length, 1); assert.deepEqual(r.corpo.presets['501'][0].pct.slice(0, 2), [65, 35]);
    r = await t.req('POST', '/api/receitas/presets/501', { nome: 'ruim', usuario: 'Ana', pct: [70, 20, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.status, 400);
    r = await t.req('POST', '/api/receitas/presets/501', { nome: 'x', usuario: 'Ana', pct: [100, 0, 0, 0, 0, 0, 0, 0, 0] }, '0000');
    assert.equal(r.status, 401);
    r = await t.req('GET', '/api/receitas');
    assert.equal(r.corpo.presets['501'][0].nome, 'CP-II');
    const salvo = JSON.parse(fs.readFileSync(path.join(t.dir, 'receitas_presets.json'), 'utf8'));
    assert.equal(salvo['501'][0].nome, 'CP-II');
    r = await t.req('DELETE', '/api/receitas/presets/501/' + encodeURIComponent('CP-II') + '?usuario=Ana');
    assert.equal(r.status, 200); assert.equal(r.corpo.presets['501'].length, 0);
    r = await t.req('DELETE', '/api/receitas/presets/501/inexistente');
    assert.equal(r.status, 404);
    assert.equal(t.plc.escritas.length, 0);
  } finally { await t.fechar(); }
});

// ─── Exclusão mútua com a leitura de nomes ────────────────────────────────────
test('drenagem espera a leitura de nomes e desiste do ciclo se ela demorar', async () => {
  const t = await montar({ semCiclo: true });
  try {
    await t.r.processarFila(t.plc); // marca online
    let liberar; const nomes = t.r.exclusivo(() => new Promise(res => { liberar = res; }));
    const pend = t.req('GET', '/api/receitas');
    await sleep(20);
    const t0 = Date.now();
    await t.r.processarFila(t.plc); // lock ocupado > esperaLockMs (100) → não drena
    assert.ok(Date.now() - t0 >= 90);
    assert.equal(t.r._estado().fila, 1);
    liberar(); await nomes;
    await t.r.processarFila(t.plc);
    const r = await pend;
    assert.equal(r.status, 200);
  } finally { await t.fechar(); }
});

// ─── Umidade ──────────────────────────────────────────────────────────────────
const UM = [9, 0, 0, 0, 0, 0, 0, 0, 0];
const ANT_U = { ...ANT, umidade: UM };
const corpoU = (pct, umidade, extra = {}) => ({ usuario: 'Teste', pct, umidade, anterior: ANT_U, ...extra });

test('validarUmidade — matriz', () => {
  assert.equal(validarUmidade([9, 0, 0, 0, 0, 0, 0, 0, 0]).ok, true);
  assert.equal(validarUmidade([30, 0, 0, 0, 0, 0, 0, 0, 0]).ok, true);
  assert.equal(validarUmidade([30.01, 0, 0, 0, 0, 0, 0, 0, 0]).erro, 'umidade_fora_da_faixa');
  assert.equal(validarUmidade([-1, 0, 0, 0, 0, 0, 0, 0, 0]).erro, 'umidade_fora_da_faixa');
  assert.equal(validarUmidade([9.123, 0, 0, 0, 0, 0, 0, 0, 0]).erro, 'umidade_invalida');
  assert.equal(validarUmidade([9]).erro, 'umidade_invalida');
});

test('GET devolve a umidade', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('GET', '/api/receitas');
    assert.deepEqual(r.corpo.linhas['501'].umidade, UM);
  } finally { await t.fechar(); }
});

test('POST só de umidade grava no mesmo writeTagGroup e confere', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('POST', '/api/receitas/501', corpoU([75, 25, 0, 0, 0, 0, 0, 0, 0], [8.5, 1.25, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.deepEqual(r.corpo.lido.umidade.slice(0, 2), [8.5, 1.25]);
    assert.equal(t.plc.escritas.length, 1);
    assert.ok(t.plc.escritas[0].some(e => e[0] === 'VA_501_UMIDADE[1]'));
    assert.equal(t.plc.mem['VA_501_UMIDADE[2]'], Math.fround(1.25));
    assert.equal(t.plc.mem['VA_502_UMIDADE[1]'], Math.fround(9), '502 intocada');
    await t.r._aguardarArquivos();
    const ult = t.log().pop();
    assert.equal(ult.resultado, 'ok'); assert.deepEqual(ult.antes.umidade, UM); assert.deepEqual(ult.depois.umidade.slice(0, 2), [8.5, 1.25]);
  } finally { await t.fechar(); }
});

test('POST pct + umidade juntos', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('POST', '/api/receitas/502', corpoU([60, 40, 0, 0, 0, 0, 0, 0, 0], [7, 2, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.deepEqual(r.corpo.lido.pct.slice(0, 2), [60, 40]);
    assert.deepEqual(r.corpo.lido.umidade.slice(0, 2), [7, 2]);
    assert.equal(t.plc.escritas.length, 1);
  } finally { await t.fechar(); }
});

test('umidade — 400 de validação e sem_alteracao', async () => {
  const t = await montar();
  try {
    await sleep(30);
    let r = await t.req('POST', '/api/receitas/501', corpoU([75, 25, 0, 0, 0, 0, 0, 0, 0], [31, 0, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 400); assert.equal(r.corpo.erro, 'umidade_fora_da_faixa');
    r = await t.req('POST', '/api/receitas/501', corpoU([75, 25, 0, 0, 0, 0, 0, 0, 0], UM));
    assert.equal(r.status, 400); assert.equal(r.corpo.erro, 'sem_alteracao');
    r = await t.req('POST', '/api/receitas/501', { usuario: 'T', pct: [75, 25, 0, 0, 0, 0, 0, 0, 0], umidade: [8, 0, 0, 0, 0, 0, 0, 0, 0], anterior: ANT });
    assert.equal(r.status, 400); assert.equal(r.corpo.erro, 'anterior_obrigatorio');
    assert.equal(t.plc.escritas.length, 0);
  } finally { await t.fechar(); }
});

test('umidade — 409 se mudou no CLP, 409 com pesagem, 502 se o CLP não aceitar', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.mem['VA_501_UMIDADE[1]'] = Math.fround(10);
    let r = await t.req('POST', '/api/receitas/501', corpoU([75, 25, 0, 0, 0, 0, 0, 0, 0], [8, 0, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'conflito'); assert.equal(r.corpo.atual.umidade[0], 10);
    t.plc.mem['VA_501_UMIDADE[1]'] = Math.fround(9);
    t.plc.mem['VD_MR_RECEITAS_501[0]'] = 0x04;
    r = await t.req('POST', '/api/receitas/501', corpoU([75, 25, 0, 0, 0, 0, 0, 0, 0], [8, 0, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'pesagem_em_andamento');
    t.plc.mem['VD_MR_RECEITAS_501[0]'] = 0;
    t.plc.ignorar.add('VA_501_UMIDADE[1]');
    r = await t.req('POST', '/api/receitas/501', corpoU([75, 25, 0, 0, 0, 0, 0, 0, 0], [8, 0, 0, 0, 0, 0, 0, 0, 0]));
    assert.equal(r.status, 502); assert.equal(r.corpo.erro, 'divergencia');
    assert.ok(r.corpo.divergencias.some(d => d.campo === 'umidade' && d.componente === 1));
  } finally { await t.fechar(); }
});

test('presets guardam a umidade (opcional)', async () => {
  const t = await montar();
  try {
    let r = await t.req('POST', '/api/receitas/presets/501', { nome: 'A', usuario: 'u', pct: [70, 30, 0, 0, 0, 0, 0, 0, 0], umidade: [8, 1, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.status, 200); assert.deepEqual(r.corpo.presets['501'][0].umidade.slice(0, 2), [8, 1]);
    r = await t.req('POST', '/api/receitas/presets/501', { nome: 'B', usuario: 'u', pct: [70, 30, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.status, 200); assert.equal(r.corpo.presets['501'][1].umidade, undefined);
    r = await t.req('POST', '/api/receitas/presets/501', { nome: 'C', usuario: 'u', pct: [70, 30, 0, 0, 0, 0, 0, 0, 0], umidade: [40, 0, 0, 0, 0, 0, 0, 0, 0] });
    assert.equal(r.status, 400);
  } finally { await t.fechar(); }
});

// ─── Nomes ────────────────────────────────────────────────────────────────────
const NOMES = ['BRASKEM', 'PAMPA', '', '', '', '', '', '', ''];
const corpoN = (nomes, extra = {}) => ({ usuario: 'Teste', nomes, anterior: NOMES, ...extra });

test('normalizarNome — matriz', () => {
  assert.equal(normalizarNome(' calcario '), 'CALCARIO');
  assert.equal(normalizarNome('ABCDEFGHIJKL'), 'ABCDEFGHIJKL');
  assert.equal(normalizarNome('ABCDEFGHIJKLM'), null);
  assert.equal(normalizarNome('GESSO-2 (B)'), 'GESSO-2 (B)');
  assert.equal(normalizarNome('ÂMBAR'), null);
  assert.equal(normalizarNome(''), '');
  assert.equal(normalizarNome(5), null);
});

test('nomes: grava só os alterados, zera o resto do DATA, atualiza o cache', async () => {
  const t = await montar();
  try {
    await sleep(30);
    const r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['BRK', 'PAMPA', 'calcario', '', '', '', '', '', '']));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.deepEqual(r.corpo.nomes.slice(0, 3), ['BRK', 'PAMPA', 'CALCARIO']);
    assert.equal(lerNomeMem(t.plc.mem, '501', 1), 'BRK');
    assert.equal(t.plc.mem['NOME_PRODUTO01_501.LEN'], 3);
    for (let k = 3; k < 7; k++) assert.equal(t.plc.mem[`NOME_PRODUTO01_501.DATA[${k}]`], 0, `DATA[${k}] zerado`);
    assert.equal(t.plc.mem['NOME_PRODUTO03_501.LEN'], 8);
    assert.equal(lerNomeMem(t.plc.mem, '501', 3), 'CALCARIO');
    assert.equal(t.plc.escritas.length, 1);
    assert.ok(!t.plc.escritas[0].some(e => e[0].startsWith('NOME_PRODUTO02_501')), 'PAMPA não regravado');
    assert.ok(!t.plc.escritas[0].some(e => e[0].startsWith('PERCENTUAL')), 'receita intocada');
    assert.deepEqual(t.cache.nomes['501'].slice(0, 3), ['BRK', 'PAMPA', 'CALCARIO']);
    const g = await t.req('GET', '/api/receitas');
    assert.equal(g.corpo.linhas['501'].nomes[2], 'CALCARIO');
    await t.r._aguardarArquivos();
    const ult = t.log().pop();
    assert.equal(ult.rota, 'nomes'); assert.equal(ult.resultado, 'ok'); assert.deepEqual(ult.depois, { 1: 'BRK', 3: 'CALCARIO' });
  } finally { await t.fechar(); }
});

test('nomes: validação (400), conflito (409), divergência (502), liberado durante pesagem', async () => {
  const t = await montar();
  try {
    await sleep(30);
    let r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['BRASKEM', 'PAMPA', 'NOME MUITO LONGO', '', '', '', '', '', '']));
    assert.equal(r.status, 400); assert.equal(r.corpo.erro, 'nome_invalido'); assert.equal(r.corpo.componente, 3);
    r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['BRASKEM', 'PAMPA', 'CALCÁRIO', '', '', '', '', '', '']));
    assert.equal(r.status, 400);
    r = await t.req('POST', '/api/receitas/501/nomes', corpoN(NOMES));
    assert.equal(r.status, 400); assert.equal(r.corpo.erro, 'sem_alteracao');
    r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['X', 'PAMPA', '', '', '', '', '', '', '']), '0000');
    assert.equal(r.status, 401);
    assert.equal(t.plc.escritas.length, 0);

    setNome(t.plc.mem, '501', 2, 'AMBAR'); // alguém trocou pelo supervisório
    r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['BRASKEM', 'PAMPA2', '', '', '', '', '', '', '']));
    assert.equal(r.status, 409); assert.equal(r.corpo.erro, 'conflito'); assert.equal(r.corpo.nomes[1], 'AMBAR');
    assert.equal(t.plc.escritas.length, 0);

    t.plc.mem['VD_MR_RECEITAS_501[0]'] = 0x04; // pesando: nome não afeta processo
    r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['CLINQUER', 'PAMPA', '', '', '', '', '', '', '']));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));

    t.plc.ignorar.add('NOME_PRODUTO01_501.DATA[0]');
    r = await t.req('POST', '/api/receitas/501/nomes', { usuario: 'T', nomes: ['ZZ', 'PAMPA', '', '', '', '', '', '', ''], anterior: ['CLINQUER', 'PAMPA', '', '', '', '', '', '', ''] });
    assert.equal(r.status, 502); assert.equal(r.corpo.erro, 'divergencia');
    assert.equal(r.corpo.divergencias[0].componente, 1);
  } finally { await t.fechar(); }
});

test('nomes: nome existente em minúsculas não é regravado se não foi editado', async () => {
  const t = await montar();
  try {
    await sleep(30);
    setNome(t.plc.mem, '501', 2, 'pampa');
    const ant = ['BRASKEM', 'pampa', '', '', '', '', '', '', ''];
    const r = await t.req('POST', '/api/receitas/501/nomes', { usuario: 'T', nomes: ['BRASKEM', 'pampa', 'GESSO', '', '', '', '', '', ''], anterior: ant });
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.equal(lerNomeMem(t.plc.mem, '501', 2), 'pampa');
    assert.ok(!t.plc.escritas[0].some(e => e[0].startsWith('NOME_PRODUTO02_501')));
  } finally { await t.fechar(); }
});

test('nomes: zera lixo no DATA além do LEN antigo', async () => {
  const t = await montar();
  try {
    await sleep(30);
    t.plc.mem['NOME_PRODUTO02_501.DATA[50]'] = 88; // lixo que o server.js mostraria
    const r = await t.req('POST', '/api/receitas/501/nomes', corpoN(['BRASKEM', 'AMB', '', '', '', '', '', '', '']));
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    for (let k = 3; k < 82; k++) assert.equal(t.plc.mem[`NOME_PRODUTO02_501.DATA[${k}]`], 0, `DATA[${k}]`);
  } finally { await t.fechar(); }
});
