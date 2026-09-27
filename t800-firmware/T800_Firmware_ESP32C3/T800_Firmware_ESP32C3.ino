/* ============================================================================
   T-800 ANIMATRONIC BUST - FIRMWARE PRINCIPAL (ESP32-C3, AUTONOMO, SEM WI-FI)

   Reescrita da versao ESP32-S3 removendo:
   - PCA9685/I2C (chip suspeito de dano - servos agora comandados direto por
     GPIO via ESP32Servo, sem nenhum driver externo)
   - Display TFT ST7735 (SPI) e todo o feedback visual na propria peca
   - LED RGB de status (NeoPixel) - removido a pedido
   - Sensores HC-SR04 esquerdo e direito - mantido so o FRONTAL
   - PITCH (inclinacao da cabeca) - removido a pedido (mecanismo nao
     comportou o peso do cranio e nao ha motor sobressalente no momento).
   - Interface Wi-Fi/web (Access Point + pagina de controle), modo REMOTO e
     calibracao de trim persistida em NVS - removidos a pedido. O firmware
     agora e 100% autonomo: so reage ao sensor, sem nenhuma dependencia de
     rede. Os centros/limites de cada eixo ja estao calibrados como
     constantes fixas abaixo (ver EYE_L_CENTER/EYE_R_CENTER/YAW_CENTER).

   ATENCAO - fiacao atual dos olhos: os dois servos dos olhos estao ligados
   em paralelo no MESMO pino (GPIO4/canal IDX_EYE_R) - o servo que ficava no
   GPIO5 (IDX_EYE_L) tinha um defeito que so aparecia dentro da sequencia
   completa (travava girando at reset). Fisicamente so o GPIO4 esta conectado
   agora; o firmware ainda calcula IDX_EYE_L internamente mas isso e
   inofensivo (nada escuta no GPIO5). So a calibracao de EYE_R (centro 65,
   curso 35-95) esta de fato comandando os dois olhos na pratica.

   Recursos mantidos:
   - 3 servos via PWM direto por GPIO (ESP32Servo): yaw (cabeca), olho
     esquerdo, olho direito.
   - 1 sensor ultrassonico HC-SR04 (frontal)
   - LED do olho (liga/desliga, sem PWM - ver aviso no digitalWrite abaixo)
   - Ao detectar alvo a menos de DETECT_RANGE_CM: dispara uma coreografia
     (olhos vao abruptamente para um lado -> pausa -> cabeca vai para o
     mesmo lado ate completar o curso -> pausa -> cabeca e olhos voltam
     juntos ao centro -> pausa -> repete para o lado oposto -> fica ocioso
     ate novo disparo). Toda a sequencia e cronometrada por millis() (sem
     delay() no loop). Ao final, os servos sao desenergizados (detach) - o
     mecanismo sustenta a posicao central de repouso sem consumo continuo.
     Se o seu mecanismo NAO segurar sozinho a posicao central, remova a
     chamada a releaseServos() no fim de updateSequence().

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

   BIBLIOTECA NECESSARIA (Library Manager):
     - ESP32Servo (Kevin Harrington / madhephaestus)
   ========================================================================= */

#include <ESP32Servo.h>

// ---------------------------------------------------------------------------
// PINOUT - ESP32-C3 (evitados: GPIO2/8/9 = strapping, GPIO20/21 = UART0/USB)
// ---------------------------------------------------------------------------
#define PIN_SERVO_YAW     7
#define PIN_SERVO_EYE_L   5   // sem uso fisico atualmente - ver aviso no topo do arquivo
#define PIN_SERVO_EYE_R   4   // os 2 servos dos olhos estao ligados aqui, em paralelo

#define PIN_HCSR04_TRIG   3   // saida direta do ESP32 (3,3V) - ok sem protecao
#define PIN_HCSR04_ECHO   10  // ENTRADA - usar divisor de tensao 5V->3,3V (ver aviso acima)

#define PIN_EYE_LED       0   // LED do olho (liga/desliga)

// GPIO 6 (antigo PIN_SERVO_PITCH) fica livre, sem uso.

// ---------------------------------------------------------------------------
// INDICES DOS SERVOS (usados nos arrays de posicao/limites)
// ---------------------------------------------------------------------------
#define IDX_YAW    0
#define IDX_EYE_L  1
#define IDX_EYE_R  2
#define NUM_SERVOS 3

Servo servoYaw, servoEyeL, servoEyeR;
Servo* servos[NUM_SERVOS] = { &servoYaw, &servoEyeL, &servoEyeR };
const int servoPins[NUM_SERVOS] = { PIN_SERVO_YAW, PIN_SERVO_EYE_L, PIN_SERVO_EYE_R };

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
// mecanico real de cada olho, medido com o sketch de teste isolado.
#define EYE_L_MIN    32
#define EYE_L_MAX    92
#define EYE_L_CENTER 62

#define EYE_R_MIN    35
#define EYE_R_MAX    95
#define EYE_R_CENTER 65

// ---------------------------------------------------------------------------
// SENSOR - alcance de deteccao (cm) que dispara a sequencia de movimento
// ---------------------------------------------------------------------------
#define DETECT_RANGE_CM   80

