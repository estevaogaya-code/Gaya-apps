/* ============================================================================
   T-800 ANIMATRONIC BUST - FIRMWARE PRINCIPAL (ESP32-C3, AUTONOMO, SEM WI-FI)

   Reescrita da versao ESP32-S3 removendo:
   - PCA9685/I2C (chip suspeito de dano - servos agora comandados direto por
     GPIO via ESP32Servo, sem nenhum driver externo)
   - Display TFT ST7735 (SPI) - substituido por um OLED I2C menor (ver abaixo)
   - LED RGB de status (NeoPixel) - removido a pedido
   - Sensores HC-SR04 esquerdo e direito - mantido so o FRONTAL
   - PITCH (inclinacao da cabeca) - removido a pedido (mecanismo nao
     comportou o peso do cranio e nao ha motor sobressalente no momento).
     GPIO6 (antigo PIN_SERVO_PITCH) agora e usado pelo display (ver abaixo).
   - Interface Wi-Fi/web (Access Point + pagina de controle), modo REMOTO e
     calibracao de trim persistida em NVS - removidos a pedido. O firmware
     agora e 100% autonomo: so reage ao sensor, sem nenhuma dependencia de
     rede. Os centros/limites de cada eixo ja estao calibrados como
     constantes fixas abaixo (ver EYE_CENTER/YAW_CENTER).

   ATENCAO - fiacao atual dos olhos: fisicamente existe HOJE 1 (um) servo de
   olho por fio de sinal - os dois servos fisicos dos olhos estao ligados em
   PARALELO no MESMO pino (GPIO4) e recebem o MESMO pulso. O canal separado
   que existia antes para o olho esquerdo (GPIO5) tinha um defeito que so
   aparecia dentro da sequencia completa (travava girando ate reset); em vez
   de depurar mais, os dois servos foram fisicamente unidos no canal que
   sempre funcionou (GPIO4). Por isso o firmware agora trata "os olhos" como
   UM UNICO canal logico (IDX_EYE), nao mais dois - e o GPIO5, que ficou
   livre, foi reaproveitado para o display (ver abaixo). NUNCA ligue o GPIO5
   a outro servo de novo sem antes tirar essa funcao de SDA do display.

   Recursos mantidos:
   - 2 servos via PWM direto por GPIO (ESP32Servo): yaw (cabeca) e olhos
     (os 2 servos fisicos dos olhos, em paralelo, comandados como 1 canal).
   - 1 sensor ultrassonico HC-SR04 (frontal)
   - LED do olho, com PWM real (LEDC) em vez de liga/desliga:
       * Repouso: "respira" entre 10% e 30% de brilho, ciclo triangular de
         ida e volta em 4s (EYE_PULSE_PERIOD_MS).
       * Ao detectar alvo: rampa suave do brilho atual até 100% em 3s
         (LED_RAMP_TO_FULL_MS), permanece em 100% durante toda a coreografia,
         e volta a "respirar" quando a coreografia termina.
   - Display OLED I2C (SSD1306 128x64) na parte frontal, abaixo do sensor:
       * Repouso: scroll continuo estilo terminal (linhas tipo "0x3F2 SCAN OK"),
         uma linha nova a cada DISP_IDLE_LINE_MS.
       * Durante a coreografia (sensor disparado): mesmo estilo de scroll,
         porem bem mais rapido (DISP_BOOT_LINE_MS) - efeito de "boot" denso
         de numeros/hex, como pedido. Volta ao ritmo de repouso quando a
         coreografia termina.
   - Ao detectar alvo a menos de DETECT_RANGE_CM: dispara uma coreografia de
     4 trechos (ver updateSequence()/seqStart() abaixo):
       1) centro -> lado A
       2) lado A -> centro
       3) centro -> lado B (oposto ao lado A)
       4) lado B -> centro -> fica ocioso ate novo disparo
     Em CADA trecho: os olhos saltam abruptamente para o angulo que vai
     coincidir com o destino da cabeca (ou permanecem no centro, se o
     destino do trecho for o centro) -> pausa de 3s com a cabeca ainda na
     posicao anterior -> a cabeca entao comeca a girar em rampa (1/4 da
     velocidade de referencia dos olhos) RUMO ao destino, enquanto os olhos,
     na MESMA velocidade, giram em sentido contrario (saem do salto abrupto
     e voltam ao centro do proprio curso) -> ao fim do trecho, cabeca e
     olhos terminam alinhados (cabeca apontando pro lado, olhos centrados na
     orbita = olhar efetivamente na direcao da cabeca) -> pausa de 3s antes
     do proximo trecho. Isso substitui o comportamento antigo, em que os
     olhos ficavam "colados" no canto ate a cabeca terminar de girar,
     desalinhando o olhar durante toda a rampa da cabeca.
     Toda a sequencia e cronometrada por millis() (sem delay() no loop). Ao
     final, os servos sao desenergizados (detach) - o mecanismo sustenta a
     posicao central de repouso sem consumo continuo. Se o seu mecanismo NAO
     segurar sozinho a posicao central, remova a chamada a releaseServos()
     no fim de updateSequence().

   ATENCAO - AVISOS IMPORTANTES ANTES DE LIGAR:
   1) SERVOS: fio de sinal no GPIO do ESP32-C3, alimentacao (+V/GND) na
      FONTE EXTERNA (5-6,6V), com GND comum entre fonte e ESP32-C3. NUNCA
      ligue a alimentacao dos servos no pino 3V3 do ESP32 nem o inverso -
      foi exatamente esse erro (3,3V no V+ de uma placa driver) que queimou
      o ESP32 anterior do projeto.
   2) HC-SR04: o pino ECHO desse sensor normalmente entrega pulso em 5V.
      O ESP32-C3 so aceita ate ~3,3V (max absoluto ~3,6V) nos GPIOs de
      entrada. LIGUE O ECHO ATRAVES DE UM DIVISOR DE TENSAO (ex.: resistor
      de 1kOhm em serie + resistor de 2kOhm do pino ECHO do ESP32 para GND -
      isso reduz o pulso de ~5V para ~3,3V). Ligar o ECHO direto no GPIO,
      sem o divisor, arrisca danificar a entrada do ESP32-C3, do mesmo jeito
      que aconteceu com o servo-driver antes.
      TRIG pode ser ligado direto (e saida do ESP32, 3,3V, o HC-SR04 aceita).
   3) YAW: montado com reducao coroa/pinhao 2:1 (o eixo do pescoco gira a
      METADE do que o servo gira). Curso maximo fisico de um servo padrao e
      ~180 graus, entao o maximo no eixo de saida com essa reducao e 90 graus.
   4) DISPLAY: confirme a tensao de VCC do seu modulo OLED especifico antes
      de ligar (a maioria dos modulos SSD1306 aceita 3,3-5V, mas verifique -
      ligar 5V num modulo que so aceita 3,3V pode danifica-lo).

   BIBLIOTECAS NECESSARIAS (Library Manager):
     - ESP32Servo (Kevin Harrington / madhephaestus)
     - Adafruit GFX Library
     - Adafruit SSD1306
   ========================================================================= */

