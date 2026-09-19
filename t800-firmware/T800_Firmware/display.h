#pragma once
#include <Arduino.h>

void displayInit();
void terminalAnimation(unsigned long durationMs);
void showStandby();
void showTargetAcquired();
void showPairingMode();
void showRemoteModeBadge();

// Backlight via PWM (GPIO 18, LEDC - ledcAttach/ledcWrite, ESP32 Arduino Core 3.x)
void setBacklight(uint8_t level);
