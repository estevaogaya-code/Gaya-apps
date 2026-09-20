#pragma once
#include <Arduino.h>

namespace WifiManager {

  void begin();          // conecta pela primeira vez (bloqueia até timeout)
  void loop(uint32_t nowMs); // chamar a cada iteração do loop principal
  bool isConnected();
}
