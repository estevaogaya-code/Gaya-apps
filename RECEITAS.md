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
