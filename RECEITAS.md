# Receitas 501/502 pelo celular

Arquivos: `receitas.js` (servidor), `public/mobile-receitas.html` (tela com escrita),
`public/mobile-status.html` (somente leitura + botão "Receitas"), `test/receitas.test.js`.

## Configuração

| Variável | Efeito |
|---|---|
| `RECEITAS_PIN` | PIN da tela. **Sem ela, toda escrita responde 503.** |
| `RECEITAS_RESTRINGIR_REDE=0` | Desliga a restrição de IP (padrão: só loopback, 10/8, 172.16/12, 192.168/16, 100.64/10 e fd7a:115c:a1e0::/48 do Tailscale). |

No PM2: `RECEITAS_PIN=xxxx pm2 restart <app> --update-env` (ou `env` no ecosystem).
O IP considerado é o do socket (não usa `X-Forwarded-For`). Se um dia houver proxy
reverso na frente, rever isso.

Arquivos gerados ao lado do `server.js`: `receitas_presets.json`, `receitas_log.jsonl`.

## Testes

```
node --test test/receitas.test.js
```
(precisa do `express` instalado, o mesmo do servidor.)

## O que foi conferido no L5K (MOAGEM_MNL62_14092026)

- `PERCENTUAL_RECEITA_L[0]` = CPT de [1]..[9] (GR4_RECEITA_L). Nada usa essa soma.
- Setpoint do componente i: `VA_RECEITAS_LWF005[9+i] = [i] * [10] * (1 + VA_L_UMIDADE[i]/100)`,
  calculado em todo scan; copiado para `VA_RECEITAS_LWF005[1]` aos 2000 ms do passo i
  (ou seja, cada componente "trava" o setpoint quando começa).
- `[10]` = 12000, mesma unidade do `EA_LWF005_WT_U01_AJUSTE[9]` ("PESO NA MOEGA").
  Pelo L5K não dá pra afirmar a unidade; provável kg.
- `VA_RECEITAS_LWF005[2]` só é lido: `ADD([2],1,COUNTER.PRE)` e `GEQ([2],4)` em
  `GR7_501DG004_SILO_4` **e** `GR7_502DG004_SILO_4` — a condição usa **OU entre as duas
  linhas**, então n ≥ 4 em qualquer linha participa do comando dos dois DG004.
- `VD_MR_RECEITAS_L[0].2/.5`: OTL no início do lote (moega abaixo do limite, WF005/WF010),
  OTU no fim (`.3`, ACC = PRE) ou reset (`.15`). Representam o lote inteiro em pesagem.
- 502 é espelho do 501, exceto: limite de liberação fixo em 900 (no 501 é
  `EA_501WF005_WT_U01_AJUSTE[14]`) e a lógica de `[40] = 71/72`.
- **Risco encontrado:** fim de lote é `EQU(ACC, PRE)`. Se `n` diminuir durante o lote
  para um valor menor que o passo atual, ACC passa de PRE e o lote não fecha.
  Por isso o servidor recusa "forçar" quando `n` muda e `COUNTER_MR_RECEITAS_L[0].ACC > novo n`
  (ou quando não consegue ler o ACC).

## Comandos: Grupo 5 e parada do moinho (28/10/2026)

Tela `public/mobile-comandos.html` (botão "⚙️ Comandos" no mobile-status). Mesmo PIN, mesma
rede confiável e mesmo log (`receitas_log.jsonl`, rotas `grupo5` e `parar_moinho`) das receitas.
Tudo pela fila do `receitas.js` — o `server.js` não mudou.

| Rota | O quê |
|---|---|
| `GET /api/comandos/:linha` | estado: `GRP50xGR05`, `GRP50xGR08.16`, `M50xBM140.6`, `M50xBM141.6` e as 9 bombas do grupo |
| `POST /api/comandos/:linha/grupo5` | `{usuario, acao:'ligar'|'desligar', confirmaSeguranca:true}` (PIN) |
| `POST /api/comandos/:linha/parar-moinho` | `{usuario, confirmacao:'PARAR'}` (PIN) |

