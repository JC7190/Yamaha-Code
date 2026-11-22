#include "sim.h"
#include "estado.h"
#include "pids.h"

Simulador SIM;

static const uint8_t PIDS_SIM[] = {0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x14, 0x1F, 0x33, 0x42};

static float ruido(float a) { return ((float)random(-1000, 1000) / 1000.0f) * a; }

void Simulador::ponerAverias(bool todas) {
  E.guardadas.n = 0;
  E.pendientes.n = 0;
  memset(&E.guardadas, 0, sizeof(E.guardadas));
  memset(&E.pendientes, 0, sizeof(E.pendientes));
  strcpy(E.guardadas.cod[E.guardadas.n++], "P0115");
  if (todas) strcpy(E.guardadas.cod[E.guardadas.n++], "P0122");
  strcpy(E.pendientes.cod[E.pendientes.n++], "P0130");
  E.mil = true;
  E.milConocido = true;
  E.averiasLeidas = true;
  E.msLecturaAverias = millis();
  E.ultimaLecturaPorBorrado = false;
  E.versionAverias++;

  FotoFallo& f = E.foto;
  memset(&f, 0, sizeof(f));
  f.valida = true;
  strcpy(f.dtc, "P0115");
  const uint8_t pids[] = {0x04, 0x05, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x42};
  const float vals[] = {38, -40, 62, 4250, 42, 18.5f, 31, 27, 13.9f};
  for (size_t i = 0; i < sizeof(pids); i++) {
    f.pid[f.n] = pids[i];
    f.valor[f.n] = vals[i];
    f.n++;
  }
}

void Simulador::iniciar() {
  Serial.println("[SIM] Modo simulador activo: datos de ejemplo, sin línea K");
  Bloqueo b;
  for (int i = 0; i < 256; i++) E.v[i] = ValorPid{false, false, 0, 0};
  for (uint8_t p : PIDS_SIM) E.v[p].soportado = true;
  strlcpy(E.ecu.vin, "SIMULADOR-NMAX125", sizeof(E.ecu.vin));
  strlcpy(E.ecu.calid, "SIM-BALD-0001", sizeof(E.ecu.calid));
  strlcpy(E.ecu.protocolo, "Simulador (sin moto)", sizeof(E.ecu.protocolo));
  strlcpy(E.ecu.direccion, "—", sizeof(E.ecu.direccion));
  E.ecu.pidsAdmitidos = sizeof(PIDS_SIM);
  E.ecu.pidsConocidos = sizeof(PIDS_SIM);
  ponerAverias(true);
  E.conexion = CONECTADO;
  msInicio_ = millis();
  msBorrado_ = 0;
  temp_ = 25;
}

void Simulador::paso() {
  if (millis() - msUltimo_ < 200) {
    delay(10);
    return;
  }
  float dt = (millis() - msUltimo_) / 1000.0f;
  msUltimo_ = millis();
  E.msUltimaRespuesta = millis();

  // Ciclo de 100 s: parado, ralentí, aceleración, crucero, frenada, ralentí, parado
  float t = fmodf((millis() - msInicio_) / 1000.0f, 100.0f);
  uint32_t ciclo = (millis() - msInicio_) / 100000;
  bool caliente = (ciclo % 4) == 3;   // uno de cada cuatro ciclos se calienta de más (prueba de alerta)
  float rpm, vel, acel, map;
  if (t < 12) {
    rpm = 0; vel = 0; acel = 0; map = 101;
  } else if (t < 22) {
    rpm = 1700 + ruido(40); vel = 0; acel = 0; map = 34;
  } else if (t < 34) {
    float a = t - 22;
    vel = 85.0f * (1 - expf(-a / 5.0f));
    rpm = min(7600.0f, 1700 + a * 2500) + ruido(60);
    acel = 95; map = 96;
  } else if (t < 55) {
    vel = 76 + 4 * sinf(t / 3) + ruido(0.5f);
    rpm = 6200 + vel * 8 + ruido(50);
    acel = 38 + ruido(3); map = 62;
  } else if (t < 68) {
    float a = t - 55;
    vel = max(0.0f, 76 - a * 6.5f);
    rpm = vel > 20 ? 3000 + vel * 30 : 1700;
    acel = 0; map = 28;
  } else if (t < 90) {
    rpm = 1700 + ruido(40); vel = 0; acel = 0; map = 34;
  } else {
    rpm = 0; vel = 0; acel = 0; map = 101;
  }
  bool enMarcha = rpm > 0;
  float objetivo = enMarcha ? (caliente && t > 34 && t < 60 ? 108 : 88) : temp_ - 0.2f;
  temp_ += (objetivo - temp_) * min(1.0f, dt * (caliente ? 0.25f : 0.05f));

  Bloqueo b;
  if (!E.simulador) return;   // se acaba de pasar a modo moto
  uint32_t ahora = millis();
  auto pon = [&](uint8_t pid, float v) {
    E.v[pid].valor = v;
    E.v[pid].valido = true;
    E.v[pid].ms = ahora;
  };
  pon(0x0C, rpm);
  pon(0x0D, roundf(vel));
  pon(0x05, roundf(temp_));
  pon(0x0F, 31 + ruido(0.5f));
  pon(0x0B, map);
  pon(0x11, acel);
  pon(0x04, enMarcha ? min(100.0f, map * 0.9f + ruido(2)) : 0);
  pon(0x0E, enMarcha ? 10 + rpm / 400 + ruido(1) : 0);
  pon(0x14, enMarcha ? 0.45f + 0.4f * sinf(ahora / 300.0f) : 0);
  pon(0x1F, enMarcha ? (ahora - msInicio_) / 1000 : 0);
  pon(0x33, 101);
  pon(0x42, enMarcha ? 14.1f + ruido(0.1f) : 12.6f + ruido(0.05f));

  // Órdenes de la web
  Orden o = E.orden;
  if (o == ORD_BORRAR_AVERIAS) {
    E.orden = ORD_NINGUNA;
    if (rpm > 0.5f) {
      eventoWeb("{\"t\":\"e\",\"e\":\"borrado\",\"ok\":false,\"msg\":\"El motor está en marcha. Páralo (con el contacto puesto) y vuelve a intentarlo.\"}");
    } else {
      memset(&E.guardadas, 0, sizeof(E.guardadas));
      memset(&E.pendientes, 0, sizeof(E.pendientes));
      memset(&E.foto, 0, sizeof(E.foto));
      E.mil = false;
      E.msLecturaAverias = ahora;
      E.ultimaLecturaPorBorrado = true;
      E.versionAverias++;
      msBorrado_ = ahora;
      eventoWeb("{\"t\":\"e\",\"e\":\"borrado\",\"ok\":true,\"msg\":\"Averías borradas (simulador). Volverán a aparecer en 3 minutos.\"}");
    }
  } else if (o == ORD_LEER_AVERIAS) {
    E.orden = ORD_NINGUNA;
    E.msLecturaAverias = ahora;
    E.versionAverias++;
  }
  if (msBorrado_ && ahora - msBorrado_ > 180000) {
    msBorrado_ = 0;
    ponerAverias(false);
  }
}
