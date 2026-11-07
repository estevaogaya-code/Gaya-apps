// Testes de misturador.js com PLC falso.  Rodar:  node --test test/*.test.js
'use strict';
const test    = require('node:test');
const assert  = require('node:assert/strict');
const fs      = require('fs');
const os      = require('os');
const path    = require('path');
const express = require('express');
const criarMisturador = require('../misturador');
const { montarReceitaClp } = criarMisturador;
const { pinConfere, ipConfiavel } = require('../receitas');

class FakeTag { constructor(n) { this.name = n; this.value = null; } }
const B = n => 1 << n;
const sleep = ms => new Promise(r => setTimeout(r, ms));

const PAYLOAD = {
  receitas: [
    { produto: 'MIX-60-40', mix: true, mps: { 'Cinza': 0.6, 'CINZA AMBAR': 0.4 } },
    { produto: 'MIX-PAMPA', mix: true, mps: { 'Cinza Pampa-Sul': 1 } },
    { produto: 'POZOMIX', mps: { 'Cinza': 0.9 } },
  ],
  months: [{ key: '2026-11', saidas: [{ data: '07/11', pozo7510mix: null }, { data: '08/11' }] }],
};

async function montar(o = {}) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mix-'));
  const mem = {
    'EA_600WF023_WT_U01_AJUSTE[9]': 512.4, 'EA_REDE_600MX022_VELOCIDADE_AJUSTE[9]': 30, 'EA_REDE_600MX022_CORRENTE_AJUSTE[9]': 4.27,
    M600MX022: B(5) | B(6) | B(8),
    V600FG044: B(1), O600FG044V01C01: true, V600FG038: B(0), O600FG038V01C01: false,
    V600FG040: 0, O600FG040V01C01: true, V600FG039: B(0), O600FG039V01C01: false,
    V600FG051: B(0), O600FG051V01C01: false, ED_600MX022_DOSAGEM: true,
  };
  if (o.rotina !== false) {
    Object.assign(mem, { MX22_PASSO: 25, MX22_PROD: 1, MX22_CAP: 1, MX22_ALVO: 600, MX22_DOSADO: 420, MX22_RCP_SEQ: 0, MX22_RCP_SEQ_AT: 0,
      MX22_BAT_SEQ: 0, MX22_BAT_RCP: 0, MX22_ALM: B(1), MX22_VAZIA: B(2), MX22_ALM_TOL_PROD: 0, MX22_RESET: false });
    for (let i = 1; i <= 4; i++) Object.assign(mem, { [`MX22_PCT[${i}]`]: 0, [`MX22_CAP_PROD[${i}]`]: 0, [`MX22_PCT_ATIVO[${i}]`]: 0, [`MX22_ACUM_KG[${i}]`]: 0 });
  }
  const escritas = [];
  const plc = {
    async readTag(t) { if (!(t.name in mem)) throw new Error('tag inexistente ' + t.name); t.value = mem[t.name]; },
    async writeTag(t) { escritas.push([t.name, t.value]); mem[t.name] = typeof t.value === 'number' ? Math.fround(t.value) : t.value; },
  };
  let tempo = new Date(2026, 10, 7, 10, 0).getTime();
  const m = criarMisturador({ Tag: FakeTag, dir, env: { RECEITAS_PIN: '9999', RECEITAS_RESTRINGIR_REDE: '0' }, pinConfere, ipConfiavel,
    getPayload: async () => PAYLOAD, agora: () => tempo, esperaJobMs: 2000 });
  const app = express(); app.use(express.json()); app.use(m.router);
  const srv = await new Promise(r => { const s = app.listen(0, '127.0.0.1', () => r(s)); });
  const base = `http://127.0.0.1:${srv.address().port}`;
  let ciclo = true; (async () => { while (ciclo) { await m.lerCiclo(plc); await sleep(20); } })();
  const get = async () => (await fetch(base + '/api/misturador')).json();
  const post = async (url, corpo, pin = '9999') => { const r = await fetch(base + url, { method: 'POST', headers: { 'content-type': 'application/json', 'x-receitas-pin': pin }, body: JSON.stringify(corpo) }); return { status: r.status, corpo: await r.json() }; };
  const fechar = async () => { ciclo = false; await sleep(40); await new Promise(r => srv.close(r)); fs.rmSync(dir, { recursive: true, force: true }); };
  return { m, plc, mem, escritas, get, post, fechar, dir, avancar: ms => { tempo += ms; } };
}

test('montarReceitaClp: produtos pelas cápsulas, % e validações', () => {
  const caps = { '7.1': 'Cinza', '7.2': 'Cinza', '7.3': 'CINZA AMBAR', '7.4': 'CINZA AMBAR' };
  const r = montarReceitaClp(PAYLOAD.receitas[0], caps);
  assert.deepEqual(r.pct.map(x => +x.toFixed(2)), [0.6, 0.4, 0, 0]);
  assert.deepEqual(r.capProd, [1, 1, 2, 2]);
  assert.deepEqual(r.prodMat, { 1: 'Cinza', 2: 'CINZA AMBAR' });
  assert.equal(montarReceitaClp(PAYLOAD.receitas[1], caps).erro, 'material_sem_capsula');
  assert.equal(montarReceitaClp({ mps: { Cinza: 0.5 } }, caps).erro, 'soma_invalida');
});

