/* ============================================================================
   T-800 ANIMATRONIC BUST - FIRMWARE PRINCIPAL (arquivo unico)
   ESP32-S3-N16R8

   Versao de arquivo unico, gerada a partir da versao modular
   (t800-firmware/T800_Firmware/*.h/*.cpp) para facilitar o carregamento
   direto no Arduino IDE quando so um .ino e aceito/desejado. O codigo e
   o mesmo; so foi reunido num arquivo so.

   Recursos:
   - 4 servos via PCA9685 (I2C): olho esquerdo, olho direito, pitch (cima/baixo),
     yaw (esquerda/direita)
   - 3 sensores ultrassonicos HC-SR04 (frente, esquerda, direita)
   - Display TFT 80x160 (driver ST7735), paisagem, boot em estilo terminal (verde),
     backlight controlado via PWM (GPIO 18)
   - LED do olho (PWM)
   - Modo Wi-Fi: ESP32 sobe como Access Point + pagina web de controle
     (sliders de movimento, calibracao de trim, brilho do backlight)
   - Alternancia de modo: SENSOR (automatico) x REMOTO (comandado pelo celular)
   - Estado salvo em NVS (Preferences): modo, trims e brilho do backlight -
     sobrevive a reset
   - Modo SENSOR: apos o ciclo de 30s, os servos voltam ao centro e o PWM e
     cortado (releaseServos) - assume que a mecanica (peso/atrito/geometria)
     sustenta a posicao de repouso sem o servo energizado. Se o mecanismo do
     seu busto NAO segurar sozinho a posicao central, o eixo pode "cair" com
     o PWM desligado - nesse caso, remova a chamada a releaseServos().

   AVISOS IMPORTANTES (leia antes de gravar):
   - Os pinos abaixo sao um mapeamento de referencia para ESP32-S3. Confira
     contra a serigrafia da SUA placa antes de ligar qualquer coisa - alguns
     GPIOs de S3 sao "strapping pins" e nao devem ser usados aqui
     (0, 3, 45, 46) - ja evitei esses.
   - Angulos de servo (MIN/MAX_PULSE, offsets) sao valores de partida - CALIBRE
     fisicamente cada servo antes de confiar em qualquer limite de curso.
   - Testado apenas por compilacao mental / revisao de logica - grave aos poucos
     e teste servo por servo antes de rodar a sequencia completa.
   ========================================================================= */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h>

// ---------------------------------------------------------------------------
// PINOUT - AJUSTE CONFORME SUA PLACA
// ---------------------------------------------------------------------------
#define PIN_I2C_SDA      8
#define PIN_I2C_SCL      9

#define PIN_TFT_CS       10
#define PIN_TFT_DC       11
#define PIN_TFT_RST      12
#define PIN_TFT_MOSI     13
#define PIN_TFT_SCLK     14
#define PIN_TFT_BLK      18   // backlight - PWM via LEDC (ledcAttach/ledcWrite, core 3.x)

#define LEDC_FREQ_BACKLIGHT   5000
#define LEDC_RES_BACKLIGHT    8      // 0-255
#define BACKLIGHT_DEFAULT     200    // nivel usado na primeira gravacao (sem NVS salva ainda)

#define PIN_HCSR04_FRONT_TRIG   4
#define PIN_HCSR04_FRONT_ECHO   5
#define PIN_HCSR04_LEFT_TRIG    6
#define PIN_HCSR04_LEFT_ECHO    7
#define PIN_HCSR04_RIGHT_TRIG   15
#define PIN_HCSR04_RIGHT_ECHO   16

#define PIN_EYE_LED      17   // LED do olho (PWM, 0-255)

// LED RGB embutido na placa (WS2812/NeoPixel) - CONFIRME o GPIO na serigrafia
// da sua placa (varia entre variantes de dev board S3: comum ver 48 ou 38)
#define PIN_RGB_LED      48
#define RGB_LED_COUNT    1

// Canais no PCA9685 (0-15)
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
#define YAW_MIN     45
#define YAW_MAX     135
#define YAW_CENTER  90

