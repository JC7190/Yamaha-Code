#include "obd.h"
#include "config.h"
#include "estado.h"
#include "kline.h"
#include "pids.h"

Obd OBD;
bool klineEsIso9141();

// Métodos de conexión en orden de prueba
// 0: rápida funcional 0x33 · 1..N: rápida física · N+1: lenta 5 baudios 0x33
static const int NUM_FISICAS = sizeof(DIRS_FISICAS);
static const int NUM_METODOS = 1 + NUM_FISICAS + 1;

void Obd::reiniciar() {
  conectado_ = false;
  fallos_ = 0;
  Bloqueo b;
  for (int i = 0; i < 256; i++) E.v[i] = ValorPid{false, false, 0, 0};
  E.conexion = DESCONECTADO;
}

void Obd::desconectar(const char* motivo) {
  Serial.printf("[OBD] Conexión perdida: %s\n", motivo);
  conectado_ = false;
  fallos_ = 0;
  Bloqueo b;
  E.conexion = DESCONECTADO;
  for (int i = 0; i < 256; i++) E.v[i].valido = false;
}

void Obd::respuestaOk() {
  fallos_ = 0;
  E.msUltimaRespuesta = millis();
}

void Obd::respuestaFallida() {
  if (++fallos_ >= 3) desconectar("3 peticiones seguidas sin respuesta");
}

bool Obd::probarMetodo(int m) {
  uint8_t kb1 = 0, kb2 = 0;
  bool ok;
  char proto[80], dir[24];
  if (m == 0) {
    ok = K.initRapida(DIR_FUNCIONAL, true, kb1, kb2);
    snprintf(proto, sizeof(proto), "ISO 14230-4 KWP2000 · init rápida · 10.400 baudios");
    snprintf(dir, sizeof(dir), "funcional 0x%02X", DIR_FUNCIONAL);
  } else if (m <= NUM_FISICAS) {
    uint8_t d = DIRS_FISICAS[m - 1];
    ok = K.initRapida(d, false, kb1, kb2);
    snprintf(proto, sizeof(proto), "ISO 14230-4 KWP2000 · init rápida · 10.400 baudios");
    snprintf(dir, sizeof(dir), "física 0x%02X", d);
  } else {
    ok = K.initLenta(DIR_FUNCIONAL, kb1, kb2);
    snprintf(proto, sizeof(proto), "%s · init lenta 5 baudios · 10.400 baudios",
             klineEsIso9141() ? "ISO 9141-2" : "ISO 14230-4 KWP2000");
    snprintf(dir, sizeof(dir), "funcional 0x%02X", DIR_FUNCIONAL);
  }
  if (!ok) return false;

  // Comprobación: la centralita debe responder al servicio 01 PID 00
  uint8_t req[] = {0x01, 0x00};
  Trama r[1];
  int n = K.peticion(req, 2, r, 1);
  if (n < 1 || r[0].d[0] != 0x41) {
    Serial.println("[OBD] La inicialización respondió pero el servicio 01 no. Se descarta el método.");
    return false;
  }
  char texto[100];
  snprintf(texto, sizeof(texto), "%s, keybytes %02X %02X", dir, kb1, kb2);
  Serial.printf("[OBD] *** MÉTODO QUE FUNCIONA: %s · %s · centralita 0x%02X ***\n", proto, texto,
                r[0].origen);
  Bloqueo b;
  strlcpy(E.ecu.protocolo, proto, sizeof(E.ecu.protocolo));
  snprintf(E.ecu.direccion, sizeof(E.ecu.direccion), "%s → 0x%02X", dir, r[0].origen);
  return true;
}

bool Obd::intentarConectar() {
  { Bloqueo b; E.conexion = CONECTANDO; }
  // Primero el método que funcionó la última vez
  if (metodoBueno_ >= 0 && probarMetodo(metodoBueno_)) {
    conectado_ = true;
  } else {
    for (int m = 0; m < NUM_METODOS && !conectado_; m++) {
      if (m == metodoBueno_) continue;
      if (probarMetodo(m)) {
        metodoBueno_ = m;
        conectado_ = true;
      }
      if (E.simulador) break;   // se ha cambiado a simulador mientras tanto
    }
  }
  if (!conectado_) {
    Bloqueo b;
    E.conexion = DESCONECTADO;
    Serial.println("[OBD] Ningún método de conexión ha funcionado. Se reintentará.");
    return false;
  }
  respuestaOk();
  lecturaInicial();
  Bloqueo b;
  E.conexion = conectado_ ? CONECTADO : DESCONECTADO;
  return conectado_;
}

void Obd::lecturaInicial() {
  detectarPids();
  leerMil();
  leerAverias(false);
  leerFoto();
  leerInfo();
  msUltimasAverias_ = millis();
}

