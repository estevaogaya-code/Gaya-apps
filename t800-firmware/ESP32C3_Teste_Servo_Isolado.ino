/* ============================================================================
   TESTE ISOLADO DE 1 SERVO - ESP32-C3
   Objetivo: isolar o item (A) do debug - descobrir se um dos servos dos
   olhos trava/gira continuamente por defeito de potenciometro de feedback,
   SEM a sequencia, SEM os outros servos, SEM Wi-Fi/sensor.

   COMO USAR:
   1) Defina TEST_SERVO_PIN abaixo para o pino que quer testar
      (4 = olho esquerdo, 5 = olho direito, 7 = yaw/cabeca).
   2) Grave este sketch (so ele - desconecte fisicamente os outros 2 servos
      dessa alimentacao por seguranca, ou ao menos garanta que so o pino
      testado esta com sinal ligado).
   3) Abra o Monitor Serial (115200 baud, "Nenhum fim de linha" / "Newline").
   4) Use os comandos abaixo e observe o eixo FISICAMENTE.
   5) Repita trocando TEST_SERVO_PIN para o outro olho, e compare.

   Comandos (digite e ENTER):
     0-180  -> vai direto a esse angulo (graus)
     c      -> centro (90)
     +  -    -> +5 / -5 graus a partir do angulo atual
     e      -> extremo minimo (60) - "comando grande" para servos de olho
     x      -> extremo maximo (120) - "comando grande" para servos de olho
     s      -> varredura lenta continua 60<->120 (comando 'q' pra parar)
     d      -> detach (desenergiza - testa se o eixo MANTEM a posicao so
                por atrito/mecanica, ou se "cai"/gira sozinho)
     a      -> attach de novo, no ultimo angulo comandado

   O QUE OBSERVAR:
     1) Com 'c' (comando pequeno, perto do centro) o servo PARA na posicao,
        ou fica vibrando/girando continuamente?
     2) Com 'e' ou 'x' (comando grande, perto do limite mecanico do olho)
        o servo PARA, ou fica girando continuamente sem parar
        (sintoma classico de potenciometro de feedback com defeito)?
     3) Depois de 'd' (detach), o eixo mantem a posicao so pela mecanica,
        ou cai/gira para outro lugar sozinho? (isola a hipotese do trim no
        limite da faixa / folga mecanica, SEM relacao com o defeito do pote)

   Se o mesmo servo travar em AMBOS os testes (isolado E na sequencia
   completa), reforca o diagnostico de defeito de hardware (troca de peca).
   Se so travar dentro da sequencia completa, o problema esta em como o
   firmware comanda esse eixo, nao no servo.
   ========================================================================= */

#include <ESP32Servo.h>

#define TEST_SERVO_PIN   4   // <-- troque para 4 (olho esq.), 5 (olho dir.) ou 7 (yaw)

#define SERVO_PULSE_MIN_US  500
#define SERVO_PULSE_MAX_US  2400

Servo testServo;
bool attachedNow = false;
int currentAngle = 90;

void attachServo() {
  testServo.setPeriodHertz(50);
  testServo.attach(TEST_SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  attachedNow = true;
  Serial.println("[attach] servo energizado");
}

void detachServo() {
  testServo.detach();
  attachedNow = false;
  Serial.println("[detach] servo desenergizado (PWM cortado) - observe o eixo agora");
}

void goTo(int angle) {
  angle = constrain(angle, 0, 180);
  if (!attachedNow) attachServo();
  currentAngle = angle;
  int us = map(angle, 0, 180, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  testServo.writeMicroseconds(us);
  Serial.print("[goto] ");
  Serial.print(angle);
  Serial.print(" graus (");
  Serial.print(us);
  Serial.println(" us) - observe se o eixo PARA ou continua girando");
}

void printMenu() {
  Serial.println();
  Serial.print("=== TESTE ISOLADO - GPIO ");
  Serial.print(TEST_SERVO_PIN);
  Serial.println(" ===");
  Serial.println("0-180=angulo | c=centro | +/-=5 graus | e=min(60) | x=max(120)");
  Serial.println("s=varredura continua | q=para varredura | d=detach | a=attach");
  Serial.println();
}

bool sweeping = false;
int sweepDir = 1;
unsigned long lastSweepMs = 0;
#define SWEEP_STEP_DELAY_MS 40

void setup() {
  Serial.begin(115200);
  delay(500);
  printMenu();
  goTo(90);
}

void loop() {
  if (sweeping) {
    unsigned long now = millis();
    if (now - lastSweepMs >= SWEEP_STEP_DELAY_MS) {
      lastSweepMs = now;
      int next = currentAngle + sweepDir;
      if (next >= 120) { next = 120; sweepDir = -1; }
      if (next <= 60)  { next = 60;  sweepDir = 1; }
      goTo(next);
    }
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;

    if (line == "c") { sweeping = false; goTo(90); }
    else if (line == "+") { sweeping = false; goTo(currentAngle + 5); }
    else if (line == "-") { sweeping = false; goTo(currentAngle - 5); }
    else if (line == "e") { sweeping = false; goTo(60); }
    else if (line == "x") { sweeping = false; goTo(120); }
    else if (line == "s") { sweeping = true; sweepDir = 1; Serial.println("[sweep] iniciada - 'q' para parar"); }
    else if (line == "q") { sweeping = false; Serial.println("[sweep] parada"); }
    else if (line == "d") { sweeping = false; detachServo(); }
    else if (line == "a") { attachServo(); goTo(currentAngle); }
    else {
      bool isNumber = true;
      for (unsigned int i = 0; i < line.length(); i++) {
        if (!isDigit(line[i])) { isNumber = false; break; }
      }
      if (isNumber) { sweeping = false; goTo(line.toInt()); }
      else { Serial.println("Comando nao reconhecido."); printMenu(); }
    }
  }
}