**Grupo 5** (`GRP50xGR05`, rotinas `GR5_50x_SELECT_DEPT` do L5K): LQ071A01, LQ145A01/A02,
LQ146A01, LQ147A01, LQ150A01, WP001–WP003.
- **Ligar** = escreve `.15 = 1` (o ladder mantém enquanto `.17` selecionado e os intertravamentos
  `.0/.1/.2/.3/.5` estão OK). O servidor recusa se: já ligado, `.16` ativo, não selecionado (`.17`),
  em LOCAL (`.18`) ou intertravamento `.0/.1/.3` ativo. Exige a confirmação "sem pessoas expostas /
  equipamento bloqueado". Se o ladder não mantiver o `.15`, responde `recusado_clp`.
- **Desligar** = `.15 = 0` e `.16 = 1` (o ladder mantém o `.16` até o retorno `.6` cair).
  **Recusado com o moinho (`M50xBM140.6`) ou o BM141 (`M50xBM141.6`) rodando** — o desliga do
  ladder não confere isso.
- O CLP desliga o grupo sozinho 20 h depois do moinho e do BM141 pararem (`TIMER_OFF_GRP5`).

**Parar moinho** = pulso em `GRP50xGR08.16` ("DESLIGA GRUPO 8": selo XIC/OTE, só a IHM zera):
no rung "COMANDO DESLIGA VIA SUPERVISORIO" do `50xBM140` dá OTU no comando do motor. O servidor
liga, espera 1 s, desliga (3 tentativas) e relê. Só aceito com o moinho rodando e confirmação
"PARAR". **Não é parada de emergência de segurança** (não substitui botão de emergência/corda) e
**não religa** o moinho — religar só pelo supervisório.

### Bancada — comandos (com a manutenção avisada)

1. Leitura: `curl -s localhost:3000/api/comandos/501` e conferir com o supervisório: grupo ligado/
   desligado, selecionado, local/remoto, moinho, BM141 e as 9 bombas.
2. Moinho parado, Grupo 5 desligado, selecionado e em remoto: **Ligar** pela tela. Conferir no
   RSLogix `GRP501GR05.15` = 1 e as bombas partindo em sequência.
3. Com o moinho parado: **Desligar**. Conferir `GRP501GR05.16` = 1 até o grupo parar e depois 0,
   e `.15` = 0. Conferir que o grupo **não** volta a partir sozinho.
4. Com o moinho rodando, tentar **Desligar**: tem de recusar (`moinho_rodando`) sem escrever nada.
5. Parar moinho: numa parada programada, com o moinho rodando sem carga e a operação avisada,
   usar **PARAR MOINHO**. Conferir que o moinho para, que `GRP501GR08.16` volta a 0 e que o moinho
   religa normalmente pelo supervisório. Se a IHM usar outro bit para o "desliga", me avise.
6. Conferir as entradas em `receitas_log.jsonl`.

## Peso desejado e resets (14/10/2026)

- **Peso desejado na moega** = `PERCENTUAL_RECEITA_L[10]` (base do lote, kg). Campo na tela,
  gravado **junto com a receita** (mesmo `writeTagGroup`), inteiro de 5.000 a 15.000 kg, com as
  mesmas regras: 409 se mudou no CLP, bloqueio durante a pesagem (com "forçar"), releitura.
  A confirmação mostra o setpoint de cada componente com o peso novo.
- **Reset sistema** = `VD_MR_RECEITAS_L[0].15` e **Reset totalizador** = `VD_MR_RECEITAS_L[0].12`
  (`POST /api/receitas/:linha/reset`, `tipo` = `sistema` | `totalizador`, PIN + rede confiável).
  - No ladder os dois bits só são lidos (XIC/XIO); quem liga e desliga é a IHM. Por isso o servidor
    manda um **pulso**: confere que está desligado, liga, espera 1 s, desliga (até 3 tentativas)
    e relê. Se o bit já estiver ligado, recusa (409). Se não conseguir desligar, avisa `bit_preso`:
    com o .15 ligado a dosagem fica parada — desligar pelo supervisório.
  - Escrita só do bit com o serviço Read-Modify-Write do CLP: os outros bits da palavra
    (.2, .3, .5…, que o ladder escreve) não são tocados.
  - Reset sistema (`.15`): `OTU .2/.5` (fim da pesagem), `.3`, `ACC = 0`, `OTU INICIO_PESAGEM`,
    zera `VA_MR_RECEITAS_ERRO_L[]`. Reset totalizador (`.12`): zera `VA_RECEITAS_LWF005[31..39]`.
  - Liberados durante a pesagem (o reset sistema existe justamente para abortar o lote).

