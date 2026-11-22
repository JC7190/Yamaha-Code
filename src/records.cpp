// Récords: velocidad máxima, revoluciones máximas y 0–50 km/h.
#include "almacen.h"
#include "estado.h"

Records REC = {};
static bool recCambiado = false;
static uint32_t msGuardado = 0;

static uint8_t estado050 = 0;
static uint32_t msArmado = 0, msUltimaVel = 0, msParado = 0, msInicio = 0, msPrev = 0;
static float vPrev = 0;

void recordsCargar() {
  REC = {};
  JsonDocument d;
  if (leerJson(rutaBase() + "/records.json", d)) {
    REC.vmax = d["vmax"] | 0.0f;
    REC.rpmMax = d["rpmMax"] | 0.0f;
    REC.t050 = d["t050"] | 0.0f;
    REC.fVmax = d["fVmax"] | 0;
    REC.fRpm = d["fRpm"] | 0;
    REC.f050 = d["f050"] | 0;
  }
}

void recordsGuardar() {
  JsonDocument d;
  d["vmax"] = REC.vmax;
  d["rpmMax"] = REC.rpmMax;
  d["t050"] = REC.t050;
  d["fVmax"] = REC.fVmax;
  d["fRpm"] = REC.fRpm;
  d["f050"] = REC.f050;
  escribirJson(rutaBase() + "/records.json", d);
  recCambiado = false;
  msGuardado = millis();
}

void recordsReiniciar() {
  REC = {};
  recordsGuardar();
}

void medir050Iniciar() {
  estado050 = 1;
  msArmado = millis();
  E.prioridadVelocidad = true;
}

void medir050Cancelar() {
  estado050 = 0;
  E.prioridadVelocidad = false;
}

uint8_t medir050Estado() { return estado050; }

static void fin050(const char* json) {
  eventoWeb(json);
  medir050Cancelar();
}

void recordsRevisar() {
  float rpm, vel;
  bool okR, okV;
  uint32_t msVel;
  {
    Bloqueo b;
    okR = E.conexion == CONECTADO && E.v[0x0C].soportado && E.v[0x0C].valido;
    okV = E.conexion == CONECTADO && E.v[0x0D].soportado && E.v[0x0D].valido;
    rpm = E.v[0x0C].valor;
    vel = E.v[0x0D].valor;
    msVel = E.v[0x0D].ms;
  }
  if (okR && rpm > REC.rpmMax && rpm < 16000) {
    REC.rpmMax = rpm;
    REC.fRpm = ahoraEpoch();
    recCambiado = true;
  }
  if (okV && vel > REC.vmax && vel < 200) {
    REC.vmax = vel;
    REC.fVmax = ahoraEpoch();
    recCambiado = true;
  }
  if (recCambiado && millis() - msGuardado > 30000) recordsGuardar();

  // ----- Medición 0–50 km/h -----
  if (estado050 == 0) return;
  if (millis() - msArmado > 5 * 60000UL && estado050 < 3) {
    fin050("{\"t\":\"e\",\"e\":\"050\",\"ok\":false,\"msg\":\"Medición cancelada: no se ha arrancado en 5 minutos.\"}");
    return;
  }
  if (!okV) {
    if (!E.v[0x0D].soportado && E.conexion == CONECTADO)
      fin050("{\"t\":\"e\",\"e\":\"050\",\"ok\":false,\"msg\":\"La centralita no da la velocidad: no se puede medir.\"}");
    return;
  }
  if (msVel == msUltimaVel) return;   // solo con lecturas nuevas
  msUltimaVel = msVel;

  if (estado050 == 1 || estado050 == 2) {
    if (vel < 0.5f) {
      estado050 = 2;
      msParado = msVel;
    } else if (estado050 == 2) {
      // Arranque: se toma el punto medio entre la última lectura parada y la primera en marcha
      msInicio = msParado + (msVel - msParado) / 2;
      estado050 = 3;
      vPrev = vel;
      msPrev = msVel;
    }
    if (estado050 != 3) return;
  }
  if (estado050 == 3) {
    if (vel >= 50) {
      float frac = vel > vPrev ? (50 - vPrev) / (vel - vPrev) : 1;
      float msCruce = msPrev + frac * (msVel - msPrev);
      float seg = (msCruce - msInicio) / 1000.0f;
      bool record = REC.t050 == 0 || seg < REC.t050;
      if (record) {
        REC.t050 = seg;
        REC.f050 = ahoraEpoch();
        recordsGuardar();
      }
      char j[160];
      snprintf(j, sizeof(j), "{\"t\":\"e\",\"e\":\"050\",\"ok\":true,\"seg\":%.2f,\"record\":%s}", seg,
               record ? "true" : "false");
      fin050(j);
      return;
    }
    if (vel < 0.5f) {   // se ha vuelto a parar: se reinicia la espera
      estado050 = 2;
      msParado = msVel;
      return;
    }
    if (msVel - msInicio > 30000) {
      fin050("{\"t\":\"e\",\"e\":\"050\",\"ok\":false,\"msg\":\"Medición cancelada: más de 30 s sin llegar a 50 km/h.\"}");
      return;
    }
    vPrev = vel;
    msPrev = msVel;
  }
}