// Coreografia disparada pelo sensor (nao-bloqueante, controlada por millis()):
//   1) olhos vao abruptamente para um lado
//   2) pausa
//   3) cabeca (yaw) vai para o MESMO lado ate completar o curso
//   4) pausa (parado no extremo)
//   5) cabeca e olhos voltam JUNTOS ao centro
//   6) pausa (parado no centro)
//   7) repete o mesmo ciclo (1-6) para o lado OPOSTO
//   8) ao voltar ao centro pela 2a vez, fica ocioso ate novo disparo do sensor
#define SEQ_EYE_TO_HEAD_PAUSE_MS   2000UL  // pausa entre olhos e cabeca
#define SEQ_HEAD_HOLD_PAUSE_MS     3000UL  // pausa parado no extremo
#define SEQ_CENTER_PAUSE_MS        3000UL  // pausa parado no centro

// Pequena pausa tecnica (nao e parte da coreografia) entre comandar os olhos
// de volta ao centro e iniciar a rampa do YAW - evita comandar os 2 olhos e
// o YAW praticamente no mesmo instante, no caso de isso pesar na fonte
// compartilhada dos 3 servos.
#define SEQ_RETURN_STAGGER_MS      250UL

// Velocidade angular da cabeca (YAW), em graus/segundo. Os olhos sao servos
// leves e vao no proprio limite mecanico deles ao receber o comando (~500-600
// graus/s, valor tipico de datasheet de micro servo tipo MG90S sem carga) -
// isso NAO e controlado por firmware, e o quanto o servo consegue fisicamente.
// A cabeca (YAW) e comandada em rampa por software para ficar em 1/4 dessa
// velocidade de referencia. Se na pratica os olhos parecerem mais rapidos ou
// mais lentos que isso, ajuste EYE_REF_SPEED_DPS e HEAD_SPEED_DPS recalcula
// sozinho a partir dele.
#define EYE_REF_SPEED_DPS   550.0f
#define HEAD_SPEED_DPS      (EYE_REF_SPEED_DPS / 4.0f)

// ---------------------------------------------------------------------------
// ESTADO GLOBAL
// ---------------------------------------------------------------------------
volatile int posYaw   = YAW_CENTER;
volatile int posEyeL  = EYE_L_CENTER;
volatile int posEyeR  = EYE_R_CENTER;

// Maquina de estados da coreografia disparada pelo sensor (ver comentario
// acima de SEQ_EYE_TO_HEAD_PAUSE_MS). Tudo cronometrado por millis(), sem
// nenhum delay() dentro do loop().
enum SeqState {
  SEQ_IDLE,
  SEQ_EYES_TO_SIDE,
  SEQ_WAIT_BEFORE_HEAD,
  SEQ_HEAD_TO_SIDE,
  SEQ_HEAD_MOVING_TO_SIDE,
  SEQ_WAIT_AT_EXTREME,
  SEQ_RETURN_CENTER,
  SEQ_WAIT_BEFORE_HEAD_RETURN,
  SEQ_HEAD_MOVING_TO_CENTER,
  SEQ_WAIT_AT_CENTER
};
SeqState seqState = SEQ_IDLE;
unsigned long seqTimerMs = 0;
int  seqDir  = 1;   // +1 ou -1: para qual lado vai nesta passada
int  seqPass = 0;   // 0 = primeira direcao, 1 = segunda direcao (oposta)

// Rampa nao-bloqueante do YAW (cabeca), usada para limitar a velocidade dela
// a HEAD_SPEED_DPS sem travar o loop() - ver startYawRamp()/updateYawRamp().
bool yawRampActive = false;
int  yawRampFromAngle = YAW_CENTER;
int  yawRampToAngle   = YAW_CENTER;
unsigned long yawRampStartMs   = 0;
unsigned long yawRampDurationMs = 0;

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
const int servoCenterAngle[NUM_SERVOS] = { YAW_CENTER, EYE_L_CENTER, EYE_R_CENTER };

// Ponha "true" no eixo cujo sentido de giro esta oposto ao esperado. So
// inverte o SENTIDO - nao muda os limites min/max/centro do eixo. Ajuste
// aqui e regrave; nao precisa mexer em mais nada.
bool invertServo[NUM_SERVOS] = { true, false, false };  // { YAW, EYE_L, EYE_R }

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
  setServoAngle(IDX_YAW,   constrain(posYaw,   YAW_MIN,   YAW_MAX));
  setServoAngle(IDX_EYE_L, constrain(posEyeL,  EYE_L_MIN, EYE_L_MAX));
  setServoAngle(IDX_EYE_R, constrain(posEyeR,  EYE_R_MIN, EYE_R_MAX));
}

void servosToCenter() {
  posYaw = YAW_CENTER;
  posEyeL = EYE_L_CENTER;
  posEyeR = EYE_R_CENTER;
  applyServoPositions();
}

