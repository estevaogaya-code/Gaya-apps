#pragma once
#include <Arduino.h>

// Carrega modo, trims de calibracao e backlight salvos em NVS para as
// variaveis globais correspondentes (globals.h). Chamar uma vez no setup(),
// antes de centralizar os servos e inicializar o display.
void storageLoad();

void storageSaveMode();
void storageSaveTrim();
void storageSaveBacklight();
