#include "sensors.h"
#include "config.h"

void sensorsInit() {
  pinMode(PIN_HCSR04_FRONT_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_FRONT_ECHO, INPUT);
  pinMode(PIN_HCSR04_LEFT_TRIG,  OUTPUT);
  pinMode(PIN_HCSR04_LEFT_ECHO,  INPUT);
  pinMode(PIN_HCSR04_RIGHT_TRIG, OUTPUT);
  pinMode(PIN_HCSR04_RIGHT_ECHO, INPUT);
}

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
