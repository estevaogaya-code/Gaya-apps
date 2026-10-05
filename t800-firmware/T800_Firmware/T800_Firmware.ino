/* ============================================================================
   T-800 ANIMATRONIC BUST - FIRMWARE PRINCIPAL
   ESP32-S3-N16R8

   Recursos:
   - 4 servos via PCA9685 (I2C): olho esquerdo, olho direito, pitch (cima/baixo),
     yaw (esquerda/direita)
   - 3 sensores ultrassonicos HC-SR04 (frente, esquerda, direita)
   - Display TFT 80x160 (driver ST7735), paisagem, boot em estilo terminal (verde),
     backlight controlado via PWM (GPIO 18)
   - LED do olho (PWM)
   - Modo Wi-Fi: ESP32 sobe como Access Point + pagina web de controle
   - Alternancia de modo: SENSOR (automatico) x REMOTO (comandado pelo celular)
   - Estado salvo em NVS (Preferences), sobrevive a reset
   - Modo SENSOR: apos o ciclo de 30s, os servos voltam ao centro e o PWM e
     cortado (releaseServos) - assume que a mecanica (peso/atrito/geometria)
     sustenta a posicao de repouso sem o servo energizado. Se o mecanismo do
     seu busto NAO segurar sozinho a posicao central, o eixo pode "cair" com
     o PWM desligado - nesse caso, remova a chamada a releaseServos().

   Organizacao dos arquivos:
   - config.h      pinout e constantes de calibracao
   - globals.h/cpp estado compartilhado (modo, posicoes, trims, backlight)
   - servos.h/cpp  PCA9685
   - sensors.h/cpp HC-SR04
   - display.h/cpp TFT + backlight PWM
   - status_led.h/cpp LED RGB de status (NeoPixel)
   - storage.h/cpp NVS (Preferences)
   - web.h/cpp     Access Point + servidor web de controle

   AVISOS IMPORTANTES (leia antes de gravar):
   - Os pinos em config.h sao um mapeamento de referencia para ESP32-S3. Confira
     contra a serigrafia da SUA placa antes de ligar qualquer coisa - alguns
     GPIOs de S3 sao "strapping pins" e nao devem ser usados aqui (0, 3, 45, 46).
   - Angulos de servo (MIN/MAX_PULSE, offsets) sao valores de partida - CALIBRE
     fisicamente cada servo antes de confiar em qualquer limite de curso.
   - Testado apenas por compilacao mental / revisao de logica - grave aos poucos
     e teste servo por servo antes de rodar a sequencia completa.
   ========================================================================= */

#include "config.h"
#include "globals.h"
#include "servos.h"
#include "sensors.h"
#include "display.h"
#include "status_led.h"
#include "storage.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0));

  servosInit();
  sensorsInit();

  pinMode(PIN_EYE_LED, OUTPUT);
  analogWrite(PIN_EYE_LED, 0);

  statusLedInit();

  displayInit();
  terminalAnimation(2500);           // animacao de boot ~2.5s

  // preferencias / modo e calibracao salvos (ANTES de centralizar os servos,
  // para que o trim gravado ja seja usado no centro do boot)
  storageLoad();
  setBacklight(backlightLevel);      // reaplica o nivel salvo (displayInit ja usou o default)

  // servos em posicao neutra (ja com o trim salvo), depois desenergizados
  servosToCenter();
  delay(300);       // tempo para os servos assentarem fisicamente no centro
  releaseServos();

  showPairingMode();
  delay(2000);

  webInit();

  if (currentMode == MODE_REMOTE) showRemoteModeBadge();
  else showStandby();
}

void loop() {
  webHandleClient();
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
      analogWrite(PIN_EYE_LED, 180 + (int)(75 * sin(millis() / 300.0)));

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