#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------------------------------------------------------------------
// PINOUT - ESP32-C3 (evitados: GPIO2/8/9 = strapping, GPIO20/21 = UART0/USB)
// ---------------------------------------------------------------------------
#define PIN_SERVO_YAW     7
#define PIN_SERVO_EYE     4   // os 2 servos fisicos dos olhos estao ligados aqui, em paralelo

#define PIN_HCSR04_TRIG   3   // saida direta do ESP32 (3,3V) - ok sem protecao
#define PIN_HCSR04_ECHO   10  // ENTRADA - usar divisor de tensao 5V->3,3V (ver aviso acima)

#define PIN_EYE_LED       0   // LED do olho (PWM via LEDC)

#define PIN_OLED_SDA      5   // antigo canal do olho esquerdo (GPIO5) - livre, ver aviso no topo
#define PIN_OLED_SCL      6   // antigo PIN_SERVO_PITCH - livre

// ---------------------------------------------------------------------------
// INDICES DOS SERVOS (usados nos arrays de posicao/limites)
// ---------------------------------------------------------------------------
#define IDX_YAW    0
#define IDX_EYE    1
#define NUM_SERVOS 2

Servo servoYaw, servoEye;
Servo* servos[NUM_SERVOS] = { &servoYaw, &servoEye };
const int servoPins[NUM_SERVOS] = { PIN_SERVO_YAW, PIN_SERVO_EYE };

// ---------------------------------------------------------------------------
// CALIBRACAO DE PULSO (microssegundos) - AJUSTAR NA BANCADA SE NECESSARIO
// ---------------------------------------------------------------------------
#define SERVO_PULSE_MIN_US   500
#define SERVO_PULSE_MAX_US   2400

