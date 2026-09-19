#pragma once
#include <Arduino.h>

void servosInit();
int angleToPulse(uint8_t channel, int angle);
void setServoAngle(uint8_t channel, int angle);
void applyServoPositions();
void servosToCenter();

// Corta o pulso PWM dos 4 canais (equivalente a servo.detach()). O mecanismo
// (peso/atrito/geometria) sustenta a posicao de repouso sem o servo energizado -
// evita corrente de sustentacao continua enquanto o busto esta parado.
// Uma nova chamada a applyServoPositions()/setServoAngle() reenergiza o canal
// normalmente (o valor de OFF deixa de ter o bit de "full-off" ligado).
void releaseServos();
