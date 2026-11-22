// Grabación de trayectos en CSV (una fila por segundo).
#include "almacen.h"
#include "config.h"
#include "estado.h"

static File fich;
static bool activo = false;
static uint32_t msInicio = 0, msUltimaFila = 0, filas = 0;
static bool avisoLleno = false;

// Columnas del CSV: PID y nombre de columna
static const struct {
  uint8_t pid;
  const char* col;
  uint8_t dec;
} COLS[] = {
    {0x0C, "rpm", 0},          {0x0D, "velocidad_kmh", 0}, {0x05, "temp_motor_c", 0},
    {0x0F, "temp_aire_c", 0},  {0x0B, "presion_adm_kpa", 0}, {0x11, "acelerador_pct", 0},
    {0x04, "carga_pct", 0},    {0x0E, "avance_grados", 1}, {0x14, "lambda_v", 3},
    {0x42, "bateria_v", 2},
};

String grabRuta(const String& nombre) { return rutaBase() + "/viajes/" + nombre; }

bool grabando() { return activo; }
uint32_t grabSegundos() { return activo ? (millis() - msInicio) / 1000 : 0; }

bool grabIniciar(String& error) {
  if (activo) return true;
  if (fsLibre() < FS_AVISO_LIBRE) {
    error = "Memoria casi llena: borra algún trayecto antes de grabar.";
    return false;
  }
  char nombre[32];
  if (horaValida()) {
    snprintf(nombre, sizeof(nombre), "%lu.csv", (unsigned long)ahoraEpoch());
  } else {
    error = "Falta la fecha y la hora: abre la web desde el iPhone para enviarla.";
    return false;
  }
  fich = LittleFS.open(grabRuta(nombre), "w");
  if (!fich) {
    error = "No se puede crear el archivo.";
    return false;
  }
  fich.print("t_s");
  for (auto& c : COLS) {
    fich.print(',');
    fich.print(c.col);
  }
  fich.print('\n');
  activo = true;
  avisoLleno = false;
  msInicio = millis();
  msUltimaFila = 0;
  filas = 0;
  Serial.printf("[GRAB] Grabando en %s\n", nombre);
  return true;
}

void grabParar() {
  if (!activo) return;
  fich.close();
  activo = false;
  fsCambiado();
  Serial.printf("[GRAB] Grabación terminada: %lu filas\n", (unsigned long)filas);
}

void grabRevisar() {
  if (!activo) return;
  uint32_t ahora = millis();
  if (ahora - msUltimaFila < GRAB_INTERVALO_MS) return;
  msUltimaFila = ahora;

  size_t libre = fsLibre();
  if (libre < FS_MINIMO_LIBRE) {
    grabParar();
    eventoWeb("{\"t\":\"e\",\"e\":\"grab\",\"ok\":false,\"msg\":\"Memoria llena: la grabación se ha detenido.\"}");
    return;
  }
  if (libre < FS_AVISO_LIBRE && !avisoLleno) {
    avisoLleno = true;
    eventoWeb("{\"t\":\"e\",\"e\":\"grab\",\"ok\":true,\"msg\":\"Queda poca memoria: la grabación se detendrá pronto.\"}");
  }

  char linea[160];
  int n = snprintf(linea, sizeof(linea), "%.1f", (ahora - msInicio) / 1000.0f);
  {
    Bloqueo b;
    bool conectado = E.conexion == CONECTADO;
    for (auto& c : COLS) {
      const ValorPid& v = E.v[c.pid];
      if (conectado && v.soportado && v.valido && ahora - v.ms < 5000)
        n += snprintf(linea + n, sizeof(linea) - n, ",%.*f", c.dec, v.valor);
      else
        n += snprintf(linea + n, sizeof(linea) - n, ",");
    }
  }
  linea[n++] = '\n';
  fich.write((const uint8_t*)linea, n);
  if (++filas % 10 == 0) fich.flush();
}

// Duración: se lee el tiempo de la última fila
static float duracion(File& f) {
  size_t tam = f.size();
  if (tam < 4) return 0;
  size_t desde = tam > 160 ? tam - 160 : 0;
  f.seek(desde);
  char buf[161];
  size_t n = f.read((uint8_t*)buf, 160);
  buf[n] = 0;
  // Buscar el comienzo de la última línea completa
  int fin = n - 1;
  while (fin > 0 && (buf[fin] == '\n' || buf[fin] == '\r')) fin--;
  int ini = fin;
  while (ini > 0 && buf[ini - 1] != '\n') ini--;
  if (!isdigit((unsigned char)buf[ini])) return 0;
  return atof(buf + ini);
}

void grabLista(JsonDocument& doc) {
  JsonArray a = doc["viajes"].to<JsonArray>();
  File dir = LittleFS.open(rutaBase() + "/viajes");
  if (dir && dir.isDirectory()) {
    File f = dir.openNextFile();
    while (f) {
      String nombre = f.name();
      int barra = nombre.lastIndexOf('/');
      if (barra >= 0) nombre = nombre.substring(barra + 1);
      if (nombre.endsWith(".csv")) {
        JsonObject o = a.add<JsonObject>();
        o["nombre"] = nombre;
        o["inicio"] = (uint32_t)strtoul(nombre.c_str(), nullptr, 10);
        o["bytes"] = (uint32_t)f.size();
        bool esActual = activo && String(fich.name()).endsWith(nombre);
        o["seg"] = esActual ? (float)grabSegundos() : duracion(f);
        o["grabando"] = esActual;
      }
      f.close();
      f = dir.openNextFile();
    }
  }
  doc["libre"] = (uint32_t)fsLibre();
  doc["total"] = (uint32_t)fsTotal();
  doc["aviso"] = fsLibre() < FS_AVISO_LIBRE;
  doc["grabando"] = activo;
  doc["seg"] = grabSegundos();
}

bool grabBorrar(const String& nombre) {
  if (nombre.indexOf('/') >= 0 || nombre.indexOf("..") >= 0 || !nombre.endsWith(".csv")) return false;
  if (activo && String(fich.name()).endsWith(nombre)) return false;
  bool ok = LittleFS.remove(grabRuta(nombre));
  fsCambiado();
  return ok;
}