// Limites de curso por eixo (graus, 0-180 = curso total do servo)
//
// YAW: reducao coroa/pinhao 2:1 (eixo do pescoco gira a METADE do servo).
// Curso do servo 0-180 (maximo absoluto) -> ~90 graus reais no pescoco.
#define YAW_MIN     0
#define YAW_MAX     180
#define YAW_CENTER  90

// Curso simetrico de +-30 graus (60 graus de liberdade) em torno do centro
// mecanico real dos olhos, medido com o sketch de teste isolado.
#define EYE_MIN     35
#define EYE_MAX     95
#define EYE_CENTER  65

// ---------------------------------------------------------------------------
// SENSOR - alcance de deteccao (cm) que dispara a sequencia de movimento
// ---------------------------------------------------------------------------
#define DETECT_RANGE_CM   80

// Coreografia disparada pelo sensor - ver descricao completa no cabecalho do
// arquivo. 4 trechos: centro->ladoA, ladoA->centro, centro->ladoB,
// ladoB->centro. Em cada trecho: olhos saltam abruptamente para o angulo de
// destino -> pausa (cabeca ainda parada) -> cabeca rampa pro destino ENQUANTO
// olhos rampam de volta ao centro, na MESMA velocidade -> pausa -> proximo
// trecho.
#define SEQ_PRE_MOVE_PAUSE_MS    3000UL  // pausa antes de a cabeca comecar a girar
#define SEQ_POST_MOVE_PAUSE_MS   3000UL  // pausa parado, ao final de cada trecho

// Velocidade angular da cabeca (YAW), em graus/segundo. Os olhos sao servos
// leves e vao no proprio limite mecanico deles ao receber um comando direto
// (~500-600 graus/s, valor tipico de datasheet de micro servo tipo MG90S sem
// carga) - isso NAO e controlado por firmware, e o quanto o servo consegue
// fisicamente nos saltos abruptos. Durante a rampa de retorno ao centro,
// porem, os olhos sao deliberadamente limitados a essa MESMA velocidade da
// cabeca (1/4 da referencia), para terminarem alinhados com ela.
#define EYE_REF_SPEED_DPS   550.0f
#define HEAD_SPEED_DPS      (EYE_REF_SPEED_DPS / 4.0f)

// ---------------------------------------------------------------------------
// LED DO OLHO - "respiracao" em repouso + rampa para 100% quando acionado
// ---------------------------------------------------------------------------
#define LED_PWM_FREQ_HZ      5000
#define LED_PWM_RES_BITS     8      // 0-255

#define EYE_PULSE_MIN_PCT    10
#define EYE_PULSE_MAX_PCT    30
#define EYE_PULSE_PERIOD_MS  4000UL  // ciclo completo de ida e volta (respiracao)

#define LED_RAMP_TO_FULL_MS  3000UL  // rampa do brilho atual até 100% ao detectar alvo

enum LedState { LED_IDLE_PULSE, LED_RAMP_TO_FULL, LED_FULL };
LedState ledState = LED_IDLE_PULSE;
unsigned long ledRampStartMs = 0;
uint8_t ledRampFromPct = EYE_PULSE_MIN_PCT;

// ---------------------------------------------------------------------------
// DISPLAY OLED (SSD1306 128x64, I2C) - scroll continuo estilo terminal
// ---------------------------------------------------------------------------
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH        128
#define OLED_HEIGHT       64

#define DISP_LINE_HEIGHT_PX  10
#define DISP_MAX_LINES       (OLED_HEIGHT / DISP_LINE_HEIGHT_PX)

#define DISP_IDLE_LINE_MS    350UL  // ritmo do scroll em repouso
#define DISP_BOOT_LINE_MS    90UL   // ritmo do scroll durante a coreografia (mais denso)

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledReady = false;

String dispLines[DISP_MAX_LINES];
int dispLineCount = 0;
unsigned long lastDispLineMs = 0;

// ---------------------------------------------------------------------------
// ESTADO GLOBAL
// ---------------------------------------------------------------------------
volatile int posYaw = YAW_CENTER;
volatile int posEye = EYE_CENTER;

