#include "kline.h"
#include "config.h"
#include <esp32-hal-matrix.h>
#include <soc/gpio_sig_map.h>

KLine K;

static bool modoIso9141 = false;   // cabecera 68 6A F1 si la init lenta indica ISO 9141-2

void KLine::begin() {
  Serial2.setRxBufferSize(512);
  Serial2.begin(KLINE_BAUDIOS, SERIAL_8N1, KLINE_RX_PIN, KLINE_TX_PIN);
  // Entregar cada byte en cuanto llega (necesario para descartar el eco y medir tiempos)
  Serial2.setRxFIFOFull(1);
  Serial2.setRxTimeout(1);
  Serial.printf("[K] UART2 lista: RX=GPIO%d TX=GPIO%d %d baudios\n", KLINE_RX_PIN, KLINE_TX_PIN,
                KLINE_BAUDIOS);
}

void KLine::traza(const char* pref, const uint8_t* b, int n) {
  if (!trazas) return;
  Serial.printf("[K] %s", pref);
  for (int i = 0; i < n; i++) Serial.printf(" %02X", b[i]);
  Serial.println();
}

void KLine::soltarTx() {
  Serial2.flush();
  digitalWrite(KLINE_TX_PIN, HIGH);
  pinMode(KLINE_TX_PIN, OUTPUT);
  pinMatrixOutDetach(KLINE_TX_PIN, false, false);   // desconecta la UART: el pin pasa a GPIO
  digitalWrite(KLINE_TX_PIN, HIGH);
}

void KLine::conectarTx() {
  pinMatrixOutAttach(KLINE_TX_PIN, U2TXD_OUT_IDX, false, false);
}

void KLine::reposoBus() {
  soltarTx();
  digitalWrite(KLINE_TX_PIN, HIGH);
}

void KLine::vaciarEntrada() {
  while (Serial2.available()) Serial2.read();
}

void KLine::esperarP3() {
  uint32_t desde = millis() - msFinRespuesta_;
  if (desde < T_P3_MIN) delay(T_P3_MIN - desde);
}

// Envía un byte y espera su eco (la línea K es semidúplex: recibimos lo que enviamos).
// Las esperas usan delay(1): ceden la CPU al resto de tareas y los bytes quedan en el búfer de la UART.
void KLine::escribirByte(uint8_t b, bool pausaP4) {
  Serial2.write(b);
  uint32_t t0 = millis();
  while (!Serial2.available()) {
    if (millis() - t0 > 15) {
      if (trazas) Serial.printf("[K] AVISO: sin eco del byte %02X (¿transceptor o 12 V?)\n", b);
      return;
    }
    delay(1);
  }
  uint8_t eco = Serial2.read();
  if (eco != b && trazas) Serial.printf("[K] AVISO: eco distinto, enviado %02X recibido %02X\n", b, eco);
  if (pausaP4) delay(T_P4);
}

bool KLine::enviar(const uint8_t* datos, uint8_t len) {
  uint8_t buf[KLINE_MAX_TRAMA + 5];
  uint8_t n = 0;
  if (modoIso9141) {
    buf[n++] = 0x68;
    buf[n++] = 0x6A;
    buf[n++] = DIR_PROBADOR;
  } else {
    if (len > 63) return false;
    buf[n++] = (funcional_ ? 0xC0 : 0x80) | len;
    buf[n++] = destino_;
    buf[n++] = DIR_PROBADOR;
  }
  memcpy(buf + n, datos, len);
  n += len;
  uint8_t cs = 0;
  for (int i = 0; i < n; i++) cs += buf[i];
  buf[n++] = cs;

  vaciarEntrada();
  traza("TX:", buf, n);
  for (int i = 0; i < n; i++) escribirByte(buf[i], i < n - 1);   // sin P4 tras el último byte
  msUltimoEnvio_ = millis();
  return true;
}