### Bancada — peso e resets (moinho parado)

1. Peso: 12000 → 12001 → conferir `PERCENTUAL_RECEITA_501[10]` no RSLogix → voltar a 12000.
2. Reset totalizador: anotar `VA_RECEITAS_501WF005[31..39]`, enviar, conferir zerados e
   `VD_MR_RECEITAS_501[0].12` = 0.
3. Reset sistema: sem lote em andamento, enviar e conferir `VD_MR_RECEITAS_501[0].15` = 0 e
   `COUNTER_MR_RECEITAS_501[0].ACC` = 0. Se o supervisório tiver o mesmo botão, comparar o efeito.

## Umidade e nomes (02/10/2026)

- **Umidade** `VA_L_UMIDADE[1..9]` (REAL, %): editada na mesma tela e gravada **junto com a
  receita**, no mesmo `writeTagGroup`. Faixa 0 a 30 %, até 2 casas. Entra no setpoint
  (`pct × [10] × (1 + umidade/100)`), então segue as mesmas regras da receita: 409 se mudou no CLP,
  bloqueio durante a pesagem (com "forçar"), releitura e conferência. A confirmação mostra o
  setpoint antes/depois de cada componente.
- **Nomes** `NOME_PRODUTO0i_L` (STRING): botão "Editar nomes", gravação **separada** da receita
  (`POST /api/receitas/:linha/nomes`). Até 12 caracteres, ASCII sem acento, convertido para
  maiúsculas (só os nomes editados). Liberado durante a pesagem: o nome não entra em lógica do ladder.
  - Gravação membro a membro: `.DATA[0..81]` (SINT; caracteres novos e zero no resto) e `.LEN` (DINT).
    O `st-ethernet-ip` lê STRING como estrutura crua e não grava com `Tag` simples.
  - Releitura por `.LEN` + `.DATA[k]`; o cache de nomes do `server.js` é atualizado na hora.
- Presets passam a guardar a umidade (presets antigos sem umidade continuam valendo; ao aplicar,
  a umidade do formulário não muda).

### Bancada — nomes e umidade (moinho parado)

1. Ler: `curl -s localhost:3000/api/receitas` deve trazer `"umidade":[9,0,...]` (ou o valor
   atual) e bater com o supervisório.
2. Nome: renomear um componente **não usado** (ex.: o 9) para `TESTE`. Conferir no supervisório
   e no RSLogix (`NOME_PRODUTO09_501.LEN` = 5). Voltar para vazio.
   Se der `falha_escrita` com `CIP status`, a escrita por membro não é aceita nesse CLP: me avise.
3. Umidade: no mesmo componente 1, umidade atual + 0,01 (ex.: 9 → 9,01). Conferir
   `VA_501_UMIDADE[1]` e o setpoint `VA_RECEITAS_501WF005[10]` no RSLogix. Voltar ao valor anterior.
4. Conferir as entradas em `receitas_log.jsonl`.

## Roteiro de teste de bancada (moinho parado)

1. **Sem PIN:** subir o servidor sem `RECEITAS_PIN`. Conferir que o dashboard e o
   `mobile-status.html` seguem iguais e que `POST /api/receitas/auth` dá 503.
2. **Só leitura:** `curl http://<srv>:3000/api/receitas`. Comparar com o RSLogix/supervisório:
   pct (em %), `n` = `VA_RECEITAS_LWF005[2]`, `soma` = `[0]`, `base` = `[10]`, `pesando`
   e nomes. Repetir com o CLP desligado da rede → 504 em até ~30 s.
