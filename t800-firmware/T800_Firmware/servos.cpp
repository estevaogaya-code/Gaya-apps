#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "servos.h"
#include "config.h"
#include "globals.h"

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA9685_ADDR);

void servosInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);
}

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

void releaseServos() {
  pwm.setPWM(CH_YAW,   0, 4096);
  pwm.setPWM(CH_PITCH, 0, 4096);
  pwm.setPWM(CH_EYE_L, 0, 4096);
  pwm.setPWM(CH_EYE_R, 0, 4096);
}