bool KLine::recibir(Trama& t, uint32_t esperaPrimerByte) {
  uint8_t buf[KLINE_MAX_TRAMA + 6];
  int n = 0;
  int total = -1;
  uint32_t t0 = millis();
  while (!Serial2.available()) {
    if (millis() - t0 > esperaPrimerByte) return false;
    delay(1);
  }
  uint32_t ultimo = millis();
  while (true) {
    if (Serial2.available()) {
      buf[n++] = Serial2.read();
      ultimo = millis();
      if (!modoIso9141 && total < 0) {
        uint8_t fmt = buf[0];
        bool dirs = (fmt & 0xC0) != 0;
        uint8_t l = fmt & 0x3F;
        int cab = 1 + (dirs ? 2 : 0);
        if (l == 0) {
          if (n >= cab + 1) total = cab + 1 + buf[cab] + 1;
        } else {
          total = cab + l + 1;
        }
      }
      if (total > 0 && n >= total) break;
      if (n >= (int)sizeof(buf)) {
        traza("RX demasiado larga:", buf, n);
        return false;
      }
    } else {
      // En ISO 9141 la trama termina por silencio; en KWP un silencio es un error
      if (millis() - ultimo > T_P1_MAX + 5) {
        if (modoIso9141 && n >= 5) break;
        traza("RX incompleta:", buf, n);
        msFinRespuesta_ = millis();
        return false;
      }
      delay(1);
    }
  }
  msFinRespuesta_ = millis();
  traza("RX:", buf, n);

  uint8_t cs = 0;
  for (int i = 0; i < n - 1; i++) cs += buf[i];
  if (cs != buf[n - 1]) {
    if (trazas) Serial.printf("[K] suma de control incorrecta (%02X != %02X)\n", cs, buf[n - 1]);
    return false;
  }

  int pos;
  if (modoIso9141) {
    t.destino = buf[1];
    t.origen = buf[2];
    pos = 3;
    t.len = n - 4;
  } else {
    uint8_t fmt = buf[0];
    bool dirs = (fmt & 0xC0) != 0;
    pos = 1;
    if (dirs) {
      t.destino = buf[1];
      t.origen = buf[2];
      pos = 3;
    } else {
      t.destino = t.origen = 0;
    }
    uint8_t l = fmt & 0x3F;
    if (l == 0) l = buf[pos++];
    t.len = l;
  }
  if (t.len > KLINE_MAX_TRAMA) return false;
  memcpy(t.d, buf + pos, t.len);
  return true;
}

int KLine::peticion(const uint8_t* datos, uint8_t len, Trama* resp, int maxTramas, uint32_t espera) {
  esperarP3();
  enviar(datos, len);
  uint8_t svc = datos[0];
  int n = 0;
  uint32_t esperaActual = espera;
  while (n < maxTramas) {
    Trama t;
    if (!recibir(t, esperaActual)) break;
    if (t.len == 0) continue;
    if (t.d[0] == 0x7F && t.len >= 3 && t.d[1] == svc) {
      if (t.d[2] == 0x78) {   // respuesta pendiente: la centralita pide más tiempo
        if (trazas) Serial.println("[K] respuesta pendiente (0x78), esperando...");
        esperaActual = 5000;
        continue;
      }
      if (trazas) Serial.printf("[K] respuesta negativa servicio %02X código %02X\n", svc, t.d[2]);
      resp[n++] = t;
      break;
    }
    if (t.d[0] == (uint8_t)(svc + 0x40)) {
      resp[n++] = t;
      esperaActual = T_P2_MAX + 15;   // ¿más tramas? (VIN, varias averías...)
    }
  }
  return n;
}