// Maquina de estados da coreografia disparada pelo sensor (ver descricao no
// cabecalho do arquivo). Tudo cronometrado por millis(), sem delay() no
// loop(). 4 trechos (legIndex 0..3), cada um com seu angulo de destino para
// a cabeca e para os olhos, calculados em seqStart().
enum SeqState {
  SEQ_IDLE,
  SEQ_EYES_ABRUPT,       // olhos saltam abruptamente para o destino do trecho
  SEQ_WAIT_BEFORE_MOVE,  // pausa com a cabeca ainda parada
  SEQ_MOVING,            // cabeca rampa pro destino, olhos rampam de volta ao centro
  SEQ_WAIT_AFTER_MOVE    // pausa parado, ao final do trecho
};
SeqState seqState = SEQ_IDLE;
unsigned long seqTimerMs = 0;

#define NUM_LEGS 4
int legYaw[NUM_LEGS];
int legEye[NUM_LEGS];
int legIndex = 0;

// Rampa nao-bloqueante do YAW (cabeca), usada para limitar a velocidade dela
// a HEAD_SPEED_DPS sem travar o loop() - ver startYawRamp()/updateYawRamp().
bool yawRampActive = false;
int  yawRampFromAngle = YAW_CENTER;
int  yawRampToAngle   = YAW_CENTER;
unsigned long yawRampStartMs   = 0;
unsigned long yawRampDurationMs = 0;

// Rampa nao-bloqueante dos olhos de volta ao centro, na MESMA velocidade da
// cabeca (HEAD_SPEED_DPS) - ver startEyeRampToCenter()/updateEyeRamp().
bool eyeRampActive = false;
int  eyeRampFromAngle = EYE_CENTER;
int  eyeRampToAngle   = EYE_CENTER;
unsigned long eyeRampStartMs    = 0;
unsigned long eyeRampDurationMs = 0;

// Controla o intervalo entre checagens do sensor ultrassonico. Sem isso, o
// loop() chamaria pulseIn() (que trava ate 25ms esperando eco) em TODA
// passagem, sem pausa. Checar o sensor so a cada SENSOR_CHECK_INTERVAL_MS
// mantem o loop() responsivo entre uma leitura e outra.
unsigned long lastSensorCheckMs = 0;
#define SENSOR_CHECK_INTERVAL_MS 300UL

// ---------------------------------------------------------------------------
// SERVOS
// ---------------------------------------------------------------------------
// Centro de cada eixo (usado para inverter o sentido de giro por espelhamento
// em torno do centro, quando o servo esta montado com a orientacao oposta
// a do eixo de referencia - ver invertServo[] logo abaixo).
const int servoCenterAngle[NUM_SERVOS] = { YAW_CENTER, EYE_CENTER };

// Ponha "true" no eixo cujo sentido de giro esta oposto ao esperado. So
// inverte o SENTIDO - nao muda os limites min/max/centro do eixo. Ajuste
// aqui e regrave; nao precisa mexer em mais nada.
bool invertServo[NUM_SERVOS] = { true, false };  // { YAW, EYE }