bool Obd::detectarPids() {
  uint8_t admitidos = 0, conocidos = 0;
  nOtros_ = 0;
  for (int base = 0x00; base <= 0xE0; base += 0x20) {
    uint8_t req[] = {0x01, (uint8_t)base};
    Trama r[1];
    int n = K.peticion(req, 2, r, 1);
    if (n < 1 || r[0].d[0] != 0x41 || r[0].len < 6) {
      if (base == 0) {
        respuestaFallida();
        return false;
      }
      break;
    }
    respuestaOk();
    uint32_t mapa = ((uint32_t)r[0].d[2] << 24) | ((uint32_t)r[0].d[3] << 16) |
                    ((uint32_t)r[0].d[4] << 8) | r[0].d[5];
    Serial.printf("[OBD] PIDs admitidos %02X-%02X: %08lX\n", base + 1, base + 0x20, (unsigned long)mapa);
    for (int i = 0; i < 32; i++) {
      if (!(mapa & (0x80000000UL >> i))) continue;
      uint8_t pid = base + 1 + i;
      if ((pid & 0x1F) == 0) continue;   // 0x20, 0x40... son mapas, no datos
      admitidos++;
      const DefPid* d = buscarPid(pid);
      Bloqueo b;
      E.v[pid].soportado = d != nullptr;
      if (d) conocidos++;
    }
    if (!(mapa & 1)) break;   // no hay siguiente bloque
  }
  // Lista de sondeo: prioritarios primero, después el resto
  for (uint8_t p : PIDS_PRIORITARIOS)
    if (p != 0x0D && E.v[p].soportado) otros_[nOtros_++] = p;
  for (size_t i = 0; i < NUM_TABLA_PIDS && nOtros_ < sizeof(otros_); i++) {
    uint8_t p = TABLA_PIDS[i].id;
    bool yaEsta = (p == 0x0C || p == 0x0D);
    for (uint8_t q : PIDS_PRIORITARIOS) yaEsta |= (q == p);
    if (!yaEsta && E.v[p].soportado) otros_[nOtros_++] = p;
  }
  Serial.printf("[OBD] %d PIDs admitidos, %d se pueden mostrar\n", admitidos, conocidos);
  Bloqueo b;
  E.ecu.pidsAdmitidos = admitidos;
  E.ecu.pidsConocidos = conocidos;
  return true;
}

bool Obd::leerMil() {
  uint8_t req[] = {0x01, 0x01};
  Trama r[1];
  int n = K.peticion(req, 2, r, 1);
  if (n < 1 || r[0].d[0] != 0x41 || r[0].len < 3) {
    Serial.println("[OBD] PID 01 (estado del testigo) no disponible");
    return false;
  }
  bool mil = r[0].d[2] & 0x80;
  Serial.printf("[OBD] Testigo motor: %s, %d averías según la centralita\n", mil ? "ENCENDIDO" : "apagado",
                r[0].d[2] & 0x7F);
  Bloqueo b;
  E.mil = mil;
  E.milConocido = true;
  return true;
}

bool Obd::leerAveriasSvc(uint8_t svc, ListaAverias& lista) {
  lista.n = 0;
  Trama r[6];
  int n = K.peticion(&svc, 1, r, 6, 300);
  if (n < 1) {
    respuestaFallida();
    return false;
  }
  respuestaOk();
  if (r[0].d[0] == 0x7F) {
    Serial.printf("[OBD] Servicio %02X no admitido\n", svc);
    return svc == 0x07;   // averías pendientes es opcional
  }
  for (int f = 0; f < n; f++) {
    int inicio = 1;
    if ((r[f].len - 1) % 2 == 1) inicio = 2;   // algunas centralitas ponen un byte de cuenta
    for (int i = inicio; i + 1 < r[f].len; i += 2) {
      uint16_t raw = (r[f].d[i] << 8) | r[f].d[i + 1];
      if (raw == 0) continue;
      char cod[6];
      dtcATexto(raw, cod);
      bool repetido = false;
      for (int k = 0; k < lista.n; k++) repetido |= !strcmp(lista.cod[k], cod);
      if (!repetido && lista.n < MAX_AVERIAS) strcpy(lista.cod[lista.n++], cod);
    }
  }
  Serial.printf("[OBD] Servicio %02X: %d averías", svc, lista.n);
  for (int k = 0; k < lista.n; k++) Serial.printf(" %s", lista.cod[k]);
  Serial.println();
  return true;
}

