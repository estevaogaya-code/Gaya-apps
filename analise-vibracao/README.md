# Análise de vibração — plataforma v2

Hosting: https://analise-de-vibracao-fe8fc.web.app (projeto Firebase `analise-de-vibracao-fe8fc`).

Recomeço do zero: novas coleções `v2_*`. As coleções antigas (`leituras`, `sensores`,
`config/catalogo`) **não são lidas nem alteradas** — continuam no Firestore até decisão sua.

## Estrutura

```
public/            -> publicado no Hosting (firebase.json: "public": "public")
  index.html       visão geral + nomear/editar sensores + relatórios PDF
  historico.html   gráfico e tabela por sensor (?mac=XXXXXXXXXXXX)
  visao_geral.html redireciona para index.html (página antiga)
firmware/sensor_vibracao_c3/  firmware único para os ESP32-C3
legado/            arquivos da v1, só para consulta (NÃO publicados)
```

## Schema Firestore (v2)

| Coleção / doc | Quem grava | Campos |
|---|---|---|
| `v2_sensores/<MAC>` | firmware (ao ligar, a cada 1 h e a cada medição) | `mac`, `fw`, `ip`, `rssi`, `visto` (hora do servidor) |
| `v2_sensores/<MAC>` | painel | `moinho`, `ponto`, `oculto`, `nomeadoEm` |
| `v2_medicoes/<MAC>_<id>` | firmware (a cada 12 h de máquina ligada) | `mac`, `ts` (hora do servidor), `h`, `v`, `a` (mm/s RMS, média do ciclo), `janelas`, `ligadoMin`, `fw` |
| `v2_eventos/<MAC>_<id>` | firmware (pico, fora do ciclo — fw v2.1+) | `mac`, `tipo`=`pico`, `ts` (hora do servidor), `h`, `v`, `a` (RMS da janela de 1 s), `limiar`, `fw` |

- O nome do ponto existe **só** em `v2_sensores`. Renomear vale na hora, inclusive para o histórico.
- `ts` é carimbado pelo servidor (`REQUEST_TIME`) — o relógio do ESP32 não é usado.
- Consultas usam só índices automáticos (nenhum índice composto necessário).

## Painel — critérios

- **Desatualizado**: última medição há mais de 30 h (2,5 ciclos de 12 h).
- **Online/offline**: sinal (`visto`) há menos/mais de 3 h (heartbeat a cada 1 h).
- **Ocultar**: para sensor retirado/substituído; some do painel e dos relatórios, o histórico fica.
- Não é permitido dois sensores ativos com o mesmo moinho + ponto.
- **Pico**: RMS de 1 s ≥ 20 mm/s em qualquer eixo → o sensor grava um evento na hora (máx. 1 a cada 30 min).
  Aparece no card por 30 h, como ✕ no gráfico do histórico e na coluna "Picos > 20" do relatório.
- Regravar o firmware **não** exige renomear: o nome fica em `v2_sensores/<MAC>` e o MAC é do chip.

## Comissionamento de cada sensor

1. Copiar `firmware/sensor_vibracao_c3/segredos_exemplo.h` para `segredos.h` e preencher a senha do Wi-Fi.
2. Gravar o firmware (Board "ESP32C3 Dev Module", USB CDC On Boot Enabled, Flash 4MB,
   Partition "Huge APP (3MB No OTA/1MB SPIFFS)").
3. Ao ligar, o display mostra o MAC por 2,5 s — anotar junto com o local de instalação.
4. Em poucos minutos o sensor aparece em "Novos sensores" no painel → **Nomear**.
5. A 1ª medição sai após 10 min de máquina ligada; depois, a cada 12 h de máquina ligada.

## Deploy (feito por você)

```
cd analise-vibracao
firebase deploy --only hosting
```

`firebase.json` não tem seção `firestore`, então esse deploy não altera regras nem índices.
