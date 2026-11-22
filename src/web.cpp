#include "web.h"
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <sys/time.h>
#include "actweb.h"
#include "almacen.h"
#include "config.h"
#include "estado.h"
#include "pids.h"

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static volatile bool otaEnCurso = false;
static volatile uint32_t msOtaDatos = 0;   // última vez que llegaron datos de una subida
static volatile bool reinicioPendiente = false;
static volatile bool cambioModo = false;
static String otaError;

// Si una subida se corta, deja de contar como «en curso» a los 30 s
bool webOtaEnCurso() { return otaEnCurso && millis() - msOtaDatos < 30000; }
bool webReinicioPendiente() { return reinicioPendiente; }
bool webCambioModoPendiente() {
  bool c = cambioModo;
  cambioModo = false;
  return c;
}

// ------------------------------------------------------------------ utilidades

static void enviarJson(AsyncWebServerRequest* r, const JsonDocument& d, int codigo = 200) {
  String s;
  serializeJson(d, s);
  AsyncWebServerResponse* resp = r->beginResponse(codigo, "application/json", s);
  resp->addHeader("Cache-Control", "no-store");
  r->send(resp);
}

static void enviarOk(AsyncWebServerRequest* r, const char* msg = nullptr) {
  JsonDocument d;
  d["ok"] = true;
  if (msg) d["msg"] = msg;
  enviarJson(r, d);
}

static void enviarError(AsyncWebServerRequest* r, const String& msg, int codigo = 400) {
  JsonDocument d;
  d["ok"] = false;
  d["msg"] = msg;
  enviarJson(r, d, codigo);
}

static void postJson(const char* ruta, std::function<void(AsyncWebServerRequest*, JsonVariant&)> fn) {
  AsyncCallbackJsonWebHandler* h = new AsyncCallbackJsonWebHandler(ruta, fn);
  h->setMethod(HTTP_POST);
  server.addHandler(h);
}

static void postSimple(const char* ruta, ArRequestHandlerFunction fn) { server.on(ruta, HTTP_POST, fn); }

static void listaAJson(const ListaAverias& l, JsonArray a) {
  for (int i = 0; i < l.n; i++) a.add(l.cod[i]);
}

// ------------------------------------------------------------------ datos en vivo

static void enviarVivo() {
  if (ws.count() == 0) return;
  static char buf[1400];
  int n;
  uint32_t ahora = millis();
  {
    Bloqueo b;
    n = snprintf(buf, sizeof(buf),
                 "{\"t\":\"v\",\"c\":%d,\"s\":%d,\"m\":%d,\"mk\":%d,\"n\":%d,\"np\":%d,\"dv\":%lu,",
                 E.conexion, E.simulador ? 1 : 0, E.mil ? 1 : 0, E.milConocido ? 1 : 0, E.guardadas.n,
                 E.pendientes.n, (unsigned long)E.versionAverias);
    n += snprintf(buf + n, sizeof(buf) - n, "\"v\":{");
    bool primero = true;
    if (E.conexion == CONECTADO) {
      for (size_t i = 0; i < NUM_TABLA_PIDS && n < (int)sizeof(buf) - 220; i++) {
        uint8_t p = TABLA_PIDS[i].id;
        const ValorPid& v = E.v[p];
        if (!v.soportado || !v.valido || ahora - v.ms > 10000 || !isfinite(v.valor)) continue;
        n += snprintf(buf + n, sizeof(buf) - n, "%s\"%d\":%.*f", primero ? "" : ",", p,
                      TABLA_PIDS[i].decimales, v.valor);
        primero = false;
      }
    }
  }
  n += snprintf(buf + n, sizeof(buf) - n,
                "},\"g\":%d,\"gs\":%lu,\"fa\":%d,\"z\":%d,\"tl\":%.0f,\"hv\":%d}", grabando() ? 1 : 0,
                (unsigned long)grabSegundos(), fsLibre() < FS_AVISO_LIBRE ? 1 : 0, medir050Estado(),
                AJ.tempLimite, horaValida() ? 1 : 0);
  ws.textAll(buf, n);
}

void webBucle() {
  static uint32_t msVivo = 0, msLimpieza = 0;
  if (millis() - msVivo >= 200) {
    msVivo = millis();
    enviarVivo();
  }
  char ev[200];
  while (sacarEventoWeb(ev, sizeof(ev))) ws.textAll(ev);
  if (millis() - msLimpieza > 1000) {
    msLimpieza = millis();
    ws.cleanupClients(3);
  }
}