bool Obd::leerAverias(bool porBorrado) {
  ListaAverias g = {}, p = {};
  if (!leerAveriasSvc(0x03, g)) return false;
  if (!leerAveriasSvc(0x07, p)) p.n = 0;
  Bloqueo b;
  bool cambio = g.n != E.guardadas.n || p.n != E.pendientes.n ||
                memcmp(&g, &E.guardadas, sizeof(g)) || memcmp(&p, &E.pendientes, sizeof(p));
  E.guardadas = g;
  E.pendientes = p;
  E.averiasLeidas = true;
  E.msLecturaAverias = millis();
  E.ultimaLecturaPorBorrado = porBorrado;
  if (cambio || porBorrado || E.versionAverias == 0) E.versionAverias++;
  return true;
}

void Obd::leerFoto() {
  FotoFallo f = {};
  uint8_t req[] = {0x02, 0x02, 0x00};
  Trama r[1];
  int n = K.peticion(req, 3, r, 1);
  if (n < 1 || r[0].d[0] != 0x42 || r[0].len < 5) {
    Serial.println("[OBD] Foto del fallo (servicio 02) no disponible");
    Bloqueo b;
    E.foto = f;
    return;
  }
  uint16_t raw = (r[0].d[3] << 8) | r[0].d[4];
  if (raw == 0) {
    Serial.println("[OBD] No hay foto del fallo guardada");
    Bloqueo b;
    E.foto = f;
    return;
  }
  dtcATexto(raw, f.dtc);
  f.valida = true;

  // PIDs admitidos en la foto
  uint32_t mapa = 0;
  uint8_t reqMapa[] = {0x02, 0x00, 0x00};
  n = K.peticion(reqMapa, 3, r, 1);
  bool hayMapa = n >= 1 && r[0].d[0] == 0x42 && r[0].len >= 7;
  if (hayMapa)
    mapa = ((uint32_t)r[0].d[3] << 24) | ((uint32_t)r[0].d[4] << 16) | ((uint32_t)r[0].d[5] << 8) | r[0].d[6];

  for (size_t i = 0; i < NUM_TABLA_PIDS && f.n < MAX_FOTO; i++) {
    uint8_t pid = TABLA_PIDS[i].id;
    bool admitido = hayMapa ? (pid <= 0x20 && (mapa & (0x80000000UL >> (pid - 1)))) || (pid > 0x20 && E.v[pid].soportado)
                            : E.v[pid].soportado;
    if (!admitido) continue;
    uint8_t q[] = {0x02, pid, 0x00};
    n = K.peticion(q, 3, r, 1);
    if (n < 1 || r[0].d[0] != 0x42 || r[0].len < 3 + TABLA_PIDS[i].bytes) continue;
    f.pid[f.n] = pid;
    f.valor[f.n] = decodificarPid(pid, &r[0].d[3]);
    f.n++;
  }
  Serial.printf("[OBD] Foto del fallo de %s con %d valores\n", f.dtc, f.n);
  Bloqueo b;
  E.foto = f;
}

static void textoServicio09(Trama* r, int n, char* out, size_t len, size_t ultimos) {
  char tmp[96];
  size_t k = 0;
  for (int f = 0; f < n; f++) {
    if (r[f].d[0] != 0x49) continue;
    for (int i = 3; i < r[f].len && k < sizeof(tmp) - 1; i++) {
      char c = r[f].d[i];
      if (c >= 0x20 && c <= 0x7E) tmp[k++] = c;
    }
  }
  tmp[k] = 0;
  const char* p = tmp;
  if (ultimos && k > ultimos) p = tmp + (k - ultimos);
  strlcpy(out, p, len);
}

void Obd::leerInfo() {
  uint8_t req[] = {0x09, 0x00};
  Trama r[8];
  int n = K.peticion(req, 2, r, 1);
  char vin[24] = "", calid[40] = "";
  if (n >= 1 && r[0].d[0] == 0x49 && r[0].len >= 6) {
    const uint8_t* m = &r[0].d[r[0].len - 4];
    uint32_t mapa = ((uint32_t)m[0] << 24) | ((uint32_t)m[1] << 16) | ((uint32_t)m[2] << 8) | m[3];
    Serial.printf("[OBD] Servicio 09 admitido, mapa %08lX\n", (unsigned long)mapa);
    if (mapa & (0x80000000UL >> 1)) {   // PID 02: VIN
      uint8_t q[] = {0x09, 0x02};
      n = K.peticion(q, 2, r, 8, 300);
      textoServicio09(r, n, vin, sizeof(vin), 17);
    }
    if (mapa & (0x80000000UL >> 3)) {   // PID 04: identificación de calibración
      uint8_t q[] = {0x09, 0x04};
      n = K.peticion(q, 2, r, 8, 300);
      textoServicio09(r, n, calid, sizeof(calid), 0);
    }
  } else {
    Serial.println("[OBD] Servicio 09 (VIN / calibración) no admitido");
  }
  Serial.printf("[OBD] VIN: '%s'  Calibración: '%s'\n", vin, calid);
  Bloqueo b;
  strlcpy(E.ecu.vin, vin, sizeof(E.ecu.vin));
  strlcpy(E.ecu.calid, calid, sizeof(E.ecu.calid));
}

