# Relógio automotivo retrô — ESP32-C3

Firmware inicial (PlatformIO + Arduino framework) para o relógio que substitui
o mostrador analógico do painel (furo ~80mm). Mostra HH:MM em estilo VFD
retrô (7 segmentos com sombra + glow), data, clima (Open-Meteo) e cor
âmbar/ciano automática por dia/noite, com override manual por 2 botões.

## Hardware

| Função        | Componente                        |
|---------------|------------------------------------|
| MCU           | ESP32-C3                           |
| Display       | ST7735S SPI, 0.96", 80x160, 65K cores |
| RTC           | DS3231 (I2C)                       |
| Botões        | 2x, pull-up interno, ativo em nível baixo |
| Alimentação   | 12V (ACC, fusível 1A) -> buck 3.3V |

## Antes de compilar

1. **Pinos** — em `src/config.h`, ajuste `PIN_TFT_*`, `PIN_I2C_*` e
   `PIN_BTN_*` conforme a placa ESP32-C3 usada. Os valores atuais são um
   ponto de partida genérico, não confirmados pra nenhuma placa específica.
2. **Credenciais e coordenadas** — copie `src/secrets.h.example` para
   `src/secrets.h` (já git-ignorado) e confira:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `HOME_LATITUDE` / `HOME_LONGITUDE` — **preencher com as coordenadas
     reais da casa** (o valor de exemplo é só um placeholder de São Paulo).
3. **Variante do painel ST7735** — `display.cpp` usa `INITR_MINI160x80`
   (padrão pra a maioria dos módulos 0.96" 80x160). Se as cores saírem
   trocadas ou a imagem ficar deslocada/espelhada, teste
   `INITR_GREENTAB160x80` ou ajuste offsets em `Adafruit_ST7735.h`.

## Compilar / gravar

```
pio run -t upload
pio device monitor
```

## Estrutura

- `display.*` — init do ST7735S + framebuffer offscreen (`GFXcanvas16`)
  pra desenhar sem flicker e mandar tudo de uma vez via SPI.
- `rtc_manager.*` — leitura/escrita do DS3231 (RTClib). O RTC guarda sempre
  hora **local** (UTC-3 já aplicado).
- `wifi_manager.*` — conecta e reconecta sozinho se a rede cair.
- `ntp_manager.*` — sincroniza via NTP (pool.ntp.org) com fuso fixo UTC-3
  (sem horário de verão) e grava no RTC.
- `weather.*` — cliente HTTP do Open-Meteo (sem chave), parse do JSON,
  atualização periódica.
- `buttons.*` — debounce (~50ms) dos 2 botões, ciclam a paleta de cores.
- `palette.*` — paleta âmbar/ciano/verde/laranja; decide dia/noite ao ligar
  comparando a hora atual com sunrise/sunset do último dado de clima.
- `segments.*` — um dígito em 7 segmentos com a animação de queda em
  cascata (top -> laterais superiores -> meio -> laterais inferiores ->
  base, ~90ms entre grupos, easing com overshoot).
- `ui_clock.*` — composição da tela: data, HH:MM, clima.

## Limitações conhecidas (firmware inicial)

- Não testado em hardware real — pinos e a variante exata do painel ST7735
  precisam ser confirmados na bancada.
- `weather.cpp` usa `WiFiClientSecure::setInsecure()` (não valida o
  certificado do Open-Meteo) pra simplificar; aceitável porque a API não
  exige autenticação nem trafega dado sensível.
- Sem persistência da escolha manual de cor entre ciclos de ignição — por
  design (o pedido é: reseta a cada liga/desliga do carro).
