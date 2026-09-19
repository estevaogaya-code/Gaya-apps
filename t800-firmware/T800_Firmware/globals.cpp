#include "globals.h"

ControlMode currentMode = MODE_SENSOR;

volatile int posYaw   = YAW_CENTER;
volatile int posPitch = PITCH_CENTER;
volatile int posEyeL  = EYE_L_CENTER;
volatile int posEyeR  = EYE_R_CENTER;

bool movementActive = false;
unsigned long movementStartMs = 0;

int trimPulse[4] = {0, 0, 0, 0};

uint8_t backlightLevel = BACKLIGHT_DEFAULT;
