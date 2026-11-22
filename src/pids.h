// Tabla de PIDs OBD2 (servicio 0x01 / 0x02) que se saben decodificar.
// Solo se muestran los que la centralita declara como admitidos.
#pragma once
#include <Arduino.h>

struct DefPid {
  uint8_t id;
  uint8_t bytes;        // bytes de datos de la respuesta
  uint8_t decimales;    // para mostrar en la web
  const char* nombre;
  const char* unidad;
};

// La temperatura exterior (0x46) se excluye a propósito.
static const DefPid TABLA_PIDS[] = {
  {0x04, 1, 0, "Carga del motor", "%"},
  {0x05, 1, 0, "Temperatura del motor", "°C"},
  {0x06, 1, 1, "Corrección combustible corta", "%"},
  {0x07, 1, 1, "Corrección combustible larga", "%"},
  {0x0A, 1, 0, "Presión de combustible", "kPa"},
  {0x0B, 1, 0, "Presión de admisión", "kPa"},
  {0x0C, 2, 0, "Revoluciones", "rpm"},
  {0x0D, 1, 0, "Velocidad", "km/h"},
  {0x0E, 1, 1, "Avance de encendido", "°"},
  {0x0F, 1, 0, "Temperatura aire admisión", "°C"},
  {0x10, 2, 2, "Caudal de aire", "g/s"},
  {0x11, 1, 0, "Acelerador", "%"},
  {0x14, 2, 3, "Sonda lambda", "V"},
  {0x15, 2, 3, "Sonda lambda 2", "V"},
  {0x1F, 2, 0, "Tiempo desde arranque", "s"},
  {0x21, 2, 0, "Distancia con testigo encendido", "km"},
  {0x2F, 1, 0, "Nivel de combustible", "%"},
  {0x30, 1, 0, "Arranques desde borrado", ""},
  {0x31, 2, 0, "Distancia desde borrado", "km"},
  {0x33, 1, 0, "Presión barométrica", "kPa"},
  {0x42, 2, 1, "Tensión módulo / batería", "V"},
  {0x43, 2, 0, "Carga absoluta", "%"},
  {0x44, 2, 3, "Lambda solicitada", "λ"},
  {0x45, 1, 0, "Acelerador relativo", "%"},
  {0x47, 1, 0, "Acelerador posición B", "%"},
  {0x4C, 1, 0, "Acelerador ordenado", "%"},
  {0x5C, 1, 0, "Temperatura del aceite", "°C"},
  {0xA6, 4, 1, "Cuentakilómetros", "km"},
};
static const size_t NUM_TABLA_PIDS = sizeof(TABLA_PIDS) / sizeof(TABLA_PIDS[0]);

// PIDs con prioridad en el sondeo (además de las revoluciones)
static const uint8_t PIDS_PRIORITARIOS[] = {0x0D, 0x05, 0x0F, 0x0B, 0x11, 0x04, 0x0E, 0x14, 0x42};

inline const DefPid* buscarPid(uint8_t id) {
  for (size_t i = 0; i < NUM_TABLA_PIDS; i++)
    if (TABLA_PIDS[i].id == id) return &TABLA_PIDS[i];
  return nullptr;
}

// Decodifica según SAE J1979. d apunta a los bytes A, B...
inline float decodificarPid(uint8_t id, const uint8_t* d) {
  float A = d[0], B = d[1];
  switch (id) {
    case 0x04: case 0x11: case 0x2F: case 0x45: case 0x47: case 0x4C: return A * 100.0f / 255.0f;
    case 0x05: case 0x0F: case 0x5C: return A - 40.0f;
    case 0x06: case 0x07: return (A - 128.0f) * 100.0f / 128.0f;
    case 0x0A: return A * 3.0f;
    case 0x0B: case 0x0D: case 0x30: case 0x33: return A;
    case 0x0C: return (A * 256.0f + B) / 4.0f;
    case 0x0E: return A / 2.0f - 64.0f;
    case 0x10: return (A * 256.0f + B) / 100.0f;
    case 0x14: case 0x15: return A / 200.0f;
    case 0x1F: case 0x21: case 0x31: return A * 256.0f + B;
    case 0x42: return (A * 256.0f + B) / 1000.0f;
    case 0x43: return (A * 256.0f + B) * 100.0f / 255.0f;
    case 0x44: return (A * 256.0f + B) / 32768.0f;
    case 0xA6: return (((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3]) / 10.0f;
  }
  return NAN;
}
