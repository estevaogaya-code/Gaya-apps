#pragma once
#include <Arduino.h>
#include "config.h"

enum ControlMode { MODE_SENSOR = 0, MODE_REMOTE = 1 };

extern ControlMode currentMode;

// posicoes atuais (graus) - usadas tanto pelo modo remoto quanto pelo sensor
extern volatile int posYaw;
extern volatile int posPitch;
extern volatile int posEyeL;
extern volatile int posEyeR;

extern bool movementActive;
extern unsigned long movementStartMs;

// Trim de calibracao do centro/zero de cada servo, em contagens de PWM (12 bits).
// Indexado pelo numero do canal (CH_EYE_L=0, CH_EYE_R=1, CH_PITCH=2, CH_YAW=3).
// Persistido em NVS (Preferences) - sobrevive a reset. Ajustavel pela interface web.
extern int trimPulse[4];

// Nivel de backlight do display (0-255), persistido em NVS.
extern uint8_t backlightLevel;
