// Diagnosis ESP32 para Yamaha NMAX 125 (2023, GPD125-A / BALD)
// Línea K (KWP2000) por UART2 + punto de acceso WiFi con web para el iPhone.
#include <Arduino.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "almacen.h"
#include "config.h"
#include "estado.h"
#include "kline.h"
#include "obd.h"
#include "sim.h"
#include "web.h"

Estado E;
SemaphoreHandle_t mutexApp;
static QueueHandle_t colaEventos;

void eventoWeb(const char* json) {
  char buf[200];
  strlcpy(buf, json, sizeof(buf));
  xQueueSend(colaEventos, buf, 0);
}

bool sacarEventoWeb(char* buf, size_t len) {
  char tmp[200];
  if (xQueueReceive(colaEventos, tmp, 0) != pdTRUE) return false;
  strlcpy(buf, tmp, len);
  return true;
}

// ------------------------------------------------------------------ tarea de comunicación

static void tareaComunicacion(void*) {
  bool simActivo = E.simulador;
  if (simActivo) SIM.iniciar();
  for (;;) {
    bool sim = E.simulador;
    if (sim != simActivo) {
      simActivo = sim;
      if (sim) {
        SIM.iniciar();
      } else {
        Serial.println("[OBD] Modo moto: se busca la centralita por la línea K");
        OBD.reiniciar();
      }
    }
    if (sim) SIM.paso();
    else OBD.paso();
    vTaskDelay(1);
  }
}

// ------------------------------------------------------------------ reposo profundo

static void dormir() {
  Serial.printf("[REPOSO] Sin centralita durante %d min. Reposo profundo; despierto cada %d s.\n",
                MIN_SIN_RESPUESTA_REPOSO, SEG_ENTRE_DESPERTARES);
  {
    BloqueoApp a;
    grabParar();
    mantGuardar();
    recordsGuardar();
  }
  WiFi.mode(WIFI_OFF);
  K.reposoBus();   // TX en alto: la línea K queda en reposo
  gpio_hold_en((gpio_num_t)KLINE_TX_PIN);
  gpio_deep_sleep_hold_en();
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)SEG_ENTRE_DESPERTARES * 1000000ULL);
  esp_deep_sleep_start();
}

static void revisarReposo() {
  if (AJ.simulador || webOtaEnCurso()) return;
  if (E.conexion == CONECTADO) return;
  if (WiFi.softAPgetStationNum() > 0) return;   // hay un iPhone conectado: no se duerme
  if (millis() - E.msUltimaRespuesta > (uint32_t)MIN_SIN_RESPUESTA_REPOSO * 60000UL) dormir();
}

// Al despertar por temporizador: se comprueba la centralita sin encender el WiFi
static void comprobacionAlDespertar() {
  Serial.println("[REPOSO] Despertar: ¿responde la centralita?");
  uint32_t t0 = millis();
  while (millis() - t0 < SEG_COMPROBACION_DESPIERTO * 1000UL) {
    if (OBD.intentarConectar()) {
      Serial.println("[REPOSO] La centralita responde: arranque normal");
      return;
    }
  }
  Serial.println("[REPOSO] Sigue sin responder");
  K.reposoBus();
  gpio_hold_en((gpio_num_t)KLINE_TX_PIN);
  gpio_deep_sleep_hold_en();
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)SEG_ENTRE_DESPERTARES * 1000000ULL);
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ arranque

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("\n=== Diagnosis NMAX 125 · firmware %s ===\n", VERSION_FIRMWARE);
  gpio_hold_dis((gpio_num_t)KLINE_TX_PIN);
  gpio_deep_sleep_hold_dis();

  E.mutex = xSemaphoreCreateMutex();
  mutexApp = xSemaphoreCreateRecursiveMutex();
  colaEventos = xQueueCreate(8, 200);

  almacenIniciar();
  ajustesCargar();
  recordsCargar();
  mantCargar();
  E.simulador = AJ.simulador;
  E.conexion = DESCONECTADO;
  E.msUltimaRespuesta = 0;

  K.begin();
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && !AJ.simulador) comprobacionAlDespertar();

  webIniciar();
  // Si ya se conectó al despertar, la tarea sigue con esa sesión
  xTaskCreatePinnedToCore(tareaComunicacion, "comunicacion", 8192, nullptr, 2, nullptr, 1);
}

void loop() {
  if (webCambioModoPendiente()) {
    BloqueoApp a;
    Serial.printf("[MODO] Cambio a %s\n", AJ.simulador ? "SIMULADOR" : "MOTO");
    {
      Bloqueo b;
      E.simulador = AJ.simulador;
      E.averiasLeidas = false;
      E.guardadas.n = E.pendientes.n = 0;
      E.foto.valida = false;
      E.conexion = DESCONECTADO;
      E.msUltimaRespuesta = millis();
    }
    medir050Cancelar();
    almacenCambiarModo();
  }

  {
    BloqueoApp a;
    recordsRevisar();
    mantRevisar();
    grabRevisar();
    historialRevisar();
  }
  webBucle();
  revisarReposo();

  if (webReinicioPendiente()) {
    delay(1000);
    ESP.restart();
  }
  delay(20);
}
