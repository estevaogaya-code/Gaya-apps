# Assistente de voz automotivo (ESP32-S3)

Firmware inicial (ESP-IDF, C puro) para o assistente de voz que substitui o
relogio analogico do painel (furo redondo de ~80mm). Cobre a estrutura
completa pedida: display, audio (mic + amp), maquina de estados de tela,
scaffold de wake word/comandos locais, e o fallback online via API de IA.

## Hardware / pinagem

| Funcao              | Pino ESP32-S3 |
|---------------------|---------------|
| Display SCK         | GPIO12 |
| Display MOSI        | GPIO11 |
| Display CS          | GPIO10 |
| Display DC          | GPIO9  |
| Display RES         | GPIO8  |
| Display BLK (PWM)   | GPIO7  |
| Mic WS (LRCLK)       | GPIO4  |
| Mic BCLK             | GPIO5  |
| Mic SD (dados)       | GPIO6  |
| Amp BCLK             | GPIO16 |
| Amp LRC              | GPIO17 |
| Amp DIN              | GPIO18 |
| Botao push-to-talk   | `CONFIG_VA_PIN_PTT_BUTTON` (menuconfig, default GPIO0) |

Display ST7735S 0.96" 80x160, mic INMP441 (L/R -> GND, canal esquerdo),
amp MAX98357A (GAIN flutuando = 9dB, SD -> VCC = sempre habilitado).
Mic e amp usam instancias de periferico I2S separadas (I2S_NUM_0 RX /
I2S_NUM_1 TX), como pedido.

## Estrutura do codigo

```
main/
  config.h              pinagem + paleta de cores
  main.c                app_main(), display_task, voice_task
  app/app_state.*        maquina de estados (idle/listening/responding)
  display/
    st7735.*             driver SPI do controlador (sem libs externas)
    font5x7.h             fonte 5x7 gerada por scripts/gen_font.py
    ui_screens.*          desenha as 3 telas do mockup
  audio/
    i2s_mic.*             INMP441 (RX, I2S_NUM_0)
    i2s_speaker.*         MAX98357A (TX, I2S_NUM_1)
    wav_format.h          cabecalho WAV compartilhado (grava e reproduz)
    tts_player.*          baixa e toca o WAV de resposta (ou bipe)
  voice/
    wakeword.*            push-to-talk hoje; ponto de integracao do WakeNet
    commands.*            tabela de comandos locais + ponto de integracao do MultiNet
  network/
    wifi_manager.*        conecta no tethering do celular + SNTP
    ai_client.*           envia o audio gravado, recebe texto (+ TTS opcional)
```

## Build

Requer ESP-IDF **5.1+** (usa `esp_netif_sntp` e a API `i2s_std`). Com o
ambiente do ESP-IDF ativado:

```sh
idf.py set-target esp32s3
idf.py menuconfig   # configurar Wi-Fi, URL/API key da IA, GPIO do botao
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

> Este firmware foi escrito e revisado sem acesso a um toolchain ESP-IDF
> (ambiente sandbox sem `idf.py`), entao **nao foi compilado aqui**. Ele
> segue fielmente as APIs do ESP-IDF 5.x (i2s_std, esp_http_client,
> esp_netif_sntp, spi_master), mas faca o primeiro `idf.py build` com
> calma e me avise se algum nome de simbolo tiver mudado na sua versao
> exata do IDF.

### Ajustes quase certos no primeiro boot

- **Offset da RAM do ST7735S** (`LCD_COL_OFFSET`/`LCD_ROW_OFFSET` em
  `display/st7735.c`): paineis 0.96" 80x160 variam de lote para lote. Se
  a imagem sair cortada ou deslocada, ajuste esses dois valores (comeco
  usual: col=26/row=1, mas alguns modulos usam col=24/row=0).
- **Particao `model`** em `partitions.csv`: reservada para os modelos do
  ESP-SR. So importa quando o WakeNet/MultiNet forem de fato integrados;
  ate la fica sem uso.

## Maquina de estados / fluxo

1. **Ocioso**: relogio grande (via SNTP, sincronizado ao conectar no
   Wi-Fi), data, temperatura (placeholder - sem sensor no BOM atual) e
   icone de mic apagado.
2. **Ouvindo**: usuario segura o botao de push-to-talk; a tela mostra o
   circulo pulsante + barras de waveform (hoje derivadas de um nivel de
   amplitude simples, nao de um espectro real) enquanto grava.
3. **Respondendo**: ao soltar o botao, o audio gravado e:
   - testado contra `commands_recognize_stub()` (comandos locais) - ver
     limitacao do MultiNet abaixo;
   - se nao bateu com nenhum comando local e o Wi-Fi esta conectado,
     enviado como WAV para `CONFIG_VA_AI_API_URL`, que deve fazer
     STT + chamada ao modelo de IA e devolver
     `{ recognized_text, response_text, tts_audio_url? }`;
   - a tela mostra pergunta + resposta, e `tts_player` toca o audio (ou
     um bipe de confirmacao se nao vier `tts_audio_url`).

## Limitacoes conhecidas / o que falta para producao

- **Wake word em portugues**: a Espressif ainda nao oferece treino
  oficial de wake word customizada em portugues (so ingles, chines,
  japones e frances) nem um modelo MultiNet de comandos em portugues.
  Por isso o firmware usa **push-to-talk fisico** como mecanismo
  principal - e tambem a opcao mais segura para dirigir, já que não
  exige "gritar uma palavra em inglês pro carro". O modulo `voice/wakeword.c`
  ja deixa o ponto de integracao pronto (`VA_WAKENET_ENABLED`) para quando
  quiser adicionar uma wake word em ingles (ex. "Computer") via WakeNet.
- **MultiNet (comandos locais offline)**: `commands_recognize_stub()`
  sempre retorna "nenhum comando reconhecido" hoje - por design, para
  nao fingir uma classificacao que nao existe. Ligar isso requer
  adicionar o componente `espressif/esp-sr` (`idf.py add-dependency`),
  escolher um modelo (ingles, ja que nao ha portugues oficial) e mapear
  o indice de comando reconhecido para os `command_id_t` ja tabelados
  em `voice/commands.h`.
- **TTS 100% local**: nao existe hoje uma engine de sintese de voz em
  portugues leve o suficiente para rodar so no ESP32-S3 com qualidade
  aceitavel. O firmware usa TTS feito no backend (a mesma chamada que
  devolve `response_text` pode devolver `tts_audio_url`), o que tambem
  bate com o pedido original de "grava o audio, envia via Wi-Fi... e
  sintetiza a resposta em voz".
- **Sensor de temperatura**: nao faz parte do BOM descrito. A tela
  ociosa mostra `--°C` ate um sensor (I2C/1-Wire) ser adicionado e ligado
  em `ui_screens.c` (`read_cabin_temperature_c`).
- **Funcoes do carro** (farol, vidro, ar-condicionado): a tabela de
  comandos ja existe em `voice/commands.c`, mas os handlers avisam
  explicitamente que o acionamento fisico (rele/CAN) ainda nao foi
  ligado, em vez de fingir sucesso.
- **Seguranca ao dirigir**: recomendo manter o push-to-talk como
  interacao primaria e, se/quando adicionar wake word por voz, testar
  bastante a taxa de falsos positivos/negativos dentro do carro em
  movimento (motor, vento, radio) antes de confiar nela como unico
  gatilho.
- **TLS**: `ai_client.c` usa o cliente HTTP padrao sem
  `esp_crt_bundle_attach`; para producao, habilite a validacao do
  certificado do seu backend (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` +
  `crt_bundle_attach = esp_crt_bundle_attach` na config do
  `esp_http_client`).
- **Credenciais em texto plano**: SSID/senha/API key ficam no sdkconfig
  via Kconfig por simplicidade neste firmware inicial. Para producao,
  mova para NVS criptografada ou provisionamento via BLE/app, em vez de
  deixar no binario/sdkconfig versionado.