bool Obd::leerPid(uint8_t pid) {
  uint8_t req[] = {0x01, pid};
  Trama r[1];
  int n = K.peticion(req, 2, r, 1);
  msUltimaPeticion_ = millis();
  if (n < 1) {
    respuestaFallida();
    return false;
  }
  respuestaOk();
  if (r[0].d[0] != 0x41 || r[0].d[1] != pid) return false;
  const DefPid* d = buscarPid(pid);
  if (!d || r[0].len < 2 + d->bytes) return false;
  float v = decodificarPid(pid, &r[0].d[2]);
  Bloqueo b;
  E.v[pid].valor = v;
  E.v[pid].valido = true;
  E.v[pid].ms = millis();
  return true;
}

void Obd::borrarAverias() {
  Serial.println("[OBD] Petición de borrado de averías");
  // Comprobar justo antes que el motor está parado
  bool rpmOk = E.v[0x0C].soportado && leerPid(0x0C);
  float rpm = E.v[0x0C].valor;
  if (!rpmOk) {
    eventoWeb("{\"t\":\"e\",\"e\":\"borrado\",\"ok\":false,\"msg\":\"No se pueden leer las revoluciones: no se borra por seguridad.\"}");
    return;
  }
  if (rpm > 0.5f) {
    eventoWeb("{\"t\":\"e\",\"e\":\"borrado\",\"ok\":false,\"msg\":\"El motor está en marcha. Páralo (con el contacto puesto) y vuelve a intentarlo.\"}");
    return;
  }
  uint8_t req = 0x04;
  Trama r[1];
  int n = K.peticion(&req, 1, r, 1, 1000);
  if (n >= 1 && r[0].d[0] == 0x44) {
    Serial.println("[OBD] Averías borradas");
    respuestaOk();
    delay(200);
    leerMil();
    leerAverias(true);
    leerFoto();
    eventoWeb("{\"t\":\"e\",\"e\":\"borrado\",\"ok\":true,\"msg\":\"Averías borradas en la centralita.\"}");
  } else {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "{\"t\":\"e\",\"e\":\"borrado\",\"ok\":false,\"msg\":\"La centralita no ha aceptado el borrado%s.\"}",
             (n >= 1 && r[0].d[0] == 0x7F) ? " (respuesta negativa)" : " (sin respuesta)");
    eventoWeb(msg);
  }
}

uint8_t Obd::siguientePid() {
  ranura_++;
  bool hayRpm = E.v[0x0C].soportado, hayVel = E.v[0x0D].soportado;
  if (ranura_ % 2 == 0 && hayRpm) return 0x0C;            // la mitad de las lecturas, rpm
  if (E.prioridadVelocidad && hayVel) return 0x0D;        // midiendo 0–50: rpm y velocidad
  if ((ranura_ / 2) % 2 == 0 && hayVel) return 0x0D;      // una cuarta parte, velocidad
  if (nOtros_ == 0) return hayRpm ? 0x0C : (hayVel ? 0x0D : 0);
  idxOtros_ = (idxOtros_ + 1) % nOtros_;
  return otros_[idxOtros_];
}

void Obd::paso() {
  if (!conectado_) {
    // Reintento de conexión cada 5 s
    if (millis() - msUltimoIntento_ < 5000 && msUltimoIntento_ != 0) {
      delay(50);
      return;
    }
    msUltimoIntento_ = millis();
    intentarConectar();
    return;
  }

  Orden o = E.orden;
  if (o != ORD_NINGUNA) {
    E.orden = ORD_NINGUNA;
    if (o == ORD_BORRAR_AVERIAS) borrarAverias();
    if (o == ORD_LEER_AVERIAS) {
      leerMil();
      leerAverias(false);
      leerFoto();
    }
    return;
  }

  if (millis() - msUltimasAverias_ > 30000) {
    msUltimasAverias_ = millis();
    leerMil();
    leerAverias(false);
    return;
  }

  uint32_t periodo = 1000 / LECTURAS_POR_SEG;
  uint32_t desde = millis() - msUltimaPeticion_;
  if (desde < periodo) {
    delay(periodo - desde);
    return;
  }
  uint8_t pid = siguientePid();
  if (pid) {
    leerPid(pid);
  } else if (millis() - K.msUltimoEnvio() > T_KEEPALIVE) {
    uint8_t tp[] = {0x3E, 0x01};   // TesterPresent para mantener la sesión
    Trama r[1];
    if (K.peticion(tp, 2, r, 1) >= 1) respuestaOk(); else respuestaFallida();
    msUltimaPeticion_ = millis();
  } else {
    delay(20);
  }
}