int angleToPulseUs(uint8_t idx, int angle) {
  angle = constrain(angle, 0, 180);
  if (invertServo[idx]) angle = servoCenterAngle[idx] * 2 - angle;
  return map(angle, 0, 180, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
}

void setServoAngle(uint8_t idx, int angle) {
  // reanexa o pino se tiver sido desenergizado (detach) anteriormente
  if (!servos[idx]->attached()) {
    servos[idx]->setPeriodHertz(50);
    servos[idx]->attach(servoPins[idx], SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  }
  servos[idx]->writeMicroseconds(angleToPulseUs(idx, angle));
}

void applyServoPositions() {
  setServoAngle(IDX_YAW, constrain(posYaw, YAW_MIN, YAW_MAX));
  setServoAngle(IDX_EYE, constrain(posEye, EYE_MIN, EYE_MAX));
}

void servosToCenter() {
  posYaw = YAW_CENTER;
  posEye = EYE_CENTER;
  applyServoPositions();
}

// ---------------------------------------------------------------------------
// LED DO OLHO - ver enum LedState acima
// ---------------------------------------------------------------------------
uint8_t computeIdlePulsePct(unsigned long now) {
  unsigned long phase = now % EYE_PULSE_PERIOD_MS;
  unsigned long half  = EYE_PULSE_PERIOD_MS / 2;
  if (phase < half) {
    return (uint8_t)map(phase, 0, half, EYE_PULSE_MIN_PCT, EYE_PULSE_MAX_PCT);
  }
  return (uint8_t)map(phase - half, 0, half, EYE_PULSE_MAX_PCT, EYE_PULSE_MIN_PCT);
}

void ledStartRampToFull() {
  ledRampFromPct = computeIdlePulsePct(millis());
  ledRampStartMs = millis();
  ledState = LED_RAMP_TO_FULL;
}

void ledReturnToIdlePulse() {
  ledState = LED_IDLE_PULSE;
}

// Chamar em toda passagem do loop(), independente do estado da coreografia.
void updateEyeLed() {
  unsigned long now = millis();
  uint8_t pct;
  if (ledState == LED_RAMP_TO_FULL) {
    unsigned long elapsed = now - ledRampStartMs;
    if (elapsed >= LED_RAMP_TO_FULL_MS) {
      pct = 100;
      ledState = LED_FULL;
    } else {
      pct = (uint8_t)map(elapsed, 0, LED_RAMP_TO_FULL_MS, ledRampFromPct, 100);
    }
  } else if (ledState == LED_FULL) {
    pct = 100;
  } else {
    pct = computeIdlePulsePct(now);
  }
  ledcWrite(PIN_EYE_LED, (uint8_t)((uint16_t)pct * 255 / 100));
}

// ---------------------------------------------------------------------------
// DISPLAY OLED - scroll continuo estilo terminal
// ---------------------------------------------------------------------------
const char* BOOT_WORDS[] = {
  "INIT", "BOOT", "SCAN", "SYS", "MEM", "SERVO", "PWM",
  "EYE", "LOAD", "ACT", "CAL", "SENSOR", "YAW", "SYNC"
};
const int BOOT_WORDS_COUNT = sizeof(BOOT_WORDS) / sizeof(BOOT_WORDS[0]);

String randomHex(int digits) {
  String s = "0x";
  const char* hexChars = "0123456789ABCDEF";
  for (int i = 0; i < digits; i++) s += hexChars[random(0, 16)];
  return s;
}

String randomBootLine() {
  String w = BOOT_WORDS[random(0, BOOT_WORDS_COUNT)];
  String status = (random(0, 100) > 15) ? "OK" : "ERR";
  return randomHex(3) + " " + w + " " + status;
}

// Empilha uma linha nova no topo da tela e redesenha (rola as linhas antigas
// pra cima quando a tela enche). Sem efeito se o OLED nao respondeu no begin().
void dispPushLine(const String& line) {
  if (!oledReady) return;

  if (dispLineCount < DISP_MAX_LINES) {
    dispLines[dispLineCount++] = line;
  } else {
    for (int i = 1; i < DISP_MAX_LINES; i++) dispLines[i - 1] = dispLines[i];
    dispLines[DISP_MAX_LINES - 1] = line;
  }

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  for (int i = 0; i < dispLineCount; i++) {
    oled.setCursor(0, i * DISP_LINE_HEIGHT_PX);
    oled.print(dispLines[i]);
  }
  oled.display();
}

// Chamar em toda passagem do loop(). O ritmo do scroll depende do estado da
// coreografia: lento em repouso, bem mais rapido ("boot") durante o movimento.
void updateDisplay() {
  if (!oledReady) return;
  unsigned long now = millis();
  unsigned long interval = (seqState == SEQ_IDLE) ? DISP_IDLE_LINE_MS : DISP_BOOT_LINE_MS;
  if (now - lastDispLineMs >= interval) {
    lastDispLineMs = now;
    dispPushLine(randomBootLine());
  }
}

// ---------------------------------------------------------------------------
// COREOGRAFIA DE MOVIMENTO (disparada pelo sensor) - ver enum SeqState acima
// ---------------------------------------------------------------------------

// Inicia uma rampa nao-bloqueante do YAW ate targetAngle, na velocidade
// HEAD_SPEED_DPS. Chamar updateYawRamp() em toda passagem do loop() ate ela
// retornar true (rampa concluida).
void startYawRamp(int targetAngle) {
  yawRampFromAngle = posYaw;
  yawRampToAngle   = constrain(targetAngle, YAW_MIN, YAW_MAX);
  int deltaDeg = abs(yawRampToAngle - yawRampFromAngle);
  yawRampDurationMs = (unsigned long)((deltaDeg / HEAD_SPEED_DPS) * 1000.0f);
  if (yawRampDurationMs < 1) yawRampDurationMs = 1; // evita divisao por zero se ja estiver no alvo
  yawRampStartMs = millis();
  yawRampActive = true;
}

// Retorna true quando a rampa termina (e ja deixa o YAW exatamente no alvo).
bool updateYawRamp() {
  if (!yawRampActive) return true;
  unsigned long elapsed = millis() - yawRampStartMs;
  if (elapsed >= yawRampDurationMs) {
    posYaw = yawRampToAngle;
    setServoAngle(IDX_YAW, posYaw);
    yawRampActive = false;
    return true;
  }
  float frac = (float)elapsed / (float)yawRampDurationMs;
  posYaw = yawRampFromAngle + (int)((yawRampToAngle - yawRampFromAngle) * frac);
  setServoAngle(IDX_YAW, posYaw);
  return false;
}

// Inicia uma rampa nao-bloqueante dos olhos de volta ao proprio centro, na
// mesma velocidade angular da cabeca (HEAD_SPEED_DPS) - e o que faz os olhos
// "destorcerem" da posicao abrupta exatamente enquanto a cabeca gira,
// terminando alinhados com ela.
void startEyeRampToCenter() {
  eyeRampFromAngle = posEye;
  eyeRampToAngle   = EYE_CENTER;
  int deltaDeg = abs(eyeRampToAngle - eyeRampFromAngle);
  eyeRampDurationMs = (unsigned long)((deltaDeg / HEAD_SPEED_DPS) * 1000.0f);
  if (eyeRampDurationMs < 1) eyeRampDurationMs = 1;
  eyeRampStartMs = millis();
  eyeRampActive = true;
}

// Retorna true quando a rampa termina (e ja deixa os olhos exatamente no centro).
bool updateEyeRamp() {
  if (!eyeRampActive) return true;
  unsigned long elapsed = millis() - eyeRampStartMs;
  if (elapsed >= eyeRampDurationMs) {
    posEye = eyeRampToAngle;
    setServoAngle(IDX_EYE, posEye);
    eyeRampActive = false;
    return true;
  }
  float frac = (float)elapsed / (float)eyeRampDurationMs;
  posEye = eyeRampFromAngle + (int)((eyeRampToAngle - eyeRampFromAngle) * frac);
  setServoAngle(IDX_EYE, posEye);
  return false;
}

// Monta os 4 trechos da coreografia (centro->ladoA, ladoA->centro,
// centro->ladoB, ladoB->centro), mantendo as mesmas direcoes/limites ja
// usados na coreografia anterior - so a forma de percorrer cada trecho mudou.
void seqStart() {
  int dir = 1;  // lado inicial - mesma convencao de sempre (YAW_MAX/EYE_MAX primeiro)

  legYaw[0] = (dir > 0) ? YAW_MAX : YAW_MIN;
  legEye[0] = (dir > 0) ? EYE_MAX : EYE_MIN;

  legYaw[1] = YAW_CENTER;
  legEye[1] = EYE_CENTER;

  legYaw[2] = (dir > 0) ? YAW_MIN : YAW_MAX;
  legEye[2] = (dir > 0) ? EYE_MIN : EYE_MAX;

  legYaw[3] = YAW_CENTER;
  legEye[3] = EYE_CENTER;

  legIndex = 0;
  seqState = SEQ_EYES_ABRUPT;
  ledStartRampToFull();
  Serial.println("Alvo detectado - iniciando sequencia de movimento");
}

// Chamada em toda passagem do loop() enquanto seqState != SEQ_IDLE. So faz
// alguma coisa quando o tempo da pausa atual termina, ou quando as rampas em
// andamento terminam - nunca bloqueia.
void updateSequence() {
  unsigned long now = millis();
  switch (seqState) {

    case SEQ_EYES_ABRUPT:
      // olhos saltam direto para o angulo que vai coincidir com o destino
      // deste trecho (ou ficam no centro, se o destino for o centro).
      posEye = legEye[legIndex];
      setServoAngle(IDX_EYE, posEye);
      seqTimerMs = now;
      seqState = SEQ_WAIT_BEFORE_MOVE;
      break;

    case SEQ_WAIT_BEFORE_MOVE:
      if (now - seqTimerMs >= SEQ_PRE_MOVE_PAUSE_MS) {
        // cabeca comeca a rampa pro destino do trecho, e os olhos comecam a
        // rampa de volta ao proprio centro, ao MESMO TEMPO e na MESMA
        // velocidade - e isso que mantem olhos e cabeca alinhados durante
        // todo o giro, em vez de ficarem "colados" no canto.
        startYawRamp(legYaw[legIndex]);
        startEyeRampToCenter();
        seqState = SEQ_MOVING;
      }
      break;

    case SEQ_MOVING: {
      bool yawDone  = updateYawRamp();
      bool eyeDone  = updateEyeRamp();
      if (yawDone && eyeDone) {
        seqTimerMs = now;
        seqState = SEQ_WAIT_AFTER_MOVE;
      }
      break;
    }

    case SEQ_WAIT_AFTER_MOVE:
      if (now - seqTimerMs >= SEQ_POST_MOVE_PAUSE_MS) {
        legIndex++;
        if (legIndex < NUM_LEGS) {
          seqState = SEQ_EYES_ABRUPT;
        } else {
          // os 4 trechos concluidos - encerra e aguarda novo disparo
          ledReturnToIdlePulse();
          releaseServos();
          seqState = SEQ_IDLE;
          Serial.println("Sequencia concluida - aguardando novo disparo do sensor");
        }
      }
      break;

    case SEQ_IDLE:
    default:
      break;
  }
}

// Desanexa o PWM dos servos (equivalente ao antigo "full-off" do PCA9685).
// O mecanismo (peso/atrito/geometria) sustenta a posicao de repouso sem o
// servo energizado - evita corrente de sustentacao continua parado.
// Uma nova chamada a applyServoPositions()/setServoAngle() reanexa sozinha.
void releaseServos() {
  for (int i = 0; i < NUM_SERVOS; i++) servos[i]->detach();
}

// ---------------------------------------------------------------------------
// SENSOR HC-SR04 (somente frontal)
// ---------------------------------------------------------------------------
long readDistanceCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000UL); // timeout 25ms (~4m)
  if (duration == 0) return -1; // sem eco / fora de alcance
  return duration / 58; // us -> cm
}

