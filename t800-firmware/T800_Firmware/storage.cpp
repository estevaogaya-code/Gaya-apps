#include <Preferences.h>
#include "storage.h"
#include "config.h"
#include "globals.h"

Preferences prefs;

void storageLoad() {
  prefs.begin("t800", false);
  currentMode = (ControlMode)prefs.getUChar("mode", MODE_SENSOR);
  trimPulse[CH_YAW]   = prefs.getInt("trimYaw",   0);
  trimPulse[CH_PITCH] = prefs.getInt("trimPitch", 0);
  trimPulse[CH_EYE_L] = prefs.getInt("trimEyeL",  0);
  trimPulse[CH_EYE_R] = prefs.getInt("trimEyeR",  0);
  backlightLevel = prefs.getUChar("blk", BACKLIGHT_DEFAULT);
}

void storageSaveMode() {
  prefs.putUChar("mode", (uint8_t)currentMode);
}

void storageSaveTrim() {
  prefs.putInt("trimYaw",   trimPulse[CH_YAW]);
  prefs.putInt("trimPitch", trimPulse[CH_PITCH]);
  prefs.putInt("trimEyeL",  trimPulse[CH_EYE_L]);
  prefs.putInt("trimEyeR",  trimPulse[CH_EYE_R]);
}

void storageSaveBacklight() {
  prefs.putUChar("blk", backlightLevel);
}
