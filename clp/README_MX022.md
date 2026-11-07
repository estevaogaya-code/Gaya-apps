# Dosagem automática do misturador 600MX022 (silo 7)

Arquivos (gerados por `python3 clp/gerar_mx022.py`, que é a fonte única):

| Arquivo | Para quê |
|---|---|
| `GR9_600MX022_DOSAGEM.L5X` | rotina + 40 tags, para **Import Routine** no programa `MOAGEM_LINHA_502` (RSLogix 5000 V17) |
| `MX22_tags.CSV` | só os tags (alternativa: Tools > Import > Tags) |
| `GR9_600MX022_DOSAGEM_rungs.txt` | os 47 rungs em texto (alternativa: criar a rotina e colar rung a rung) |

Nenhum rung existente é alterado. A rotina comanda as válvulas pelo **mesmo bit da IHM**
(`V600FGxxx.4`): nas cápsulas a saída `O600FGxxxV01C01` = `M600MX022.6` e `.4` e não `.6`;
na FG051 continua valendo a lógica atual (tromba `M600TC002`).

## Funcionamento
- Seletora `ED_600MX022_DOSAGEM` = 1: repete bateladas de 1.000 kg (`MX22_LOTE`).
- Começa com balança < 100 kg (`MX22_VAZIO`), misturador ligado, FG051 fechada e receita válida.
- Dosa produto por produto: abre a cápsula até `alvo − queda aprendida`, fecha, espera 6 s, registra o real.
  - **Queda** (`MX22_QUEDA`): média do que ainda cai depois de fechar (0–200 kg).
  - **Compensação** (`MX22_COMP`): `alvo − real` entra na próxima batelada do produto (±10 % do nominal).
  - **Cápsula vazia**: ganhou < 10 kg em 20 s com a válvula comandada → marca vazia, alarma e passa
    para a outra cápsula do mesmo produto; sem nenhuma → pausa (passo 90) até "Reconhecer alarmes".
  - **Tolerância 10 %**: alarma e segue.
- Descarga automática pela FG051 até < 100 kg por 5 s; registra a batelada e recomeça.
- Seletora = 0: fecha **todas** as válvulas (cápsulas e FG051) uma vez e não mexe mais (comando manual).
- Receita: só pelo celular (tela Misturador). O servidor grava `MX22_PCT[1..4]`, `MX22_CAP_PROD[1..4]`
  e por último `MX22_RCP_SEQ`; o CLP aplica no início da batelada seguinte e zera a compensação.

## Instalação (testar antes numa cópia offline: File > Save As)
1. Finalizar/descartar edições pendentes que já existam (ex.: rung 15 da `GR9_600MX022`).
2. Criar o tag da seletora `ED_600MX022_DOSAGEM` (alias da entrada) se ainda não existir.
3. Botão direito em `MOAGEM_LINHA_502` > **Import Routine…** > `GR9_600MX022_DOSAGEM.L5X`.
   Se a importação recusar o arquivo: importar `MX22_tags.CSV` e colar os rungs do `.txt`.
4. Na `MainRoutine` de `MOAGEM_LINHA_502`, acrescentar no fim: `JSR(GR9_600MX022_DOSAGEM,0);`
5. Verify (sem erros) e só então aceitar/testar as edições online.
6. Conferir os valores iniciais: `MX22_LOTE = 1000`, `MX22_VAZIO = 100` (a rotina põe esses valores se estiverem em 0).

## Teste (nesta ordem)
1. **Seletora desligada**: `MX22_PASSO = 0`; nada muda no comando manual.
2. **Receita**: na tela Misturador, definir os materiais das cápsulas (7.1/7.2 e 7.3/7.4) e gravar a receita.
   Conferir `MX22_PCT[1..4]`, `MX22_CAP_PROD[1..4]` e `MX22_RCP_SEQ` no RSLogix.
3. **Uma batelada acompanhada**, com a tromba posicionada: ligar a seletora, conferir passo 10 → 20 → 25,
   a cápsula certa abrindo, o fechamento perto do alvo, a descarga pela FG051 e `MX22_BAT_SEQ` +1.
4. **Seletora no meio da dosagem**: tudo fecha, `MX22_PASSO = 0`.
5. **Cápsula vazia**: com uma cápsula vazia, conferir a troca para a outra do mesmo material e o alarme.
6. No dia seguinte: o app Cimento Gaúcho (aba Saídas) com o Pozomix 7510 MIX e o consumo das cinzas.