bool KLine::initRapida(uint8_t destino, bool funcional, uint8_t& kb1, uint8_t& kb2) {
  modoIso9141 = false;
  configurarDestino(destino, funcional);
  Serial.printf("[K] Init RÁPIDA -> destino %02X (%s)\n", destino, funcional ? "funcional" : "física");

  // El bus debe estar en reposo (alto) al menos W5 = 300 ms
  soltarTx();
  uint32_t desde = millis() - msUltimaInit_;
  delay(desde < 1000 ? 1000 - desde : 300);

  digitalWrite(KLINE_TX_PIN, LOW);
  delay(25);
  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(25);
  conectarTx();
  vaciarEntrada();
  msUltimaInit_ = millis();
  msFinRespuesta_ = 0;

  uint8_t start = 0x81;   // StartCommunication
  enviar(&start, 1);
  Trama t;
  uint32_t t0 = millis();
  while (millis() - t0 < 300) {
    if (!recibir(t, 150)) break;
    if (t.len >= 1 && t.d[0] == 0xC1) {
      kb1 = t.len > 1 ? t.d[1] : 0;
      kb2 = t.len > 2 ? t.d[2] : 0;
      Serial.printf("[K] StartCommunication OK. Centralita %02X, keybytes %02X %02X\n", t.origen, kb1, kb2);
      if (t.origen && !funcional) configurarDestino(t.origen, false);
      return true;
    }
    if (t.len >= 3 && t.d[0] == 0x7F)
      Serial.printf("[K] StartCommunication rechazado, código %02X\n", t.d[2]);
  }
  Serial.println("[K] Init rápida sin respuesta");
  return false;
}

static int leerByte(uint32_t espera) {
  uint32_t t0 = millis();
  while (!Serial2.available()) {
    if (millis() - t0 > espera) return -1;
    delay(1);
  }
  return Serial2.read();
}

bool KLine::initLenta(uint8_t direccion, uint8_t& kb1, uint8_t& kb2) {
  Serial.printf("[K] Init LENTA (5 baudios) -> dirección %02X\n", direccion);
  modoIso9141 = false;
  soltarTx();
  uint32_t desde = millis() - msUltimaInit_;
  delay(desde < 2600 ? 2600 - desde : 300);   // W5 y separación entre intentos

  // bit de arranque, 8 bits de datos (LSB primero), bit de parada: 200 ms cada uno
  digitalWrite(KLINE_TX_PIN, LOW);
  delay(200);
  for (int i = 0; i < 8; i++) {
    digitalWrite(KLINE_TX_PIN, (direccion >> i) & 1 ? HIGH : LOW);
    delay(200);
  }
  digitalWrite(KLINE_TX_PIN, HIGH);
  delay(200);
  conectarTx();
  vaciarEntrada();
  msUltimaInit_ = millis();

  int sync = leerByte(400);   // W1: 60–300 ms
  if (sync < 0) {
    Serial.println("[K] Init lenta: sin byte de sincronización");
    return false;
  }
  if (sync != 0x55) {
    Serial.printf("[K] Init lenta: sincronización %02X (se esperaba 55; ¿otra velocidad?)\n", sync);
    return false;
  }
  int b1 = leerByte(60), b2 = leerByte(60);
  if (b1 < 0 || b2 < 0) {
    Serial.println("[K] Init lenta: faltan keybytes");
    return false;
  }
  kb1 = b1;
  kb2 = b2;
  Serial.printf("[K] Init lenta: keybytes %02X %02X\n", kb1, kb2);
  delay(30);                  // W4: 25–50 ms
  escribirByte(~kb2 & 0xFF, false);
  int inv = leerByte(80);
  if (inv < 0 || (uint8_t)inv != (uint8_t)~direccion) {
    Serial.printf("[K] Init lenta: confirmación incorrecta (%d)\n", inv);
    return false;
  }
  msFinRespuesta_ = millis();
  modoIso9141 = (kb1 == 0x08 && kb2 == 0x08) || (kb1 == 0x94 && kb2 == 0x94);
  configurarDestino(direccion, true);
  Serial.printf("[K] Init lenta OK (%s)\n", modoIso9141 ? "ISO 9141-2" : "ISO 14230 KWP2000");
  return true;
}

bool klineEsIso9141() { return modoIso9141; }
