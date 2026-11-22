// Servicios OBD2 sobre KWP2000: conexión, PIDs, averías, foto del fallo, datos de la centralita.
#pragma once
#include <Arduino.h>
#include "estado.h"

class Obd {
 public:
  void reiniciar();
  void paso();                // se llama continuamente desde la tarea de comunicación
  bool intentarConectar();    // una ronda completa de métodos de inicialización
  bool conectado() const { return conectado_; }

 private:
  bool probarMetodo(int metodo);
  void lecturaInicial();
  bool detectarPids();
  bool leerMil();
  bool leerAveriasSvc(uint8_t svc, ListaAverias& lista);
  bool leerAverias(bool porBorrado);
  void leerFoto();
  void leerInfo();
  bool leerPid(uint8_t pid);
  void borrarAverias();
  uint8_t siguientePid();
  void desconectar(const char* motivo);
  void respuestaOk();
  void respuestaFallida();

  bool conectado_ = false;
  int metodoBueno_ = -1;
  uint8_t fallos_ = 0;
  uint32_t msUltimaPeticion_ = 0;
  uint32_t msUltimasAverias_ = 0;
  uint32_t msUltimoIntento_ = 0;
  uint32_t ranura_ = 0;
  uint8_t idxOtros_ = 0;
  uint8_t otros_[64];
  uint8_t nOtros_ = 0;
};

extern Obd OBD;
