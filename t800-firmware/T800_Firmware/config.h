#pragma once
/* ============================================================================
   T-800 ANIMATRONIC BUST - mapa de pinos e constantes (ESP32-S3-N16R8)

   AVISOS IMPORTANTES (leia antes de gravar):
   - Os pinos abaixo sao um mapeamento de referencia para ESP32-S3. Confira
     contra a serigrafia da SUA placa antes de ligar qualquer coisa - alguns
     GPIOs de S3 sao "strapping pins" e nao devem ser usados aqui
     (0, 3, 45, 46) - ja evitados.
   - Angulos de servo (MIN/MAX_PULSE, offsets) sao valores de partida -
     CALIBRE fisicamente cada servo antes de confiar em qualquer limite.
   ========================================================================= */

// ---------------------------------------------------------------------------
// I2C / PCA9685
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9
#define PCA9685_ADDR     0x40

// ---------------------------------------------------------------------------
// TFT ST7735S 0.96" 80x160
// ---------------------------------------------------------------------------
#define PIN_TFT_CS       10
#define PIN_TFT_DC       11
#define PIN_TFT_RST      12
#define PIN_TFT_MOSI     13
#define PIN_TFT_SCLK     14
#define PIN_TFT_BLK      18   // backlight - PWM via LEDC (ledcAttach/ledcWrite, core 3.x)

#define LEDC_FREQ_BACKLIGHT   5000
#define LEDC_RES_BACKLIGHT    8      // 0-255
#define BACKLIGHT_DEFAULT     200    // nivel salvo na primeira gravacao

// ---------------------------------------------------------------------------
// HC-SR04 (TRIG / ECHO com divisor 10k/20k no ECHO)
// ---------------------------------------------------------------------------
#define PIN_HCSR04_FRONT_TRIG   4
#define PIN_HCSR04_FRONT_ECHO   5
#define PIN_HCSR04_LEFT_TRIG    6
#define PIN_HCSR04_LEFT_ECHO    7
#define PIN_HCSR04_RIGHT_TRIG   15
#define PIN_HCSR04_RIGHT_ECHO   16

#define DETECT_RANGE_CM   80
#define MOVE_CYCLE_MS     30000UL   // duracao do ciclo de movimento (30s)

// ---------------------------------------------------------------------------
// LED do olho (PWM) e LED RGB de status (WS2812/NeoPixel embutido na placa)
// ---------------------------------------------------------------------------
#define PIN_EYE_LED      17

// CONFIRME o GPIO do RGB onboard na serigrafia da sua placa (varia entre
// variantes de dev board S3: comum ver 48 ou 38)
#define PIN_RGB_LED      48
#define RGB_LED_COUNT    1

// ---------------------------------------------------------------------------
// Canais no PCA9685 (0-15)
// ---------------------------------------------------------------------------
#define CH_EYE_L   0
#define CH_EYE_R   1
#define CH_PITCH   2
#define CH_YAW     3

// ---------------------------------------------------------------------------
// CONSTANTES DE CALIBRACAO DE SERVO (AJUSTAR NA BANCADA)
// ---------------------------------------------------------------------------
#define SERVO_FREQ        50      // Hz, padrao p/ servos analogicos
#define SERVO_PULSE_MIN   102     // ~0.5ms em contagem de 12 bits @50Hz (SG90/MG996R tipico)
#define SERVO_PULSE_MAX   512     // ~2.5ms

// Limites de curso por eixo (graus, 0-180 = curso total do servo)
#define YAW_MIN      45
#define YAW_MAX      135
#define YAW_CENTER   90

#define PITCH_MIN    70
#define PITCH_MAX    120
#define PITCH_CENTER 95

#define EYE_L_MIN    60
#define EYE_L_MAX    120
#define EYE_L_CENTER 90

#define EYE_R_MIN    60
#define EYE_R_MAX    120
#define EYE_R_CENTER 90

// faixa de ajuste de trim permitida, em contagens de PWM (~ +-15 graus)
#define TRIM_LIMIT   80

// ---------------------------------------------------------------------------
// WI-FI (modo Access Point)
// ---------------------------------------------------------------------------
#define AP_SSID   "T800-CONTROL"
#define AP_PASS   "exterminador"   // minimo 8 caracteres
#define WEB_SERVER_PORT   80
