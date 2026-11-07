/**
 * Misturador 600MX022 (silo 7) — tela public/mobile-misturador.html e
 * integração com a dosagem automática do CLP (rotina GR9_600MX022_DOSAGEM,
 * gerada por clp/gerar_mx022.py).
 *
 * Leitura (tags do L5K MOAGEM_MNL62_14092026):
 *   EA_600WF023_WT_U01_AJUSTE[9]           REAL, peso da balança do misturador (kg)
 *   M600MX022                              DINT, .5 pronto, .6 retorno de ligado, .8 disjuntor OK (0 = trip)
 *   EA_REDE_600MX022_VELOCIDADE/CORRENTE_AJUSTE[9]  Hz e A do inversor
 *   V600FGxxx (.0 fechada, .1 aberta) e O600FGxxxV01C01 (comando)  — cápsulas 7.1..7.4 e FG051
 *   ED_600MX022_DOSAGEM                    BOOL, seletora da porta do painel (dosagem habilitada)
 *   MX22_*                                 tags da rotina de dosagem (ver clp/gerar_mx022.py)
 *
 * Escrita (só pelo celular, com PIN das receitas + rede confiável, pela fila
 * drenada no ciclo do server.js — nunca concorrente com a leitura):
 *   receita  -> MX22_PCT[1..4] (fração por produto), MX22_CAP_PROD[1..4] (cápsula -> produto)
 *               e por último MX22_RCP_SEQ+1. O CLP só aplica no início da próxima batelada.
 *   reset    -> MX22_RESET = 1 (o CLP zera alarmes e retoma da pausa).
 *
 * Bateladas: o servidor lê MX22_BAT_SEQ a cada ciclo; quando muda, soma a
 * diferença de MX22_ACUM_KG[1..4] (acumuladores que nunca zeram — nada se
 * perde se o servidor ficar fora do ar) e grava em misturador_bateladas.json
 * (45 dias). aplicarNoPayload() leva o total do dia para o app Cimento Gaúcho
 * (aba Saídas: pozo7510mix, receitaMix e mixConsumo = t reais por material).
 *
 * O resto da tela (balança, válvulas, misturador) só é lido com a tela aberta.
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
const DESCARGAS = ['600FG051'];
// Material de cada cápsula: nome do material no app (estoque) e nome mostrado
const MATERIAIS = [
  { chave: 'Cinza',           rotulo: 'Cinza Braskem' },
  { chave: 'Cinza Pampa-Sul', rotulo: 'Cinza Pampa' },
  { chave: 'CINZA AMBAR',     rotulo: 'Cinza Ambar' },
];
const TAG_SELETORA = 'ED_600MX022_DOSAGEM';
const BALANCA_MAX = 6000, BATELADA = 1000, DIAS_HIST = 45, N_PROD = 4;
const PASSOS = { 0: 'Parado', 10: 'Aguardando balança vazia / condições', 20: 'Próximo produto', 22: 'Escolhendo cápsula',
  25: 'Dosando', 30: 'Estabilizando', 40: 'Descarregando', 45: 'Registrando batelada', 90: 'PAUSA — sem cápsula com material' };
const ALARMES = ['Produto fora da tolerância de 10%', 'Cápsula vazia (trocou para a outra)', 'Nenhuma cápsula com o produto — dosagem pausada',
  'Descarga demorando mais de 10 min', 'Sem receita no CLP', 'Misturador desligado'];

const bit = (v, b) => ((Number(v) | 0) & (1 << b)) !== 0;
function estadoValvula(v, cmd) {
  const aberta = bit(v, 1), fechada = bit(v, 0);
  return { aberta, fechada, indefinida: !aberta && !fechada, comandoAbrir: !!cmd,
           divergente: (!!cmd && !aberta) || (!cmd && aberta) };
}
function materialDe(valor) {
  const s = String(valor || '').trim().toLowerCase();
  const m = MATERIAIS.find(x => x.chave.toLowerCase() === s || x.rotulo.toLowerCase() === s);
  return m ? m.chave : '';
}
const rotuloDe = chave => (MATERIAIS.find(m => m.chave === chave) || {}).rotulo || chave || '';

// Receita do app (mps por material) + material de cada cápsula -> o que vai pro CLP
function montarReceitaClp(rec, capsulasCfg) {
  const caps = CAPSULAS.map(c => capsulasCfg[c.id] || '');
  const produtos = [];
  caps.forEach(m => { if (m && !produtos.includes(m)) produtos.push(m); });
  const mps = Object.entries((rec && rec.mps) || {}).filter(([, f]) => Number(f) > 0);
  if (!mps.length) return { erro: 'receita_vazia', mensagem: 'A receita não tem componentes.' };
  for (const [m] of mps) if (!produtos.includes(m))
    return { erro: 'material_sem_capsula', mensagem: `"${rotuloDe(m)}" não está em nenhuma cápsula. Ajuste os materiais das cápsulas.` };
  if (produtos.length > N_PROD) return { erro: 'produtos_demais', mensagem: 'Mais de 4 materiais nas cápsulas.' };
  const soma = mps.reduce((s, [, f]) => s + Number(f), 0);
  if (Math.abs(soma - 1) > 0.002) return { erro: 'soma_invalida', mensagem: `A receita soma ${(soma * 100).toFixed(1)}% (precisa 100%).` };
  const pct = [0, 0, 0, 0], capProd = [0, 0, 0, 0], prodMat = {};
  produtos.forEach((m, i) => { prodMat[i + 1] = m; const e = mps.find(([x]) => x === m); pct[i] = e ? Number(e[1]) : 0; });
  caps.forEach((m, i) => { capProd[i] = m ? produtos.indexOf(m) + 1 : 0; });
  return { pct, capProd, prodMat };
}

function criarMisturador(opts) {
  const {
    Tag,
    dir = __dirname,
    env = process.env,
    agora = () => Date.now(),
    leituraMs = 1000,
    interesseMs = 60000,
    esperaJobMs = 20000,
    pinConfere = () => false,
    ipConfiavel = () => false,
    getPayload = async () => null,
  } = opts;
  const ARQ = path.join(dir, 'misturador_config.json');
  const ARQ_HIST = path.join(dir, 'misturador_bateladas.json');
  let config = { capsulas: {}, receitas: {}, receitaAtual: null };
  try { config = { ...config, ...JSON.parse(fs.readFileSync(ARQ, 'utf8')) }; } catch (_) {}
  // migração: nomes livres da primeira versão -> material do app
  for (const c of CAPSULAS) config.capsulas[c.id] = materialDe(config.capsulas[c.id]);
  let hist = { ultimoSeq: null, ultimoAcum: null, registros: [] };
  try { hist = { ...hist, ...JSON.parse(fs.readFileSync(ARQ_HIST, 'utf8')) }; } catch (_) {}

  let salvando = Promise.resolve();
  const salvar = (arq, obj) => { const t = JSON.stringify(obj, null, 1); salvando = salvando.then(() => fs.promises.writeFile(arq, t, 'utf8')).catch(e => console.error('[MISTURADOR] erro salvando', arq, e.message)); return salvando; };

  let ultimoInteresse = 0, estado = null, ultimoCiclo = 0;
  const fila = [];

  function lerTag(PLC, nome) {
    const tag = new Tag(nome);
    return new Promise(resolve => {
      const t = setTimeout(() => resolve(null), leituraMs);
      Promise.resolve(PLC.readTag(tag)).then(() => { clearTimeout(t); resolve(tag.value); }, () => { clearTimeout(t); resolve(null); });
    });
  }
  async function escrever(PLC, nome, valor) {
    const tag = new Tag(nome);
    await PLC.readTag(tag);            // descobre o tipo antes de escrever
    tag.value = valor;
    await PLC.writeTag(tag);
    const v = await lerTag(PLC, nome);
    const igual = typeof valor === 'number' ? Math.abs(Number(v) - Math.fround(valor)) < 1e-4 : !!v === !!valor;
    if (!igual) throw new Error(`releitura de ${nome} = ${v} (esperado ${valor})`);
  }
  function enfileirar(fn) {
    return new Promise((resolve, reject) => {
      const job = { fn, resolve, reject };
      job.timer = setTimeout(() => { const i = fila.indexOf(job); if (i >= 0) { fila.splice(i, 1); reject(Object.assign(new Error('CLP sem resposta — nada foi enviado.'), { status: 504 })); } }, esperaJobMs);
      fila.push(job);
    });
  }

  // ── Bateladas ──────────────────────────────────────────────────────────────
  async function lerBateladas(PLC) {
    const seq = await lerTag(PLC, 'MX22_BAT_SEQ');
    if (seq == null) return;                                 // rotina ainda não instalada
    if (hist.ultimoSeq === Number(seq) && hist.ultimoAcum) return;
    const acum = [];
    for (let p = 1; p <= N_PROD; p++) acum.push(Number(await lerTag(PLC, `MX22_ACUM_KG[${p}]`)) || 0);
    const rcp = Number(await lerTag(PLC, 'MX22_BAT_RCP')) || 0;
    if (!hist.ultimoAcum || hist.ultimoSeq == null) {       // primeira leitura: só guarda a referência
      hist.ultimoSeq = Number(seq); hist.ultimoAcum = acum; await salvar(ARQ_HIST, hist); return;
    }
    const info = config.receitas[rcp] || {};
    const kg = {}; let total = 0;
    acum.forEach((v, i) => {
      let d = v - (hist.ultimoAcum[i] || 0);
      if (d < 0) d = v;                                      // acumulador zerado (download do CLP)
      if (d > 0) { const m = (info.produtos || {})[i + 1] || `Produto ${i + 1}`; kg[m] = (kg[m] || 0) + d; total += d; }
    });
    const nBat = Number(seq) >= hist.ultimoSeq ? Number(seq) - hist.ultimoSeq : 1;
    if (total > 0) hist.registros.push({ ts: agora(), bateladas: nBat, kg, totalKg: total, receita: info.nome || null, rcp });
    const corte = agora() - DIAS_HIST * 86400000;
    hist.registros = hist.registros.filter(r => r.ts >= corte);
    hist.ultimoSeq = Number(seq); hist.ultimoAcum = acum;
    if (total > 0) console.log(`[MISTURADOR] batelada(s) ${nBat}: ${Math.round(total)} kg`, kg);
    await salvar(ARQ_HIST, hist);
  }

  // Chamado pelo server.js a cada ciclo
  async function lerCiclo(PLC) {
    ultimoCiclo = agora();
    while (fila.length) {
      const job = fila.shift(); clearTimeout(job.timer);
      try { job.resolve(await job.fn(PLC)); } catch (e) { job.reject(e); }
    }
    try { await lerBateladas(PLC); } catch (e) { console.error('[MISTURADOR] bateladas:', e.message); }
    if (agora() - ultimoInteresse > interesseMs) return;
    const n = v => (v == null ? null : Number(v));
    const peso = n(await lerTag(PLC, 'EA_600WF023_WT_U01_AJUSTE[9]'));
    const hz = n(await lerTag(PLC, 'EA_REDE_600MX022_VELOCIDADE_AJUSTE[9]'));
    const a = n(await lerTag(PLC, 'EA_REDE_600MX022_CORRENTE_AJUSTE[9]'));
    const mx = n(await lerTag(PLC, 'M600MX022'));
    const valvula = async fg => {
      const v = await lerTag(PLC, `V${fg}`);
      const c = await lerTag(PLC, `O${fg}V01C01`);
      return v == null ? { fg, erro: true } : { fg, ...estadoValvula(v, c) };
    };
    const capsulas = [];
    for (const c of CAPSULAS) capsulas.push({ id: c.id, ...(await valvula(c.fg)) });
    const descargas = [];
    for (const fg of DESCARGAS) descargas.push(await valvula(fg));
    const sel = await lerTag(PLC, env.MISTURADOR_SELETORA || TAG_SELETORA);
    // Dosagem automática (null se a rotina ainda não foi instalada no CLP)
    const passo = n(await lerTag(PLC, 'MX22_PASSO'));
    let dosagem = null;
    if (passo != null) {
      const g = async t => n(await lerTag(PLC, t));
      const alm = await g('MX22_ALM'), vazia = await g('MX22_VAZIA'), tolProd = await g('MX22_ALM_TOL_PROD');
      const pct = []; for (let p = 1; p <= N_PROD; p++) pct.push(await g(`MX22_PCT_ATIVO[${p}]`));
      dosagem = {
        passo, passoTexto: PASSOS[passo] || `Passo ${passo}`,
        produto: await g('MX22_PROD'), capsula: await g('MX22_CAP'),
        alvo: await g('MX22_ALVO'), dosado: await g('MX22_DOSADO'),
        rcpSeq: await g('MX22_RCP_SEQ'), rcpSeqAtivo: await g('MX22_RCP_SEQ_AT'), batSeq: await g('MX22_BAT_SEQ'),
        pctAtivo: pct,
        alarmes: ALARMES.map((t, i) => ({ bit: i, texto: t, ativo: bit(alm, i) })).filter(x => x.ativo),
        capsulasVazias: CAPSULAS.filter((c, i) => bit(vazia, i + 1)).map(c => c.id),
        produtosForaTol: [1, 2, 3, 4].filter(p => bit(tolProd, p)),
      };
    }
    estado = {
      ts: agora(),
      peso: peso == null ? null : Math.round(peso * 10) / 10,
      misturador: mx == null ? null : { ligado: bit(mx, 6), pronto: bit(mx, 5), falha: !bit(mx, 8),
        hz: hz == null ? null : Math.round(hz * 10) / 10, a: a == null ? null : Math.round(a * 10) / 10 },
      capsulas, descargas,
      seletora: { habilitada: sel == null ? null : !!sel, configurada: sel != null },
      dosagem,
    };
  }

  // ── Resumo por dia (tela e sincronização com o app) ───────────────────────
  function resumoPorDia() {
    const dias = {};
    for (const r of hist.registros) {
      const d = new Date(r.ts);
      const chave = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
      const x = dias[chave] || (dias[chave] = { data: chave, bateladas: 0, totalKg: 0, kg: {}, receita: null });
      x.bateladas += r.bateladas || 0; x.totalKg += r.totalKg || 0;
      Object.entries(r.kg || {}).forEach(([m, v]) => { x.kg[m] = (x.kg[m] || 0) + v; });
      if (r.receita) x.receita = r.receita;
    }
    return dias;
  }
  // Leva o pesado de cada dia para a aba Saídas do app (só dias com batelada registrada)
  function aplicarNoPayload(payload) {
    if (!payload || !Array.isArray(payload.months)) return [];
    const mudou = [];
    for (const d of Object.values(resumoPorDia())) {
      const [a, m, dd] = d.data.split('-');
      const mes = payload.months.find(x => x.key === `${a}-${m}`);
      const row = mes && Array.isArray(mes.saidas) ? mes.saidas.find(r => r.data === `${dd}/${m}`) : null;
      if (!row) continue;
      const t = Math.round(d.totalKg) / 1000;
      const cons = {}; Object.entries(d.kg).forEach(([k, v]) => { cons[k] = Math.round(v) / 1000; });
      let alterou = false;
      if (row.pozo7510mix !== t) { row.pozo7510mix = t; alterou = true; }
      if (JSON.stringify(row.mixConsumo || null) !== JSON.stringify(cons)) { row.mixConsumo = cons; alterou = true; }
      if (d.receita && row.receitaMix !== d.receita) { row.receitaMix = d.receita; alterou = true; }
      if (alterou) mudou.push(`${dd}/${m}: ${t} t`);
    }
    return mudou;
  }

  // ── Segurança (mesmo PIN e rede das receitas) ─────────────────────────────
  const falhas = new Map();
  function autorizado(req, res) {
    const ip = String((req.socket && req.socket.remoteAddress) || '').replace(/^::ffff:/, '');
    if (env.RECEITAS_RESTRINGIR_REDE !== '0' && !ipConfiavel(ip)) { res.status(403).json({ ok: false, erro: 'rede_nao_confiavel', mensagem: 'Só a partir da rede da fábrica / Tailscale.' }); return false; }
    const f = falhas.get(ip);
    if (f && f.ate > agora()) { res.status(429).json({ ok: false, erro: 'bloqueado', mensagem: 'Muitas tentativas de PIN. Aguarde 10 min.' }); return false; }
    if (!env.RECEITAS_PIN) { res.status(503).json({ ok: false, erro: 'escrita_desabilitada', mensagem: 'RECEITAS_PIN não configurado no servidor.' }); return false; }
    if (pinConfere(req.get('x-receitas-pin'), env.RECEITAS_PIN)) { falhas.delete(ip); return true; }
    const r = f && agora() - f.ultima < 600000 ? f : { n: 0, ultima: 0, ate: 0 };
    r.n++; r.ultima = agora(); if (r.n >= 5) { r.ate = agora() + 600000; r.n = 0; }
    falhas.set(ip, r);
    res.status(401).json({ ok: false, erro: 'pin_invalido', mensagem: 'PIN incorreto.' });
    return false;
  }
  const usuarioDe = b => { const u = typeof (b || {}).usuario === 'string' ? b.usuario.trim() : ''; return u && u.length <= 60 ? u : null; };

  // ── Rotas ──────────────────────────────────────────────────────────────────
  const router = express.Router();
  router.get('/api/misturador', async (req, res) => {
    ultimoInteresse = agora();
    let receitasMix = [];
    try { const p = await getPayload(); receitasMix = ((p && p.receitas) || []).filter(r => r.mix).map(r => ({ nome: r.produto, mps: r.mps || {} })); } catch (_) {}
    const dias = resumoPorDia();
    const d = new Date(agora());
    const hoje = dias[`${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`] || null;
    res.json({
      ok: true, estado, idadeMs: estado ? agora() - estado.ts : null, clpCicloMs: ultimoCiclo ? agora() - ultimoCiclo : null,
      balancaMax: BALANCA_MAX, batelada: BATELADA, materiais: MATERIAIS,
      capsulas: CAPSULAS.map(c => ({ ...c, material: config.capsulas[c.id] || '', rotulo: rotuloDe(config.capsulas[c.id]) })),
      receitasMix, receitaAtual: config.receitaAtual, receitasClp: config.receitas,
      hoje, ultimos: hist.registros.slice(-10).reverse(),
    });
  });

  router.get('/api/misturador/historico', (req, res) => {
    res.json({ ok: true, dias: Object.values(resumoPorDia()).sort((a, b) => b.data.localeCompare(a.data)) });
  });

  router.post('/api/misturador/capsulas', (req, res) => {
    if (!autorizado(req, res)) return;
    const b = req.body || {}, novos = {};
    for (const c of CAPSULAS) {
      const v = b.capsulas && c.id in b.capsulas ? String(b.capsulas[c.id] || '') : config.capsulas[c.id] || '';
      if (v && !MATERIAIS.some(m => m.chave === v)) return res.status(400).json({ ok: false, erro: 'material_invalido', mensagem: `Material inválido na cápsula ${c.id}.` });
      novos[c.id] = v;
    }
    config.capsulas = novos;
    salvar(ARQ, config);
    res.json({ ok: true, capsulas: novos, mensagem: 'Materiais salvos. Aplique a receita de novo para o CLP usar a nova distribuição.' });
  });

  router.post('/api/misturador/receita', async (req, res) => {
    if (!autorizado(req, res)) return;
    const usuario = usuarioDe(req.body);
    if (!usuario) return res.status(400).json({ ok: false, erro: 'usuario_obrigatorio', mensagem: 'Informe o operador.' });
    let payload = null;
    try { payload = await getPayload(true); } catch (_) {}
    const nome = String((req.body || {}).receita || '');
    const rec = ((payload && payload.receitas) || []).find(r => r.mix && r.produto === nome);
    if (!rec) return res.status(400).json({ ok: false, erro: 'receita_inexistente', mensagem: 'Receita de mistura não encontrada no app.' });
    const r = montarReceitaClp(rec, config.capsulas);
    if (r.erro) return res.status(409).json({ ok: false, ...r });
    try {
      const seq = await enfileirar(async PLC => {
        for (let p = 1; p <= N_PROD; p++) await escrever(PLC, `MX22_PCT[${p}]`, r.pct[p - 1]);
        for (let c = 1; c <= N_PROD; c++) await escrever(PLC, `MX22_CAP_PROD[${c}]`, r.capProd[c - 1]);
        const atual = Number(await lerTag(PLC, 'MX22_RCP_SEQ')) || 0;
        await escrever(PLC, 'MX22_RCP_SEQ', atual + 1);
        return atual + 1;
      });
      config.receitas[seq] = { nome, produtos: r.prodMat, pct: r.pct, ts: agora(), usuario };
      const seqs = Object.keys(config.receitas).map(Number).sort((a, b) => a - b);
      while (seqs.length > 50) delete config.receitas[seqs.shift()];
      config.receitaAtual = nome;
      await salvar(ARQ, config);
      console.log(`[MISTURADOR] receita "${nome}" gravada no CLP (seq ${seq}) por ${usuario}`);
      res.json({ ok: true, seq, mensagem: `Receita "${nome}" gravada no CLP. Entra no início da próxima batelada.` });
    } catch (e) {
      res.status(e.status || 502).json({ ok: false, erro: 'falha_escrita', mensagem: `Não foi possível gravar no CLP: ${e.message}. Confira se a rotina e os tags MX22_* foram criados.` });
    }
  });

  router.post('/api/misturador/reset', async (req, res) => {
    if (!autorizado(req, res)) return;
    const usuario = usuarioDe(req.body);
    if (!usuario) return res.status(400).json({ ok: false, erro: 'usuario_obrigatorio', mensagem: 'Informe o operador.' });
    try {
      await enfileirar(PLC => escrever(PLC, 'MX22_RESET', true).catch(e => { if (!/releitura/.test(e.message)) throw e; }));
      console.log(`[MISTURADOR] alarmes reconhecidos por ${usuario}`);
      res.json({ ok: true, mensagem: 'Alarmes reconhecidos.' });
    } catch (e) {
      res.status(e.status || 502).json({ ok: false, erro: 'falha_escrita', mensagem: `Não foi possível gravar no CLP: ${e.message}` });
    }
  });

  return { router, lerCiclo, aplicarNoPayload, resumoPorDia, _config: () => config, _hist: () => hist };
}

module.exports = criarMisturador;
module.exports.estadoValvula = estadoValvula;
module.exports.montarReceitaClp = montarReceitaClp;
module.exports.CAPSULAS = CAPSULAS;
module.exports.MATERIAIS = MATERIAIS;
