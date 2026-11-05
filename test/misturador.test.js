// Testes de misturador.js com PLC falso.  Rodar:  node --test test/*.test.js
'use strict';
const test    = require('node:test');
const assert  = require('node:assert/strict');
const fs      = require('fs');
const os      = require('os');
const path    = require('path');
const express = require('express');
const criarMisturador = require('../misturador');
const { pinConfere, ipConfiavel } = require('../receitas');

class FakeTag { constructor(n) { this.name = n; this.value = null; } }
const B = n => 1 << n;

async function montar(env = { RECEITAS_PIN: '9999' }) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mix-'));
  const mem = {
    'EA_600WF023_WT_U01_AJUSTE[9]': 512.4, 'EA_REDE_600MX022_VELOCIDADE_AJUSTE[9]': 30, 'EA_REDE_600MX022_CORRENTE_AJUSTE[9]': 4.27,
    M600MX022: B(5) | B(6) | B(8),
    V600FG044: B(1), O600FG044V01C01: true,   // 7.1 aberta e comandada
    V600FG038: B(0), O600FG038V01C01: false,  // 7.2 fechada
    V600FG040: 0,    O600FG040V01C01: true,   // 7.3 abrindo (sem fim de curso)
    V600FG039: B(0), O600FG039V01C01: false,
    V600FG051: B(0), O600FG051V01C01: false, V600FG052: B(0), O600FG052V01C01: false,
    'Local:1:I.Data.5': true,
  };
  const leituras = [];
  const plc = { async readTag(t) { leituras.push(t.name); if (!(t.name in mem)) throw new Error('x'); t.value = mem[t.name]; } };
  const m = criarMisturador({ Tag: FakeTag, dir, env, pinConfere, ipConfiavel });
  const app = express(); app.use(express.json()); app.use(m.router);
  const srv = await new Promise(r => { const s = app.listen(0, '127.0.0.1', () => r(s)); });
  const base = `http://127.0.0.1:${srv.address().port}`;
  const get = async () => (await fetch(base + '/api/misturador')).json();
  const post = async (corpo, pin) => { const r = await fetch(base + '/api/misturador/rotulos', { method: 'POST', headers: { 'content-type': 'application/json', 'x-receitas-pin': pin || '' }, body: JSON.stringify(corpo) }); return { status: r.status, corpo: await r.json() }; };
  const fechar = async () => { await new Promise(r => srv.close(r)); fs.rmSync(dir, { recursive: true, force: true }); };
  return { m, plc, mem, leituras, get, post, fechar, dir };
}

test('misturador: só lê o CLP com a tela aberta', async () => {
  const t = await montar();
  try {
    await t.m.lerCiclo(t.plc);
    assert.equal(t.leituras.length, 0);
    await t.get();                  // tela abriu
    await t.m.lerCiclo(t.plc);
    assert.ok(t.leituras.includes('EA_600WF023_WT_U01_AJUSTE[9]'));
    assert.ok(!t.leituras.some(n => /Local:/.test(n)), 'sem MISTURADOR_SELETORA não lê seletora');
  } finally { await t.fechar(); }
});

test('misturador: estado de balança, válvulas e misturador', async () => {
  const t = await montar({ RECEITAS_PIN: '9999', MISTURADOR_SELETORA: 'Local:1:I.Data.5' });
  try {
    await t.get(); await t.m.lerCiclo(t.plc);
    const e = (await t.get()).estado;
    assert.equal(e.peso, 512.4);
    assert.deepEqual(e.misturador, { ligado: true, pronto: true, falha: false, hz: 30, a: 4.3 });
    const c = Object.fromEntries(e.capsulas.map(x => [x.id, x]));
    assert.equal(c['7.1'].fg, '600FG044'); assert.equal(c['7.1'].aberta, true); assert.equal(c['7.1'].divergente, false);
    assert.equal(c['7.2'].fechada, true);
    assert.equal(c['7.3'].indefinida, true); assert.equal(c['7.3'].divergente, true);
    assert.deepEqual(e.seletora, { configurada: true, habilitada: true });
    assert.equal(e.descargas.length, 2);
  } finally { await t.fechar(); }
});

test('misturador: nomes das cápsulas exigem PIN e ficam salvos', async () => {
  const t = await montar();
  try {
    let r = await t.post({ rotulos: { '7.1': 'cinza ambar' } }, '0000');
    assert.equal(r.status, 401);
    r = await t.post({ rotulos: { '7.1': 'x'.repeat(21) } }, '9999');
    assert.equal(r.status, 400);
    r = await t.post({ rotulos: { '7.1': 'cinza ambar', '7.4': 'Pampa Sul' } }, '9999');
    assert.equal(r.status, 200);
    assert.equal(r.corpo.rotulos['7.1'], 'CINZA AMBAR');
    assert.equal((await t.get()).capsulas.find(c => c.id === '7.4').rotulo, 'PAMPA SUL');
    await new Promise(res => setTimeout(res, 50));
    assert.equal(JSON.parse(fs.readFileSync(path.join(t.dir, 'misturador_config.json'), 'utf8')).capsulas['7.1'], 'CINZA AMBAR');
  } finally { await t.fechar(); }
});
