// Almacenamiento en LittleFS: ajustes, historial de averías, récords,
// mantenimiento y grabaciones de trayectos.
//
// Los datos de la moto se guardan en /datos y los del simulador en /sim,
// para que las pruebas no ensucien el historial real.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// ---------- Ajustes (comunes a moto y simulador) ----------
struct Ajustes {
  float tempLimite;
  bool simulador;
};
extern Ajustes AJ;
void ajustesCargar();
void ajustesGuardar();

// ---------- Utilidades ----------
bool almacenIniciar();
String rutaBase();                 // "/datos" o "/sim"
void almacenCambiarModo();         // recarga todo al cambiar entre moto y simulador
bool horaValida();
uint32_t ahoraEpoch();             // segundos UTC (0 si no hay hora)
size_t fsLibre();                  // en caché 5 s
void fsCambiado();                 // invalida la caché tras borrar o crear archivos
size_t fsTotal();
bool leerJson(const String& ruta, JsonDocument& doc);
bool escribirJson(const String& ruta, const JsonDocument& doc);

// ---------- Historial de averías ----------
void historialRevisar();           // llamar en el bucle: registra cambios de averías
void historialJson(JsonDocument& doc);

// ---------- Récords ----------
struct Records {
  float vmax, rpmMax, t050;        // t050 = 0 si no hay
  uint32_t fVmax, fRpm, f050;      // fechas (epoch)
};
extern Records REC;
void recordsCargar();
void recordsGuardar();
void recordsReiniciar();
void recordsRevisar();             // actualiza máximos y la medición 0–50
void medir050Iniciar();
void medir050Cancelar();
uint8_t medir050Estado();          // 0 inactivo, 1 esperando a que la moto esté parada, 2 listo, 3 midiendo

// ---------- Mantenimiento ----------
struct ItemMant {
  float intervaloKm;
  float hechoKm;
  float hechoHoras;
  uint32_t fecha;
};
#define NUM_MANT 3
struct Mant {
  float odometroKm;      // kilómetros estimados (o leídos de la centralita)
  float horasMotor;
  uint32_t fechaOdometro;
  ItemMant item[NUM_MANT];   // 0 aceite, 1 correa, 2 filtro de aire
};
extern Mant MANT;
extern const char* NOMBRES_MANT[NUM_MANT];
void mantCargar();
void mantGuardar();
void mantRevisar();                // integra km y horas
void mantJson(JsonDocument& doc);

// ---------- Grabación ----------
bool grabIniciar(String& error);
void grabParar();
bool grabando();
uint32_t grabSegundos();
void grabRevisar();
void grabLista(JsonDocument& doc);
bool grabBorrar(const String& nombre);
String grabRuta(const String& nombre);