#define PITCH_MIN   70
#define PITCH_MAX   120
#define PITCH_CENTER 95

#define EYE_L_MIN   60
#define EYE_L_MAX   120
#define EYE_L_CENTER 90

#define EYE_R_MIN   60
#define EYE_R_MAX   120
#define EYE_R_CENTER 90

// ---------------------------------------------------------------------------
// SENSOR - alcance de deteccao (cm) que dispara o ciclo de movimento
// ---------------------------------------------------------------------------
#define DETECT_RANGE_CM   80
#define MOVE_CYCLE_MS     30000UL   // duracao do ciclo de movimento (30s)

// ---------------------------------------------------------------------------
// WI-FI (modo Access Point)
// ---------------------------------------------------------------------------
const char* AP_SSID = "T800-CONTROL";
const char* AP_PASS = "exterminador";   // minimo 8 caracteres

// ---------------------------------------------------------------------------
// OBJETOS GLOBAIS
// ---------------------------------------------------------------------------
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
Adafruit_ST7735 tft = Adafruit_ST7735(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
WebServer server(80);
Preferences prefs;
Adafruit_NeoPixel rgbLed(RGB_LED_COUNT, PIN_RGB_LED, NEO_GRB + NEO_KHZ800);

enum ControlMode { MODE_SENSOR = 0, MODE_REMOTE = 1 };
ControlMode currentMode = MODE_SENSOR;

// posicoes atuais (graus) - usadas tanto pelo modo remoto quanto pelo sensor
volatile int posYaw   = YAW_CENTER;
volatile int posPitch = PITCH_CENTER;
volatile int posEyeL  = EYE_L_CENTER;
volatile int posEyeR  = EYE_R_CENTER;

bool movementActive = false;
unsigned long movementStartMs = 0;

// Trim de calibracao do centro/zero de cada servo, em contagens de PWM (12 bits).
// Indexado pelo numero do canal (CH_EYE_L=0, CH_EYE_R=1, CH_PITCH=2, CH_YAW=3).
// Persistido em NVS (Preferences) - sobrevive a reset. Ajustavel pela interface web.
int trimPulse[4] = {0, 0, 0, 0};
#define TRIM_LIMIT 80   // faixa de ajuste permitida, em contagens (~ +-15 graus)

// Nivel de backlight do display (0-255), persistido em NVS.
uint8_t backlightLevel = BACKLIGHT_DEFAULT;

// ---------------------------------------------------------------------------
// LED RGB DE STATUS - azul "batimento cardiaco" em espera, vermelho fixo
// quando um estimulo externo (sensor) dispara o ciclo de movimento
// ---------------------------------------------------------------------------
uint8_t heartbeatBrightness(unsigned long t) {
  unsigned long p = t % 1000UL;           // ciclo de 1s
  if (p < 80)   return map(p, 0, 80, 0, 255);          // "lub" subindo
  if (p < 160)  return map(p, 80, 160, 255, 60);        // "lub" descendo
  if (p < 220)  return map(p, 160, 220, 60, 255);       // "dub" subindo
  if (p < 400)  return map(p, 220, 400, 255, 0);        // "dub" descendo
  return 0;                                              // pausa entre batidas
}

void updateStatusLed() {
  if (movementActive) {
    rgbLed.setPixelColor(0, rgbLed.Color(255, 0, 0)); // vermelho fixo = acionado
  } else {
    uint8_t b = heartbeatBrightness(millis());
    rgbLed.setPixelColor(0, rgbLed.Color(0, 0, b));   // azul pulsando = em espera
  }
  rgbLed.show();
}

// ---------------------------------------------------------------------------
// SERVOS
// ---------------------------------------------------------------------------
int angleToPulse(uint8_t channel, int angle) {
  angle = constrain(angle, 0, 180);
  int pulse = map(angle, 0, 180, SERVO_PULSE_MIN, SERVO_PULSE_MAX);
  pulse += constrain(trimPulse[channel], -TRIM_LIMIT, TRIM_LIMIT);
  return constrain(pulse, SERVO_PULSE_MIN - TRIM_LIMIT, SERVO_PULSE_MAX + TRIM_LIMIT);
}

void setServoAngle(uint8_t channel, int angle) {
  pwm.setPWM(channel, 0, angleToPulse(channel, angle));
}

void applyServoPositions() {
  setServoAngle(CH_YAW,   constrain(posYaw,   YAW_MIN,   YAW_MAX));
  setServoAngle(CH_PITCH, constrain(posPitch, PITCH_MIN, PITCH_MAX));
  setServoAngle(CH_EYE_L, constrain(posEyeL,  EYE_L_MIN, EYE_L_MAX));
  setServoAngle(CH_EYE_R, constrain(posEyeR,  EYE_R_MIN, EYE_R_MAX));
}

void servosToCenter() {
  posYaw = YAW_CENTER;
  posPitch = PITCH_CENTER;
  posEyeL = EYE_L_CENTER;
  posEyeR = EYE_R_CENTER;
  applyServoPositions();
}

// Corta o pulso PWM dos 4 canais (equivalente a servo.detach()). O mecanismo
// (peso/atrito/geometria) sustenta a posicao de repouso sem o servo energizado -
// evita corrente de sustentacao continua enquanto o busto esta parado.
// Uma nova chamada a applyServoPositions()/setServoAngle() reenergiza o canal
// normalmente (o valor de OFF deixa de ter o bit de "full-off" ligado).
void releaseServos() {
  pwm.setPWM(CH_YAW,   0, 4096);
  pwm.setPWM(CH_PITCH, 0, 4096);
  pwm.setPWM(CH_EYE_L, 0, 4096);
  pwm.setPWM(CH_EYE_R, 0, 4096);
}

// ---------------------------------------------------------------------------
// SENSORES HC-SR04
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

bool anySensorTriggered() {
  long dF = readDistanceCm(PIN_HCSR04_FRONT_TRIG, PIN_HCSR04_FRONT_ECHO);
  long dL = readDistanceCm(PIN_HCSR04_LEFT_TRIG,  PIN_HCSR04_LEFT_ECHO);
  long dR = readDistanceCm(PIN_HCSR04_RIGHT_TRIG, PIN_HCSR04_RIGHT_ECHO);

  if (dF > 0 && dF < DETECT_RANGE_CM) return true;
  if (dL > 0 && dL < DETECT_RANGE_CM) return true;
  if (dR > 0 && dR < DETECT_RANGE_CM) return true;
  return false;
}

// ---------------------------------------------------------------------------
// DISPLAY - animacao de boot estilo terminal (texto verde, paisagem) +
// backlight via PWM (GPIO 18)
// ---------------------------------------------------------------------------
const char* BOOT_WORDS[] = {
  "INIT", "BOOT", "SCAN", "SYS", "MEM", "SERVO", "PWM", "I2C",
  "EYE", "LOAD", "ACT", "CAL", "WIFI", "HC_SR04"
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

void setBacklight(uint8_t level) {
  backlightLevel = level;
  ledcWrite(PIN_TFT_BLK, level);
}

// desenha N colunas de texto subindo, por 'durationMs' milissegundos
void terminalAnimation(unsigned long durationMs) {
  const int cols = 3;
  const int colWidth = tft.width() / cols;
  const int lineHeight = 9;
  const int maxLines = tft.height() / lineHeight;

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);

  int lineCount[cols] = {0,0,0};
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    for (int c = 0; c < cols; c++) {
      // rola a coluna pra cima
      if (lineCount[c] >= maxLines) {
        // limpa a coluna e recomeca de baixo (efeito simplificado, sem scroll real de pixel)
        tft.fillRect(c*colWidth, 0, colWidth, tft.height(), ST77XX_BLACK);
        lineCount[c] = 0;
      }
      tft.setCursor(c*colWidth + 2, tft.height() - (lineCount[c]+1)*lineHeight);
      tft.print(randomBootLine());
      lineCount[c]++;
    }
    delay(140);
  }
}

