# T-800 — Firmware (ESP32-S3-N16R8)

Firmware do busto animatrônico T-800, organizado em arquivos separados por
responsabilidade (servos, sensores, display, Wi-Fi/web, LED de status,
armazenamento em NVS).

## Estrutura (sketch `T800_Firmware/`)

| Arquivo | Responsabilidade |
|---|---|
| `config.h` | Pinout e constantes de calibração (ajustar por hardware) |
| `globals.h/.cpp` | Estado compartilhado: modo atual, posições dos servos, trims, backlight |
| `servos.h/.cpp` | PCA9685 — ângulo → pulso, aplicar posições, centralizar, desenergizar |
| `sensors.h/.cpp` | 3x HC-SR04 — leitura de distância e detecção de gatilho (<80 cm) |
| `display.h/.cpp` | TFT ST7735S — telas (STANDBY, TARGET ACQUIRED, PAIRING, REMOTE), animação de boot estilo terminal e **backlight via PWM (GPIO 18)** |
| `status_led.h/.cpp` | NeoPixel de status — pulso azul em repouso, vermelho fixo quando acionado |
| `storage.h/.cpp` | Preferences (NVS) — modo, trims de calibração e brilho do backlight sobrevivem a reset |
| `web.h/.cpp` | Access Point (`T800-CONTROL`) + servidor web de controle (sliders, calibração, brilho) |
| `T800_Firmware.ino` | `setup()` / `loop()` — orquestra os módulos acima |

## Novo nesta versão

- **Backlight do display controlado por PWM** (`PIN_TFT_BLK = GPIO 18`, via
  `ledcAttach`/`ledcWrite` — API do ESP32 Arduino Core 3.x). Nível salvo em
  NVS e ajustável pelo slider "Brilho do backlight" na página de controle.
- Firmware quebrado em arquivos por módulo em vez de um único `.ino`.

## Bibliotecas necessárias (Arduino IDE / Library Manager)

- `Adafruit PWM Servo Driver Library` (PCA9685)
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`
- `Adafruit NeoPixel`
- Core ESP32 (Espressif) ≥ 3.x — traz `WiFi`, `WebServer`, `Preferences`, `Wire`, `SPI`

## Antes de gravar

- Confira o pinout de `config.h` contra a serigrafia da sua placa —
  alguns GPIOs de S3 são *strapping pins* (0, 3, 45, 46) e já foram evitados.
- Calibre fisicamente cada servo (trim, pela interface web) antes de
  confiar nos limites de curso.
- Grave aos poucos e teste servo por servo antes de rodar a sequência completa.
