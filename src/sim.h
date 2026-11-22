// Modo simulador: genera datos y averías de ejemplo sin moto.
#pragma once
#include <Arduino.h>

class Simulador {
 public:
  void iniciar();
  void paso();   // se llama continuamente desde la tarea de comunicación
 private:
  void ponerAverias(bool todas);
  uint32_t msInicio_ = 0;
  uint32_t msUltimo_ = 0;
  uint32_t msBorrado_ = 0;
  float temp_ = 25;
};

extern Simulador SIM;