// ---------------------------------------------------------------------------
// COREOGRAFIA DE MOVIMENTO (disparada pelo sensor) - ver enum SeqState acima
// ---------------------------------------------------------------------------
void seqSetEyesToSide(int dir) {
  posEyeL = (dir > 0) ? EYE_L_MAX : EYE_L_MIN;
  posEyeR = (dir > 0) ? EYE_R_MAX : EYE_R_MIN;
  setServoAngle(IDX_EYE_L, posEyeL);
  setServoAngle(IDX_EYE_R, posEyeR);
}

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

void seqStart() {
  seqDir  = 1;
  seqPass = 0;
  seqState = SEQ_EYES_TO_SIDE;
  Serial.println("Alvo detectado - iniciando sequencia de movimento");
}

// Chamada em toda passagem do loop() enquanto seqState != SEQ_IDLE. So faz
// alguma coisa quando o tempo da pausa atual termina - nunca bloqueia.
void updateSequence() {
  unsigned long now = millis();
  switch (seqState) {

    case SEQ_EYES_TO_SIDE:
      seqSetEyesToSide(seqDir);
      digitalWrite(PIN_EYE_LED, HIGH);
      seqTimerMs = now;
      seqState = SEQ_WAIT_BEFORE_HEAD;
      break;

    case SEQ_WAIT_BEFORE_HEAD:
      if (now - seqTimerMs >= SEQ_EYE_TO_HEAD_PAUSE_MS) {
        seqState = SEQ_HEAD_TO_SIDE;
      }
      break;

    case SEQ_HEAD_TO_SIDE:
      // olhos ja estao no lado (feito em SEQ_EYES_TO_SIDE); a cabeca agora
      // faz o mesmo percurso, mas em rampa, a 1/4 da velocidade dos olhos.
      startYawRamp((seqDir > 0) ? YAW_MAX : YAW_MIN);
      seqState = SEQ_HEAD_MOVING_TO_SIDE;
      break;

    case SEQ_HEAD_MOVING_TO_SIDE:
      if (updateYawRamp()) {
        seqTimerMs = now;
        seqState = SEQ_WAIT_AT_EXTREME;
      }
      break;

    case SEQ_WAIT_AT_EXTREME:
      if (now - seqTimerMs >= SEQ_HEAD_HOLD_PAUSE_MS) {
        seqState = SEQ_RETURN_CENTER;
      }
      break;

    case SEQ_RETURN_CENTER:
      // olhos voltam ao centro na hora (mesmo criterio de "abruptamente").
      posEyeL = EYE_L_CENTER;
      posEyeR = EYE_R_CENTER;
      setServoAngle(IDX_EYE_L, posEyeL);
      setServoAngle(IDX_EYE_R, posEyeR);
      seqTimerMs = now;
      seqState = SEQ_WAIT_BEFORE_HEAD_RETURN;
      break;

    case SEQ_WAIT_BEFORE_HEAD_RETURN:
      // pequena pausa tecnica antes de iniciar a rampa do YAW (ver
      // SEQ_RETURN_STAGGER_MS acima) - so entao a cabeca comeca a rampa de
      // volta ao centro, tambem a 1/4 da velocidade dos olhos.
      if (now - seqTimerMs >= SEQ_RETURN_STAGGER_MS) {
        startYawRamp(YAW_CENTER);
        seqState = SEQ_HEAD_MOVING_TO_CENTER;
      }
      break;

    case SEQ_HEAD_MOVING_TO_CENTER:
      if (updateYawRamp()) {
        seqTimerMs = now;
        seqState = SEQ_WAIT_AT_CENTER;
      }
      break;

    case SEQ_WAIT_AT_CENTER:
      if (now - seqTimerMs >= SEQ_CENTER_PAUSE_MS) {
        if (seqPass == 0) {
          // repete o mesmo ciclo para o lado oposto
          seqPass = 1;
          seqDir  = -seqDir;
          seqState = SEQ_EYES_TO_SIDE;
        } else {
          // as duas direcoes concluidas - encerra e aguarda novo disparo
          digitalWrite(PIN_EYE_LED, LOW);
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

  // --- LED do olho (so usa liga/desliga - digitalWrite direto, sem passar
  //     pelo periferico LEDC, que os 3 servos tambem usam) ---
  pinMode(PIN_EYE_LED, OUTPUT);
  digitalWrite(PIN_EYE_LED, LOW);

  // --- servos: anexa e ja escreve a posicao central correta na mesma
  //     iteracao, servo por servo - sem gap entre attach() e o pulso certo
  //     (a biblioteca ESP32Servo aplica um pulso padrao, perto do minimo,
  //     assim que attach() e chamado, antes de qualquer comando explicito) ---
  const int centerAngle[NUM_SERVOS] = { YAW_CENTER, EYE_L_CENTER, EYE_R_CENTER };
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i]->setPeriodHertz(50);
    servos[i]->attach(servoPins[i], SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
    servos[i]->writeMicroseconds(angleToPulseUs(i, centerAngle[i]));
  }
  posYaw = YAW_CENTER; posEyeL = EYE_L_CENTER; posEyeR = EYE_R_CENTER;

  delay(300);       // tempo para os servos assentarem fisicamente no centro
  releaseServos();  // desenergiza - repouso sustentado mecanicamente

  Serial.println("Pronto - aguardando disparo do sensor frontal.");
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
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