void showStandby() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(4, tft.height()/2 - 4);
  tft.print("STANDBY");
}

void showTargetAcquired() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(4, tft.height()/2 - 4);
  tft.print("TARGET ACQUIRED");
}

void showPairingMode() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 4);
  tft.print("PAIRING MODE");
  tft.setCursor(4, 16);
  tft.print(WiFi.softAPIP().toString());
}

void showRemoteModeBadge() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(4, 4);
  tft.print("REMOTE MODE");
}

// ---------------------------------------------------------------------------
// PAGINA HTML (servida em "/") - sliders + toggle de modo + calibracao +
// brilho do backlight
// ---------------------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>T-800 Controle</title>
<style>
  body { background:#111; color:#0f0; font-family: monospace; text-align:center; padding:20px; }
  h1 { font-size:18px; letter-spacing:2px; }
  .slider-box { margin:20px auto; max-width:320px; text-align:left; }
  label { display:block; margin-bottom:4px; }
  input[type=range] { width:100%; }
  .toggle-box { margin:24px auto; }
  button { background:#0f0; color:#111; border:none; padding:10px 16px; font-family:monospace;
           font-weight:bold; border-radius:4px; margin:4px; }
  button.off { background:#333; color:#0f0; border:1px solid #0f0; }
  #status { margin-top:16px; font-size:12px; opacity:0.7; }
</style>
</head>
<body>
  <h1>&gt; T-800 CONTROL_</h1>

  <div class="toggle-box">
    <button id="btnSensor" onclick="setMode('sensor')">MODO SENSORES</button>
    <button id="btnRemote" onclick="setMode('remote')">MODO CELULAR</button>
  </div>

  <div class="slider-box">
    <label>YAW (esquerda / direita): <span id="yawVal">90</span></label>
    <input type="range" min="45" max="135" value="90" id="yaw" oninput="sendMove()">
  </div>
  <div class="slider-box">
    <label>PITCH (cima / baixo): <span id="pitchVal">95</span></label>
    <input type="range" min="70" max="120" value="95" id="pitch" oninput="sendMove()">
  </div>
  <div class="slider-box">
    <label>OLHO ESQUERDO: <span id="eyeLVal">90</span></label>
    <input type="range" min="60" max="120" value="90" id="eyeL" oninput="sendMove()">
  </div>
  <div class="slider-box">
    <label>OLHO DIREITO: <span id="eyeRVal">90</span></label>
    <input type="range" min="60" max="120" value="90" id="eyeR" oninput="sendMove()">
  </div>

  <hr style="border-color:#333; margin:24px 0;">
  <h1 style="font-size:14px;">&gt; DISPLAY_</h1>
  <div class="slider-box">
    <label>Brilho do backlight: <span id="blkVal">200</span></label>
    <input type="range" min="0" max="255" value="200" id="blk" oninput="sendBacklight()">
  </div>

  <hr style="border-color:#333; margin:24px 0;">
  <h1 style="font-size:14px;">&gt; CALIBRACAO_ (centro/zero)</h1>
  <div class="slider-box">
    <label>Trim YAW: <span id="trimYawVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimYaw" oninput="sendTrim()">
  </div>
  <div class="slider-box">
    <label>Trim PITCH: <span id="trimPitchVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimPitch" oninput="sendTrim()">
  </div>
  <div class="slider-box">
    <label>Trim OLHO ESQUERDO: <span id="trimEyeLVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimEyeL" oninput="sendTrim()">
  </div>
  <div class="slider-box">
    <label>Trim OLHO DIREITO: <span id="trimEyeRVal">0</span></label>
    <input type="range" min="-80" max="80" value="0" id="trimEyeR" oninput="sendTrim()">
  </div>
  <div class="toggle-box">
    <button onclick="saveTrim()">SALVAR CALIBRACAO</button>
    <button class="off" onclick="goIdle()">CENTRALIZAR E DESENERGIZAR</button>
  </div>
  <div id="trimStatus" style="font-size:11px; opacity:0.7;"></div>

  <div id="status">modo atual: --</div>

<script>
function sendMove() {
  const yaw = document.getElementById('yaw').value;
  const pitch = document.getElementById('pitch').value;
  const eyeL = document.getElementById('eyeL').value;
  const eyeR = document.getElementById('eyeR').value;
  document.getElementById('yawVal').innerText = yaw;
  document.getElementById('pitchVal').innerText = pitch;
  document.getElementById('eyeLVal').innerText = eyeL;
  document.getElementById('eyeRVal').innerText = eyeR;
  fetch(`/move?yaw=${yaw}&pitch=${pitch}&eyeL=${eyeL}&eyeR=${eyeR}`);
}
function setMode(m) {
  fetch(`/mode?state=${m}`).then(()=>refreshStatus());
}
function sendBacklight() {
  const v = document.getElementById('blk').value;
  document.getElementById('blkVal').innerText = v;
  fetch(`/backlight?val=${v}&save=1`);
}
let trimLoaded = false;
function refreshStatus() {
  fetch('/status').then(r=>r.json()).then(d=>{
    document.getElementById('status').innerText = 'modo atual: ' + d.mode;
    document.getElementById('btnSensor').className = d.mode==='sensor' ? '' : 'off';
    document.getElementById('btnRemote').className = d.mode==='remote' ? '' : 'off';
    // so preenche os sliders de trim/backlight UMA vez (no carregamento da
    // pagina), com o valor realmente salvo no ESP32 - evita sobrescrever a
    // calibracao com 0 caso o usuario mexa no slider sem ter recarregado a
    // pagina antes.
    if (!trimLoaded) {
      document.getElementById('trimYaw').value = d.trimYaw;
      document.getElementById('trimPitch').value = d.trimPitch;
      document.getElementById('trimEyeL').value = d.trimEyeL;
      document.getElementById('trimEyeR').value = d.trimEyeR;
      document.getElementById('trimYawVal').innerText = d.trimYaw;
      document.getElementById('trimPitchVal').innerText = d.trimPitch;
      document.getElementById('trimEyeLVal').innerText = d.trimEyeL;
      document.getElementById('trimEyeRVal').innerText = d.trimEyeR;
      document.getElementById('blk').value = d.backlight;
      document.getElementById('blkVal').innerText = d.backlight;
      trimLoaded = true;
    }
  });
}
function sendTrim() {
  const ty = document.getElementById('trimYaw').value;
  const tp = document.getElementById('trimPitch').value;
  const tl = document.getElementById('trimEyeL').value;
  const tr = document.getElementById('trimEyeR').value;
  document.getElementById('trimYawVal').innerText = ty;
  document.getElementById('trimPitchVal').innerText = tp;
  document.getElementById('trimEyeLVal').innerText = tl;
  document.getElementById('trimEyeRVal').innerText = tr;
  // aplica na hora (RAM), sem gravar na NVS ainda - so move o servo pro novo centro
  fetch(`/trim?yaw=${ty}&pitch=${tp}&eyeL=${tl}&eyeR=${tr}`);
  document.getElementById('trimStatus').innerText = 'ajustando... (nao salvo)';
}
function saveTrim() {
  fetch(`/trim?save=1`).then(()=>{
    document.getElementById('trimStatus').innerText = 'calibracao salva na memoria do ESP32.';
  });
}
function goIdle() {
  fetch('/idle');
}
refreshStatus();
setInterval(refreshStatus, 4000);
</script>
</body>
</html>
)HTML";

// ---------------------------------------------------------------------------
// HANDLERS DO SERVIDOR WEB
// ---------------------------------------------------------------------------
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleMove() {
  if (currentMode != MODE_REMOTE) {
    server.send(409, "text/plain", "Fora do modo remoto - troque o modo antes de mover.");
    return;
  }
  if (server.hasArg("yaw"))   posYaw   = server.arg("yaw").toInt();
  if (server.hasArg("pitch")) posPitch = server.arg("pitch").toInt();
  if (server.hasArg("eyeL"))  posEyeL  = server.arg("eyeL").toInt();
  if (server.hasArg("eyeR"))  posEyeR  = server.arg("eyeR").toInt();
  applyServoPositions();
  server.send(200, "text/plain", "ok");
}

void handleSetMode() {
  if (server.hasArg("state")) {
    String s = server.arg("state");
    if (s == "remote") {
      currentMode = MODE_REMOTE;
      showRemoteModeBadge();
    } else {
      currentMode = MODE_SENSOR;
      showStandby();
    }
    prefs.putUChar("mode", (uint8_t)currentMode);
  }
  server.send(200, "text/plain", "ok");
}

void handleStatus() {
  String json = String("{\"mode\":\"") + (currentMode == MODE_REMOTE ? "remote" : "sensor") +
                "\",\"trimYaw\":" + String(trimPulse[CH_YAW]) +
                ",\"trimPitch\":" + String(trimPulse[CH_PITCH]) +
                ",\"trimEyeL\":" + String(trimPulse[CH_EYE_L]) +
                ",\"trimEyeR\":" + String(trimPulse[CH_EYE_R]) +
                ",\"backlight\":" + String(backlightLevel) + "}";
  server.send(200, "application/json", json);
}

// Calibracao de centro/zero. Ajusta o trim em RAM e mostra o efeito na hora
// (posiciona o eixo no seu centro); so grava na NVS quando chamado com save=1.
void handleTrim() {
  bool changed = false;
  if (server.hasArg("yaw"))   { trimPulse[CH_YAW]   = server.arg("yaw").toInt();   changed = true; }
  if (server.hasArg("pitch")) { trimPulse[CH_PITCH] = server.arg("pitch").toInt(); changed = true; }
  if (server.hasArg("eyeL"))  { trimPulse[CH_EYE_L] = server.arg("eyeL").toInt();  changed = true; }
  if (server.hasArg("eyeR"))  { trimPulse[CH_EYE_R] = server.arg("eyeR").toInt();  changed = true; }

  if (changed) {
    posYaw = YAW_CENTER; posPitch = PITCH_CENTER;
    posEyeL = EYE_L_CENTER; posEyeR = EYE_R_CENTER;
    applyServoPositions();   // reenergiza e mostra o novo centro na hora
  }

  if (server.hasArg("save") && server.arg("save") == "1") {
    prefs.putInt("trimYaw",   trimPulse[CH_YAW]);
    prefs.putInt("trimPitch", trimPulse[CH_PITCH]);
    prefs.putInt("trimEyeL",  trimPulse[CH_EYE_L]);
    prefs.putInt("trimEyeR", trimPulse[CH_EYE_R]);
  }
  server.send(200, "text/plain", "ok");
}

// Brilho do backlight (PWM, GPIO 18). save=1 persiste em NVS.
void handleBacklight() {
  if (server.hasArg("val")) {
    int v = constrain(server.arg("val").toInt(), 0, 255);
    setBacklight((uint8_t)v);
  }
  if (server.hasArg("save") && server.arg("save") == "1") {
    prefs.putUChar("blk", backlightLevel);
  }
  server.send(200, "text/plain", "ok");
}

// Centraliza (com o trim aplicado) e corta o PWM - usado apos calibrar manualmente.
void handleIdle() {
  servosToCenter();
  delay(300);
  releaseServos();
  server.send(200, "text/plain", "ok");
}

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  // --- I2C / PCA9685 ---
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);

  // --- sensores ---
  pinMode(PIN_HCSR04_FRONT_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_FRONT_ECHO, INPUT);
  pinMode(PIN_HCSR04_LEFT_TRIG,  OUTPUT);
  pinMode(PIN_HCSR04_LEFT_ECHO,  INPUT);
  pinMode(PIN_HCSR04_RIGHT_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_RIGHT_ECHO, INPUT);

  // --- LED do olho ---
  pinMode(PIN_EYE_LED, OUTPUT);
  analogWrite(PIN_EYE_LED, 0);

  // --- LED RGB de status ---
  rgbLed.begin();
  rgbLed.setBrightness(120); // limite geral, evita ofuscar / puxar corrente demais
  rgbLed.show();

  // --- display ---
  SPI.begin(PIN_TFT_SCLK, -1, PIN_TFT_MOSI, PIN_TFT_CS);
  tft.initR(INITR_MINI160x80);       // ajuste conforme o driver exato do seu painel
  tft.setRotation(1);                // paisagem (160x80)

  // --- backlight (PWM, GPIO 18) ---
  ledcAttach(PIN_TFT_BLK, LEDC_FREQ_BACKLIGHT, LEDC_RES_BACKLIGHT);
  setBacklight(backlightLevel);      // valor padrao ate carregar o salvo em NVS

  terminalAnimation(2500);           // animacao de boot ~2.5s

  // --- preferencias / modo, calibracao e backlight salvos (ANTES de
  //     centralizar os servos, para que o trim gravado ja seja usado no
  //     centro do boot) ---
  prefs.begin("t800", false);
  currentMode = (ControlMode)prefs.getUChar("mode", MODE_SENSOR);
  trimPulse[CH_YAW]   = prefs.getInt("trimYaw",   0);
  trimPulse[CH_PITCH] = prefs.getInt("trimPitch", 0);
  trimPulse[CH_EYE_L] = prefs.getInt("trimEyeL",  0);
  trimPulse[CH_EYE_R] = prefs.getInt("trimEyeR",  0);
  backlightLevel = prefs.getUChar("blk", BACKLIGHT_DEFAULT);
  setBacklight(backlightLevel);       // reaplica o nivel realmente salvo

  // --- servos em posicao neutra (ja com o trim salvo), depois desenergizados ---
  servosToCenter();
  delay(300);       // tempo para os servos assentarem fisicamente no centro
  releaseServos();

  // --- Wi-Fi Access Point ---
  WiFi.softAP(AP_SSID, AP_PASS);
  showPairingMode();
  delay(2000);

  // --- rotas do servidor web ---
  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/mode", handleSetMode);
  server.on("/status", handleStatus);
  server.on("/trim", handleTrim);
  server.on("/backlight", handleBacklight);
  server.on("/idle", handleIdle);
  server.begin();

  if (currentMode == MODE_REMOTE) showRemoteModeBadge();
  else showStandby();
}