// ------------------------------------------------------------------ rutas

static void rutasApi() {
  // Estado general
  server.on("/api/estado", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    {
      Bloqueo b;
      d["conexion"] = E.conexion;
      d["simulador"] = E.simulador;
      d["protocolo"] = E.ecu.protocolo;
    }
    d["version"] = VERSION_FIRMWARE;
    d["horaValida"] = horaValida();
    d["tempLimite"] = AJ.tempLimite;
    d["bateriaBaja"] = BATERIA_BAJA_V;
    d["fsLibre"] = (uint32_t)fsLibre();
    d["fsTotal"] = (uint32_t)fsTotal();
    enviarJson(r, d);
  });

  // Fecha y hora desde el iPhone (segundos UTC)
  postJson("/api/hora", [](AsyncWebServerRequest* r, JsonVariant& j) {
    double epoch = j["epoch"] | 0.0;
    if (epoch < 1700000000) return enviarError(r, "Hora no válida");
    struct timeval tv;
    tv.tv_sec = (time_t)epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
    Serial.printf("[WEB] Hora recibida del iPhone: %lu\n", (unsigned long)tv.tv_sec);
    enviarOk(r);
  });

  // Lista de PIDs disponibles
  server.on("/api/pids", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    JsonArray a = d["pids"].to<JsonArray>();
    Bloqueo b;
    for (size_t i = 0; i < NUM_TABLA_PIDS; i++) {
      const DefPid& p = TABLA_PIDS[i];
      if (!E.v[p.id].soportado) continue;
      JsonObject o = a.add<JsonObject>();
      o["id"] = p.id;
      o["nombre"] = p.nombre;
      o["unidad"] = p.unidad;
      o["dec"] = p.decimales;
    }
    enviarJson(r, d);
  });

  // Averías y foto del fallo
  server.on("/api/averias", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    Bloqueo b;
    d["conexion"] = E.conexion;
    d["leidas"] = E.averiasLeidas;
    d["mil"] = E.mil;
    d["milConocido"] = E.milConocido;
    d["version"] = E.versionAverias;
    d["hace"] = E.averiasLeidas ? (millis() - E.msLecturaAverias) / 1000 : 0;
    listaAJson(E.guardadas, d["guardadas"].to<JsonArray>());
    listaAJson(E.pendientes, d["pendientes"].to<JsonArray>());
    JsonObject f = d["foto"].to<JsonObject>();
    f["valida"] = E.foto.valida;
    if (E.foto.valida) {
      f["dtc"] = E.foto.dtc;
      JsonArray vs = f["valores"].to<JsonArray>();
      for (int i = 0; i < E.foto.n; i++) {
        const DefPid* p = buscarPid(E.foto.pid[i]);
        if (!p) continue;
        JsonObject o = vs.add<JsonObject>();
        o["id"] = p->id;
        o["nombre"] = p->nombre;
        o["unidad"] = p->unidad;
        o["dec"] = p->decimales;
        o["v"] = E.foto.valor[i];
      }
    }
    enviarJson(r, d);
  });

  postSimple("/api/averias/leer", [](AsyncWebServerRequest* r) {
    if (E.conexion != CONECTADO) return enviarError(r, "Sin conexión con la centralita");
    E.orden = ORD_LEER_AVERIAS;
    enviarOk(r);
  });

  postJson("/api/averias/borrar", [](AsyncWebServerRequest* r, JsonVariant& j) {
    if (!(j["confirmar"] | false)) return enviarError(r, "Falta la confirmación");
    bool ok;
    float rpm = valorPid(0x0C, &ok);
    if (E.conexion != CONECTADO) return enviarError(r, "Sin conexión con la centralita");
    if (!ok) return enviarError(r, "No se conocen las revoluciones: no se puede borrar por seguridad.");
    if (rpm > 0.5f) return enviarError(r, "El motor está en marcha. Páralo (con el contacto puesto) para borrar.");
    E.orden = ORD_BORRAR_AVERIAS;
    enviarOk(r, "Borrando...");
  });

  server.on("/api/historial", HTTP_GET, [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    JsonDocument d;
    historialJson(d);
    enviarJson(r, d);
  });

  // Datos de la centralita
  server.on("/api/ecu", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    Bloqueo b;
    d["conexion"] = E.conexion;
    d["vin"] = E.ecu.vin;
    d["calid"] = E.ecu.calid;
    d["protocolo"] = E.ecu.protocolo;
    d["direccion"] = E.ecu.direccion;
    d["pidsAdmitidos"] = E.ecu.pidsAdmitidos;
    d["pidsConocidos"] = E.ecu.pidsConocidos;
    d["version"] = VERSION_FIRMWARE;
    enviarJson(r, d);
  });

  // Ajustes
  server.on("/api/ajustes", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    d["tempLimite"] = AJ.tempLimite;
    d["simulador"] = AJ.simulador;
    enviarJson(r, d);
  });
  postJson("/api/ajustes", [](AsyncWebServerRequest* r, JsonVariant& j) {
    BloqueoApp a;
    if (j["tempLimite"].is<float>()) {
      float t = j["tempLimite"];
      if (t < 60 || t > 130) return enviarError(r, "El límite debe estar entre 60 y 130 °C");
      AJ.tempLimite = t;
    }
    if (j["simulador"].is<bool>()) {
      bool s = j["simulador"];
      if (s != AJ.simulador) {
        AJ.simulador = s;
        cambioModo = true;
      }
    }
    ajustesGuardar();
    enviarOk(r);
  });

  // Récords
  server.on("/api/records", HTTP_GET, [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    JsonDocument d;
    d["vmax"] = REC.vmax;
    d["rpmMax"] = REC.rpmMax;
    d["t050"] = REC.t050;
    d["fVmax"] = REC.fVmax;
    d["fRpm"] = REC.fRpm;
    d["f050"] = REC.f050;
    d["medir"] = medir050Estado();
    enviarJson(r, d);
  });
  postSimple("/api/records/reiniciar", [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    recordsReiniciar();
    enviarOk(r);
  });
  postJson("/api/records/050", [](AsyncWebServerRequest* r, JsonVariant& j) {
    BloqueoApp a;
    String acc = j["accion"] | "";
    if (acc == "iniciar") {
      if (E.conexion != CONECTADO) return enviarError(r, "Sin conexión con la centralita");
      medir050Iniciar();
    } else {
      medir050Cancelar();
    }
    enviarOk(r);
  });

  // Mantenimiento
  server.on("/api/mant", HTTP_GET, [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    JsonDocument d;
    mantJson(d);
    enviarJson(r, d);
  });
  postJson("/api/mant", [](AsyncWebServerRequest* r, JsonVariant& j) {
    BloqueoApp a;
    if (j["km"].is<float>()) {   // cuentakilómetros escrito a mano
      float km = j["km"];
      if (km < 0 || km > 999999) return enviarError(r, "Kilómetros no válidos");
      MANT.odometroKm = km;
      MANT.fechaOdometro = ahoraEpoch();
    }
    JsonArray items = j["items"];
    for (int i = 0; i < NUM_MANT && i < (int)items.size(); i++) {
      if (items[i]["int"].is<float>()) {
        float v = items[i]["int"];
        if (v >= 100 && v <= 100000) MANT.item[i].intervaloKm = v;
      }
      if (items[i]["km"].is<float>()) MANT.item[i].hechoKm = items[i]["km"];
    }
    if (j["hecho"].is<int>()) {   // «Hecho ahora»
      int i = j["hecho"];
      if (i >= 0 && i < NUM_MANT) {
        MANT.item[i].hechoKm = MANT.odometroKm;
        MANT.item[i].hechoHoras = MANT.horasMotor;
        MANT.item[i].fecha = ahoraEpoch();
      }
    }
    mantGuardar();
    enviarOk(r);
  });

  // Grabación
  server.on("/api/viajes", HTTP_GET, [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    JsonDocument d;
    grabLista(d);
    enviarJson(r, d);
  });
  postSimple("/api/viajes/iniciar", [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    String err;
    if (!grabIniciar(err)) return enviarError(r, err);
    enviarOk(r);
  });
  postSimple("/api/viajes/parar", [](AsyncWebServerRequest* r) {
    BloqueoApp a;
    grabParar();
    enviarOk(r);
  });
  postJson("/api/viajes/borrar", [](AsyncWebServerRequest* r, JsonVariant& j) {
    BloqueoApp a;
    String n = j["nombre"] | "";
    if (!grabBorrar(n)) return enviarError(r, "No se puede borrar ese trayecto");
    enviarOk(r);
  });
  server.on("/viaje", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!r->hasParam("n")) { r->send(400); return; }
    String n = r->getParam("n")->value();
    if (n.indexOf('/') >= 0 || n.indexOf("..") >= 0) { r->send(400); return; }
    String ruta = grabRuta(n);
    if (!LittleFS.exists(ruta)) { r->send(404); return; }
    r->send(LittleFS, ruta, "text/csv", r->hasParam("descargar"));
  });

  // Actualización de la web (pantallas) por WiFi, sin borrar los datos
  server.on(
      "/api/ota-web", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        bool ok;
        {
          BloqueoApp a;
          ok = actWebTerminar();
        }
        if (ok) enviarOk(r, "Web actualizada. Recarga la página.");
        else enviarError(r, actWebError());
        otaEnCurso = false;
      },
      [](AsyncWebServerRequest* r, const String& nombre, size_t index, uint8_t* data, size_t len, bool final) {
        BloqueoApp a;
        msOtaDatos = millis();
        if (index == 0) {
          otaEnCurso = true;
          grabParar();
          actWebEmpezar(r->contentLength());
        }
        actWebDatos(data, len);
      });

  // Actualización del programa por WiFi
  server.on(
      "/api/ota", HTTP_POST,
      [](AsyncWebServerRequest* r) {
        bool ok = !Update.hasError() && otaError.length() == 0;
        if (ok) {
          enviarOk(r, "Programa actualizado. Reiniciando...");
          reinicioPendiente = true;
        } else {
          if (Update.isRunning()) Update.abort();
          enviarError(r, otaError.length() ? otaError : String("Error al escribir el programa"));
        }
        otaEnCurso = false;
      },
      [](AsyncWebServerRequest* r, const String& nombre, size_t index, uint8_t* data, size_t len, bool final) {
        msOtaDatos = millis();
        if (index == 0) {
          otaError = "";
          otaEnCurso = true;
          if (Update.isRunning()) Update.abort();   // subida anterior cortada a medias
          Serial.printf("[OTA] Recibiendo %s\n", nombre.c_str());
          { BloqueoApp a; grabParar(); }
          if (len == 0 || data[0] != 0xE9) {
            otaError = "El archivo no parece un firmware de ESP32 (firmware.bin)";
            return;
          }
          if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
            otaError = "No se puede iniciar la actualización";
            Update.printError(Serial);
            return;
          }
        }
        if (otaError.length()) return;
        if (Update.write(data, len) != len) {
          otaError = "Error al escribir en la memoria";
          Update.printError(Serial);
          return;
        }
        if (final) {
          if (Update.end(true)) {
            Serial.printf("[OTA] Correcto: %u bytes\n", (unsigned)(index + len));
          } else {
            otaError = "El archivo está incompleto o dañado";
            Update.printError(Serial);
          }
        }
      });
}