test('misturador: estado da tela, dosagem e seletora', async () => {
  const t = await montar();
  try {
    await t.get(); await sleep(120);
    const e = (await t.get()).estado;
    assert.equal(e.peso, 512.4);
    assert.equal(e.seletora.habilitada, true);
    assert.equal(e.capsulas.find(c => c.id === '7.3').divergente, true);
    assert.equal(e.dosagem.passoTexto, 'Dosando');
    assert.deepEqual(e.dosagem.capsulasVazias, ['7.2']);
    assert.equal(e.dosagem.alarmes[0].texto.startsWith('Cápsula vazia'), true);
  } finally { await t.fechar(); }
});

test('misturador: sem a rotina no CLP a tela funciona (dosagem = null)', async () => {
  const t = await montar({ rotina: false });
  try { await t.get(); await sleep(120); assert.equal((await t.get()).estado.dosagem, null); } finally { await t.fechar(); }
});

test('misturador: materiais das cápsulas e receita gravada no CLP', async () => {
  const t = await montar();
  try {
    let r = await t.post('/api/misturador/capsulas', { capsulas: { '7.1': 'Cinza', '7.2': 'Cinza', '7.3': 'CINZA AMBAR', '7.4': 'CINZA AMBAR' } }, '0000');
    assert.equal(r.status, 401);
    r = await t.post('/api/misturador/capsulas', { capsulas: { '7.1': 'Areia' } });
    assert.equal(r.status, 400);
    r = await t.post('/api/misturador/capsulas', { capsulas: { '7.1': 'Cinza', '7.2': 'Cinza', '7.3': 'CINZA AMBAR', '7.4': 'CINZA AMBAR' } });
    assert.equal(r.status, 200);
    const g = await t.get();
    assert.equal(g.capsulas[0].rotulo, 'Cinza Braskem');
    assert.deepEqual(g.receitasMix.map(x => x.nome), ['MIX-60-40', 'MIX-PAMPA']);

    r = await t.post('/api/misturador/receita', { usuario: 'Op', receita: 'MIX-PAMPA' });
    assert.equal(r.status, 409);
    r = await t.post('/api/misturador/receita', { usuario: 'Op', receita: 'MIX-60-40' });
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.ok(Math.abs(t.mem['MX22_PCT[1]'] - 0.6) < 1e-6);
    assert.equal(t.mem['MX22_CAP_PROD[3]'], 2);
    assert.equal(t.mem.MX22_RCP_SEQ, 1);
    assert.equal(t.escritas[t.escritas.length - 1][0], 'MX22_RCP_SEQ'); // seq por último
  } finally { await t.fechar(); }
});

test('misturador: bateladas pelo acumulador, histórico e aba Saídas do app', async () => {
  const t = await montar();
  try {
    await t.post('/api/misturador/capsulas', { capsulas: { '7.1': 'Cinza', '7.2': 'Cinza', '7.3': 'CINZA AMBAR', '7.4': 'CINZA AMBAR' } });
    await t.post('/api/misturador/receita', { usuario: 'Op', receita: 'MIX-60-40' });
    await sleep(80);                                    // primeira leitura = referência
    Object.assign(t.mem, { 'MX22_ACUM_KG[1]': 600, 'MX22_ACUM_KG[2]': 402, MX22_BAT_SEQ: 1, MX22_BAT_RCP: 1 });
    await sleep(80);
    Object.assign(t.mem, { 'MX22_ACUM_KG[1]': 1795, 'MX22_ACUM_KG[2]': 1200, MX22_BAT_SEQ: 3 }); // 2 bateladas de uma vez
    await sleep(80);
    const h = t.m._hist();
    assert.equal(h.registros.length, 2);
    assert.deepEqual(h.registros[0].kg, { Cinza: 600, 'CINZA AMBAR': 402 });
    assert.equal(h.registros[1].bateladas, 2);
    const dia = t.m.resumoPorDia()['2026-11-07'];
    assert.equal(dia.bateladas, 3); assert.equal(dia.totalKg, 2995); assert.equal(dia.receita, 'MIX-60-40');

    const payload = JSON.parse(JSON.stringify(PAYLOAD));
    assert.deepEqual(t.m.aplicarNoPayload(payload), ['07/11: 2.995 t']);
    const row = payload.months[0].saidas[0];
    assert.equal(row.pozo7510mix, 2.995);
    assert.deepEqual(row.mixConsumo, { Cinza: 1.795, 'CINZA AMBAR': 1.2 });
    assert.equal(row.receitaMix, 'MIX-60-40');
    assert.deepEqual(t.m.aplicarNoPayload(payload), []);           // sem mudança, não regrava
    assert.ok(fs.existsSync(path.join(t.dir, 'misturador_bateladas.json')));
  } finally { await t.fechar(); }
});

test('misturador: reconhecer alarmes escreve MX22_RESET', async () => {
  const t = await montar();
  try {
    const r = await t.post('/api/misturador/reset', { usuario: 'Op' });
    assert.equal(r.status, 200, JSON.stringify(r.corpo));
    assert.deepEqual(t.escritas.pop(), ['MX22_RESET', true]);
  } finally { await t.fechar(); }
});