// ---------------------------------------------------------------------------
// LOOP
// ---------------------------------------------------------------------------
void loop() {
  server.handleClient();
  updateStatusLed();   // roda toda iteracao: pulso azul em espera, vermelho fixo se acionado

  if (currentMode == MODE_SENSOR) {
    if (!movementActive) {
      if (anySensorTriggered()) {
        movementActive = true;
        movementStartMs = millis();
        showTargetAcquired();
        terminalAnimation(300); // pequeno "flash" de animacao ao disparar
      }
    } else {
      // ciclo de movimento ativo: anima o display continuamente
      terminalAnimation(200);

      // exemplo simples de padrao de movimento - AJUSTAR conforme a coreografia desejada
      posYaw   = YAW_CENTER   + (int)(20 * sin(millis() / 500.0));
      posPitch = PITCH_CENTER + (int)(10 * sin(millis() / 700.0));
      posEyeL  = EYE_L_CENTER + (int)(15 * sin(millis() / 400.0));
      posEyeR  = EYE_R_CENTER + (int)(15 * sin(millis() / 400.0));
      applyServoPositions();
      analogWrite(PIN_EYE_LED, 180 + (int)(75 * sin(millis()/300.0)));

      if (millis() - movementStartMs > MOVE_CYCLE_MS) {
        movementActive = false;
        servosToCenter();
        delay(300);        // aguarda os servos assentarem no centro antes de cortar o PWM
        releaseServos();   // desenergiza - repouso sustentado mecanicamente, sem consumo
        analogWrite(PIN_EYE_LED, 0);
        showStandby();
      }
    }
  }
  // no modo remoto, o movimento e so via /move (chamado pelo handler),
  // entao o loop so precisa manter o servidor respondendo.
}
