// Estado compartido entre la tarea de comunicación (línea K o simulador),
// el bucle principal y el servidor web.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

enum EstadoConexion : uint8_t { DESCONECTADO = 0, CONECTANDO = 1, CONECTADO = 2 };

#define MAX_AVERIAS 16
#define MAX_FOTO    24

struct ValorPid {
  bool soportado;
  bool valido;
  float valor;
  uint32_t ms;   // millis() de la última lectura
};

struct FotoFallo {
  bool valida;
  char dtc[6];
  uint8_t n;
  uint8_t pid[MAX_FOTO];
  float valor[MAX_FOTO];
};

struct ListaAverias {
  uint8_t n;
  char cod[MAX_AVERIAS][6];
};

struct InfoEcu {
  char vin[24];
  char calid[40];
  char protocolo[80];
  char direccion[24];
  uint8_t pidsAdmitidos;     // PIDs del servicio 01 declarados por la centralita
  uint8_t pidsConocidos;     // de esos, los que esta versión sabe mostrar
};

// Órdenes de la web a la tarea de comunicación
enum Orden : uint8_t { ORD_NINGUNA = 0, ORD_LEER_AVERIAS, ORD_BORRAR_AVERIAS };

struct Estado {
  SemaphoreHandle_t mutex;

  bool simulador;
  EstadoConexion conexion;
  ValorPid v[256];

  bool mil;                  // testigo del motor
  bool milConocido;
  ListaAverias guardadas;
  ListaAverias pendientes;
  bool averiasLeidas;
  uint32_t msLecturaAverias; // millis() de la última lectura correcta
  uint32_t versionAverias;   // se incrementa cada vez que cambian
  bool ultimaLecturaPorBorrado;
  FotoFallo foto;
  InfoEcu ecu;

  volatile Orden orden;
  volatile bool prioridadVelocidad;  // durante la medición 0–50

  uint32_t msUltimaRespuesta;        // última respuesta correcta de la centralita
};

extern Estado E;

struct Bloqueo {
  Bloqueo() { xSemaphoreTake(E.mutex, portMAX_DELAY); }
  ~Bloqueo() { xSemaphoreGive(E.mutex); }
};

// Bloqueo de los datos de la aplicación (récords, mantenimiento, grabación, historial)
// entre el bucle principal y el servidor web.
extern SemaphoreHandle_t mutexApp;
struct BloqueoApp {
  BloqueoApp() { xSemaphoreTakeRecursive(mutexApp, portMAX_DELAY); }
  ~BloqueoApp() { xSemaphoreGiveRecursive(mutexApp); }
};

// Cola de eventos hacia la web (texto JSON ya formado)
void eventoWeb(const char* json);
bool sacarEventoWeb(char* buf, size_t len);

inline float valorPid(uint8_t pid, bool* ok = nullptr) {
  Bloqueo b;
  bool valido = E.v[pid].soportado && E.v[pid].valido && (millis() - E.v[pid].ms < 10000);
  if (ok) *ok = valido;
  return valido ? E.v[pid].valor : NAN;
}

inline void dtcATexto(uint16_t raw, char* out) {
  static const char L[] = {'P', 'C', 'B', 'U'};
  out[0] = L[(raw >> 14) & 3];
  out[1] = '0' + ((raw >> 12) & 3);
  snprintf(out + 2, 4, "%03X", raw & 0x0FFF);
}
