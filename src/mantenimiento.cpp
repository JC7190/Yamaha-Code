// Mantenimiento: kilómetros (de la centralita o calculados con la velocidad),
// horas de motor y avisos de aceite, correa y filtro de aire.
#include "almacen.h"
#include "estado.h"

Mant MANT;
const char* NOMBRES_MANT[NUM_MANT] = {"Aceite del motor", "Correa de transmisión", "Filtro de aire"};
// Intervalos iniciales orientativos: compruébalos en tu manual y cámbialos desde la web.
static const float INTERVALOS_DEFECTO[NUM_MANT] = {6000, 24000, 12000};

static uint32_t msUltimo = 0, msGuardado = 0;
static bool cambiado = false;
static bool motorEnMarcha = false;

void mantCargar() {
  MANT = {};
  for (int i = 0; i < NUM_MANT; i++) MANT.item[i].intervaloKm = INTERVALOS_DEFECTO[i];
  JsonDocument d;
  if (leerJson(rutaBase() + "/mant.json", d)) {
    MANT.odometroKm = d["km"] | 0.0f;
    MANT.horasMotor = d["horas"] | 0.0f;
    MANT.fechaOdometro = d["fKm"] | 0;
    JsonArray a = d["items"];
    for (int i = 0; i < NUM_MANT && i < (int)a.size(); i++) {
      MANT.item[i].intervaloKm = a[i]["int"] | INTERVALOS_DEFECTO[i];
      MANT.item[i].hechoKm = a[i]["km"] | 0.0f;
      MANT.item[i].hechoHoras = a[i]["h"] | 0.0f;
      MANT.item[i].fecha = a[i]["f"] | 0;
    }
  }
  msUltimo = millis();
}

void mantGuardar() {
  JsonDocument d;
  d["km"] = MANT.odometroKm;
  d["horas"] = MANT.horasMotor;
  d["fKm"] = MANT.fechaOdometro;
  JsonArray a = d["items"].to<JsonArray>();
  for (int i = 0; i < NUM_MANT; i++) {
    JsonObject o = a.add<JsonObject>();
    o["int"] = MANT.item[i].intervaloKm;
    o["km"] = MANT.item[i].hechoKm;
    o["h"] = MANT.item[i].hechoHoras;
    o["f"] = MANT.item[i].fecha;
  }
  escribirJson(rutaBase() + "/mant.json", d);
  cambiado = false;
  msGuardado = millis();
}

void mantRevisar() {
  uint32_t ahora = millis();
  float dt = (ahora - msUltimo) / 1000.0f;
  if (dt < 0.2f) return;
  msUltimo = ahora;
  if (dt > 5) return;   // hueco largo (reconexión): no se integra

  bool okR, okV, okOdo;
  float rpm, vel, odo;
  {
    Bloqueo b;
    bool c = E.conexion == CONECTADO;
    okR = c && E.v[0x0C].valido && ahora - E.v[0x0C].ms < 3000;
    okV = c && E.v[0x0D].valido && ahora - E.v[0x0D].ms < 3000;
    okOdo = c && E.v[0xA6].valido && ahora - E.v[0xA6].ms < 60000;
    rpm = E.v[0x0C].valor;
    vel = E.v[0x0D].valor;
    odo = E.v[0xA6].valor;
  }
  bool enMarcha = okR && rpm > 300;
  if (enMarcha) {
    MANT.horasMotor += dt / 3600.0f;
    cambiado = true;
  }
  if (okOdo && odo > 0) {
    if (fabsf(odo - MANT.odometroKm) > 0.05f) cambiado = true;
    MANT.odometroKm = odo;   // la centralita da los kilómetros
  } else if (okV && vel > 0 && vel < 200) {
    MANT.odometroKm += vel * dt / 3600.0f;
    cambiado = true;
  }
  // Se guarda al parar el motor y, en marcha, cada 5 minutos
  bool parado = motorEnMarcha && !enMarcha;
  motorEnMarcha = enMarcha;
  if (cambiado && (parado || ahora - msGuardado > 300000UL)) mantGuardar();
}

void mantJson(JsonDocument& d) {
  bool odoCentralita;
  {
    Bloqueo b;
    odoCentralita = E.v[0xA6].soportado && E.conexion == CONECTADO;
  }
  d["km"] = MANT.odometroKm;
  d["horas"] = MANT.horasMotor;
  d["fKm"] = MANT.fechaOdometro;
  d["kmCentralita"] = odoCentralita;
  JsonArray a = d["items"].to<JsonArray>();
  for (int i = 0; i < NUM_MANT; i++) {
    JsonObject o = a.add<JsonObject>();
    o["nombre"] = NOMBRES_MANT[i];
    o["int"] = MANT.item[i].intervaloKm;
    o["km"] = MANT.item[i].hechoKm;
    o["h"] = MANT.item[i].hechoHoras;
    o["f"] = MANT.item[i].fecha;
  }
}
