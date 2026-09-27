# T-800 — Diagrama Elétrico (ESP32-C3)

Diagrama ponto-a-ponto do estado **final** do firmware (`T800_Firmware_ESP32C3.ino`):
sem display, sem PCA9685/I2C, sem LED RGB, sem PITCH, sem Wi-Fi, e com os dois
servos dos olhos ligados em paralelo no mesmo GPIO.

## Visão geral

```
                         ┌─────────────────────────┐
                         │        ESP32-C3         │
                         │                          │
   HC-SR04 (frontal)     │                          │
   ┌───────────────┐     │                          │
   │ VCC ──────────┼──── 5V (fonte dos servos, NÃO o 3V3 do ESP32)
   │ TRIG ─────────┼──── GPIO3                       │
   │ ECHO ─────────┼──┐  │                          │
   │ GND ──────────┼─┐│  │                          │
   └───────────────┘ ││  │                          │
                      ││  │                          │
        divisor 5V→3,3V│  │                          │
        ECHO──1kΩ──┬───┘  │                          │
                    2kΩ    │                          │
                    │      │                          │
                   GND ────┼──── GND comum ───────────┤
                            │    GPIO10 ◄──────────────┘ (ECHO já em 3,3V)
                            │
   LED do olho               │
   ┌─────────┐               │
   │ Ânodo ──┼──── GPIO0     │
   │ Cátodo ─┼──── resistor 100Ω ──── GND
   └─────────┘               │
                            │
   Servo YAW (cabeça, MG996R + redução coroa/pinhão 2:1)
   ┌─────────┐               │
   │ Sinal ──┼──── GPIO7     │
   │ V+ ─────┼──── 5-6,6V (fonte externa dos servos)
   │ GND ────┼──── GND comum (fonte + ESP32-C3)
   └─────────┘               │
                            │
   Servo OLHO ESQUERDO (MG90S)      Servo OLHO DIREITO (MG90S)
   ┌─────────┐               ┌─────────┐
   │ Sinal ──┼──┐         ┌──┼── Sinal │
   │ V+ ─────┼──┤         ├──┼── V+    │  → fonte externa 5-6,6V (mesma dos demais)
   │ GND ────┼──┤         ├──┼── GND   │  → GND comum
   └─────────┘  │         │  └─────────┘
                └──┬──────┘
                   │
              GPIO4 (único sinal para os DOIS servos dos olhos, em paralelo)

   GPIO5: sem uso (olho esquerdo tinha um defeito que só aparecia dentro da
   sequência completa - solução foi ligar os dois olhos no mesmo sinal, GPIO4)
   GPIO6: sem uso (era o PITCH, removido - mecanismo não sustentou o peso do
   crânio e não há motor sobressalente no momento)
```

## Tabela de pinos (ESP32-C3)

| Sinal                         | GPIO  | Observação |
|--------------------------------|-------|------------|
| Servo YAW (cabeça)              | 7     | Sinal apenas — alimentação vem da fonte externa |
| Servos OLHO ESQ. + OLHO DIR.    | 4     | **Os dois servos em paralelo no mesmo GPIO** — GND comum, alimentação da fonte externa |
| (não usado) — antigo olho esq.  | 5     | Desconectado — ver nota abaixo |
| (não usado) — antigo PITCH      | 6     | Livre, sem uso |
| HC-SR04 TRIG                    | 3     | Saída direta do ESP32 (3,3V) — o HC-SR04 aceita, sem necessidade de proteção |
| HC-SR04 ECHO                    | 10    | **Entrada** — obrigatório divisor de tensão (ver abaixo), o HC-SR04 entrega ~5V e o ESP32-C3 só aceita até ~3,3V |
| LED do olho                     | 0     | Liga/desliga (`digitalWrite`, sem PWM) — resistor limitador 100Ω em série |

## Divisor de tensão do ECHO (5V → 3,3V)

```
HC-SR04 ECHO ──── R1 (1kΩ) ──┬──── GPIO10 (ESP32-C3)
                              │
                             R2 (2kΩ)
                              │
                             GND
```
Saída ≈ 5V × 2k/(1k+2k) ≈ 3,3V — dentro do limite do GPIO do ESP32-C3.

## Alimentação — regra que não pode ser quebrada

- Os **3 servos** (YAW + os 2 dos olhos em paralelo) são alimentados por uma
  **fonte externa dedicada de 5–6,6V**, nunca pelo 3V3 do ESP32-C3 (corrente
  insuficiente e risco de dano).
- **GND comum** obrigatório entre a fonte dos servos e o GND do ESP32-C3 —
  sem GND comum, os sinais de PWM (GPIO7 e GPIO4) não têm referência e os
  servos não respondem corretamente.
- Nunca ligue o V+ dos servos a um pino de sinal ou 3V3 do ESP32 — foi esse
  tipo de erro (3,3V no V+ de uma placa driver) que já queimou um ESP32 e um
  PCA9685 anteriores no projeto.

## O que foi removido nesta versão (não faz parte do diagrama)

- PCA9685 / barramento I2C (SDA/SCL) — servos agora vão direto por GPIO
- Display TFT ST7735 (SPI: CS/DC/RST/MOSI/SCLK) e backlight
- LED RGB de status (NeoPixel)
- Sensores HC-SR04 esquerdo e direito — só o frontal permanece
- Servo de PITCH (inclinação da cabeça)
- Módulo Wi-Fi / Access Point / página de controle web
