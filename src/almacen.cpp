#include "almacen.h"
#include "config.h"
#include "estado.h"
#include <sys/time.h>

Ajustes AJ = {TEMP_LIMITE_DEFECTO, SIMULADOR_POR_DEFECTO != 0};

bool almacenIniciar() {
  if (!LittleFS.begin(false)) {
    Serial.println("[FS] No se puede montar LittleFS. ¿Has subido los archivos con 'pio run -t uploadfs'?");
    Serial.println("[FS] Se formatea para poder guardar datos (la web no estará hasta que la subas).");
    if (!LittleFS.begin(true)) return false;
  }
  LittleFS.mkdir("/datos");
  LittleFS.mkdir("/datos/viajes");
  LittleFS.mkdir("/sim");
  LittleFS.mkdir("/sim/viajes");
  if (!LittleFS.exists("/www/index.html"))
    Serial.println("[FS] AVISO: falta /www/index.html. Sube la web con 'pio run -t uploadfs'.");
  Serial.printf("[FS] LittleFS: %u KB usados de %u KB\n", (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024));
  return true;
}

String rutaBase() { return AJ.simulador ? "/sim" : "/datos"; }

size_t fsTotal() { return LittleFS.totalBytes(); }
size_t fsLibre() { return LittleFS.totalBytes() - LittleFS.usedBytes(); }

bool horaValida() { return time(nullptr) > 1700000000; }
uint32_t ahoraEpoch() { return horaValida() ? (uint32_t)time(nullptr) : 0; }

bool leerJson(const String& ruta, JsonDocument& doc) {
  File f = LittleFS.open(ruta, "r");
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) {
    Serial.printf("[FS] %s dañado (%s)\n", ruta.c_str(), e.c_str());
    return false;
  }
  return true;
}

bool escribirJson(const String& ruta, const JsonDocument& doc) {
  String tmp = ruta + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  size_t n = serializeJson(doc, f);
  f.close();
  if (n == 0) return false;
  LittleFS.remove(ruta);
  return LittleFS.rename(tmp, ruta);
}

void ajustesCargar() {
  JsonDocument d;
  if (leerJson("/ajustes.json", d)) {
    AJ.tempLimite = d["tempLimite"] | TEMP_LIMITE_DEFECTO;
    AJ.simulador = d["simulador"] | (SIMULADOR_POR_DEFECTO != 0);
  }
  Serial.printf("[FS] Ajustes: límite temperatura %.0f °C, simulador %s\n", AJ.tempLimite,
                AJ.simulador ? "sí" : "no");
}

void ajustesGuardar() {
  JsonDocument d;
  d["tempLimite"] = AJ.tempLimite;
  d["simulador"] = AJ.simulador;
  escribirJson("/ajustes.json", d);
}

void historialReiniciarVista();

void almacenCambiarModo() {
  grabParar();
  recordsCargar();
  mantCargar();
  historialReiniciarVista();
}

// ======================= Historial de averías =======================

static uint32_t versionVista = 0;
void historialReiniciarVista() { versionVista = 0; }

static bool contiene(const ListaAverias& l, const char* c) {
  for (int i = 0; i < l.n; i++)
    if (!strcmp(l.cod[i], c)) return true;
  return false;
}

void historialRevisar() {
  if (!horaValida()) return;   // la hora la pone el iPhone al conectarse
  ListaAverias g, p;
  bool porBorrado;
  uint32_t msLectura;
  {
    Bloqueo b;
    if (!E.averiasLeidas || E.versionAverias == versionVista) return;
    versionVista = E.versionAverias;
    g = E.guardadas;
    p = E.pendientes;
    porBorrado = E.ultimaLecturaPorBorrado;
    msLectura = E.msLecturaAverias;
  }
  uint32_t t = ahoraEpoch() - (millis() - msLectura) / 1000;
  String ruta = rutaBase() + "/historial.json";
  JsonDocument doc;
  leerJson(ruta, doc);
  JsonArray h = doc["h"].as<JsonArray>();
  if (h.isNull()) h = doc["h"].to<JsonArray>();
  bool cambio = false;

  // Códigos activos que desaparecen: borrados
  for (JsonObject o : h) {
    if (strcmp(o["e"] | "", "a")) continue;
    const char* c = o["c"] | "";
    if (contiene(g, c) || contiene(p, c)) {
      if (contiene(g, c) && !strcmp(o["t"] | "", "p")) {   // pendiente que pasa a guardada
        o["t"] = "g";
        cambio = true;
      }
      continue;
    }
    o["e"] = "b";
    o["h"] = t;
    o["m"] = porBorrado ? "Borrada con este aparato" : "Ya no aparece en la centralita";
    cambio = true;
  }
  // Códigos nuevos
  auto anadir = [&](const ListaAverias& l, const char* tipo) {
    for (int i = 0; i < l.n; i++) {
      bool abierta = false;
      for (JsonObject o : h)
        if (!strcmp(o["e"] | "", "a") && !strcmp(o["c"] | "", l.cod[i])) abierta = true;
      if (abierta) continue;
      JsonObject o = h.add<JsonObject>();
      o["c"] = l.cod[i];
      o["t"] = tipo;
      o["d"] = t;
      o["e"] = "a";
      cambio = true;
      Serial.printf("[HIST] Nueva avería %s (%s)\n", l.cod[i], tipo);
    }
  };
  anadir(g, "g");
  anadir(p, "p");

  // Límite de 150 entradas: se eliminan las más antiguas ya borradas
  while (h.size() > 150) {
    size_t i = 0;
    for (; i < h.size(); i++)
      if (!strcmp(h[i]["e"] | "", "b")) break;
    h.remove(i < h.size() ? i : 0);
  }
  if (cambio) escribirJson(ruta, doc);
}

void historialJson(JsonDocument& doc) {
  if (!leerJson(rutaBase() + "/historial.json", doc)) doc["h"].to<JsonArray>();
}