bool frontSensorTriggered() {
  long d = readDistanceCm(PIN_HCSR04_TRIG, PIN_HCSR04_ECHO);
  return (d > 0 && d < DETECT_RANGE_CM);
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== T-800 FIRMWARE - ESP32-C3 (autonomo, sem Wi-Fi) ===");

  // --- sensor frontal ---
  pinMode(PIN_HCSR04_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_ECHO, INPUT);

  // --- LED do olho (PWM via LEDC - canal proprio, independente dos servos) ---
  ledcAttach(PIN_EYE_LED, LED_PWM_FREQ_HZ, LED_PWM_RES_BITS);
  updateEyeLed();   // ja aplica o primeiro valor da "respiracao" de repouso

  // --- display OLED (I2C) ---
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR);
  if (oledReady) {
    oled.clearDisplay();
    oled.display();
    Serial.println("Display OLED inicializado.");
  } else {
    Serial.println("AVISO: display OLED nao respondeu no endereco I2C - seguindo sem ele.");
  }

  // --- servos: anexa e ja escreve a posicao central correta na mesma
  //     iteracao, servo por servo - sem gap entre attach() e o pulso certo
  //     (a biblioteca ESP32Servo aplica um pulso padrao, perto do minimo,
  //     assim que attach() e chamado, antes de qualquer comando explicito) ---
  const int centerAngle[NUM_SERVOS] = { YAW_CENTER, EYE_CENTER };
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i]->setPeriodHertz(50);
    servos[i]->attach(servoPins[i], SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
    servos[i]->writeMicroseconds(angleToPulseUs(i, centerAngle[i]));
  }
  posYaw = YAW_CENTER; posEye = EYE_CENTER;

  delay(300);       // tempo para os servos assentarem fisicamente no centro
  releaseServos();  // desenergiza - repouso sustentado mecanicamente

  Serial.println("Pronto - aguardando disparo do sensor frontal.");
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
  updateEyeLed();   // roda sempre - respiracao em repouso ou rampa/brilho total em atividade
  updateDisplay();  // roda sempre - scroll lento em repouso ou rapido durante a coreografia

  if (seqState == SEQ_IDLE) {
    // So chama pulseIn() a cada SENSOR_CHECK_INTERVAL_MS.
    if (millis() - lastSensorCheckMs >= SENSOR_CHECK_INTERVAL_MS) {
      lastSensorCheckMs = millis();
      if (frontSensorTriggered()) {
        seqStart();
      }
    }
  } else {
    updateSequence();   // maquina de estados nao-bloqueante - ver definicao acima
  }
}