3. **Log do ciclo:** o `[CLP] ... ms` no log do PM2 não pode aumentar perceptivelmente.
   Abrir a tela e deixar 5 min: nenhum "Micro Queue".
4. **Escrita que não altera o processo:** moinho parado, sem pesagem (`.2`/`.5` = 0).
   Configurar `RECEITAS_PIN`, abrir a tela, escolher uma linha e trocar 75/25 → 74,99/25,01.
   Conferir no RSLogix `[1]`, `[2]`, `[0]` ≈ 1,0 e `n` inalterado. Voltar para 75/25.
   Conferir as duas entradas em `receitas_log.jsonl`.
5. **n:** com o moinho parado, gravar uma receita com 3 componentes (ex.: 74/25/1) e conferir
   `[2]` = 3 e `COUNTER_MR_RECEITAS_L[0].PRE` = 4. Voltar para 2. **Não** testar n ≥ 4 sem
   antes avaliar o efeito no DG004 do silo 4 nas duas linhas.
6. **Conflito:** abrir a tela, alterar o mesmo valor pelo supervisório, tentar gravar no
   celular → 409 "alguém alterou".
7. **Pesagem:** forçar `.2` (ou esperar um lote real com o moinho em operação) e tentar
   gravar → 409. Testar "forçar" só com mudança que não altera o processo (passo 4).
8. **PIN:** 5 PINs errados → 429 por 10 min (por IP).
9. **Rede:** de um IP fora das faixas (se possível) → 403.

## Recomendação: levar a validação para o ladder

Hoje o CLP aceita qualquer valor em `PERCENTUAL_RECEITA_L[1..9]` e `[2]`, venha do celular,
do supervisório ou de um tag monitor. A validação do servidor protege só este caminho.
Sugestão (não implementada):

1. **Tags de staging** por linha: `RCP_L_STAGE_PCT : REAL[10]`, `RCP_L_STAGE_N : DINT`,
   `RCP_L_APLICAR : BOOL`, `RCP_L_STATUS : DINT` (0 ok, 1 soma, 2 limite, 3 buraco,
   4 pesando, 5 passo) e `RCP_L_SEQ : DINT` (incrementa a cada aplicação aceita).
   Celular/supervisório escrevem **só** no staging e setam `APLICAR`.
2. **Rotina `RCP_L_VALIDA`**, executada na borda de `APLICAR` (ONS):
   - soma de `STAGE_PCT[1..9]` entre 0,9999 e 1,0001;
   - cada componente dentro de `RCP_L_MIN[i]`..`RCP_L_MAX[i]` (limites por material,
     configuráveis só pela engenharia);
   - `STAGE_N` = último componente > 0, e nenhum zero antes dele;
   - intertravamento: `XIO(VD_MR_RECEITAS_L[0].2) XIO(VD_MR_RECEITAS_L[0].5)` — ou aplicar
     no fim do lote, guardando como "pendente" e copiando na borda de `.3`;
   - se tudo OK: `COP(STAGE_PCT[1], PERCENTUAL_RECEITA_L[1], 9)`,
     `MOV(STAGE_N, VA_RECEITAS_LWF005[2])`, `ADD(SEQ,1,SEQ)`, `STATUS := 0`;
     senão `STATUS := código` e nada é copiado. Sempre `OTU(APLICAR)`.
3. Tornar `PERCENTUAL_RECEITA_L` e `VA_RECEITAS_LWF005` **External Access = Read Only**
   (exceto para o próprio ladder), para que o único caminho de escrita seja o staging.
   Avaliar impacto nas telas do supervisório que hoje escrevem direto.
4. Opcional: cópia "travada" da receita no início do lote (`.2`/`.5` em borda), usando a
   cópia nos CPT dos setpoints — elimina o lote misturado mesmo se alguém escrever direto.
5. O servidor então passaria a: escrever staging + `APLICAR`, esperar `SEQ` mudar ou
   `STATUS ≠ 0`, e reportar o `STATUS` do CLP.