void webIniciar() {
  WiFi.mode(WIFI_AP);
  bool ok = WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CANAL, 0, 4);   // IP por defecto: 192.168.4.1
  Serial.printf("[WEB] WiFi %s '%s' en http://192.168.4.1\n", ok ? "creada" : "ERROR", WIFI_SSID);

  ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType t, void*, uint8_t*, size_t) {
    if (t == WS_EVT_CONNECT) Serial.printf("[WEB] WebSocket conectado #%u\n", c->id());
    if (t == WS_EVT_DISCONNECT) Serial.printf("[WEB] WebSocket desconectado #%u\n", c->id());
  });
  server.addHandler(&ws);
  rutasApi();

  server.serveStatic("/fonts/", LittleFS, "/www/fonts/").setCacheControl("max-age=31536000");
  server.serveStatic("/icons/", LittleFS, "/www/icons/").setCacheControl("max-age=86400");
  server.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html").setCacheControl("no-cache");
  server.onNotFound([](AsyncWebServerRequest* r) {
    if (r->url().startsWith("/api/")) { r->send(404, "application/json", "{\"ok\":false,\"msg\":\"No existe\"}"); return; }
    if (!LittleFS.exists("/www/index.html")) {
      r->send(200, "text/html; charset=utf-8",
              "<meta name=viewport content='width=device-width'><h2>Falta la web</h2>"
              "<p>Sube los archivos con <code>pio run -t uploadfs</code>.</p>");
      return;
    }
    r->send(404, "text/plain", "No encontrado");
  });
  server.begin();
}
