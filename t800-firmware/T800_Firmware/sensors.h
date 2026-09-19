#pragma once
#include <Arduino.h>

void sensorsInit();
long readDistanceCm(uint8_t trigPin, uint8_t echoPin);
bool anySensorTriggered();
