// Capa física y de enlace de la línea K (ISO 14230 / KWP2000) sobre UART2.
#pragma once
#include <Arduino.h>

#define KLINE_MAX_TRAMA 64

struct Trama {
  uint8_t destino;
  uint8_t origen;
  uint8_t len;                    // bytes de datos (servicio + parámetros)
  uint8_t d[KLINE_MAX_TRAMA];
};

class KLine {
 public:
  void begin();

  // Inicialización rápida: 25 ms bajo + 25 ms alto + StartCommunication.
  // funcional = true usa cabecera de direccionamiento funcional (0xC0).
  bool initRapida(uint8_t destino, bool funcional, uint8_t& kb1, uint8_t& kb2);

  // Inicialización lenta a 5 baudios (ISO 9141-2 / ISO 14230 5-baud init).
  bool initLenta(uint8_t direccion, uint8_t& kb1, uint8_t& kb2);

  // Envía una petición con cabecera y suma de control. Descarta el eco.
  bool enviar(const uint8_t* datos, uint8_t len);

  // Recibe una trama. Devuelve false si no llega nada en 'esperaPrimerByte' ms
  // o si la trama es incorrecta.
  bool recibir(Trama& t, uint32_t esperaPrimerByte);

  // Petición completa: envía y recoge todas las tramas de respuesta
  // (hasta 'maxTramas') que tengan como primer byte 'servicio + 0x40' o 0x7F.
  // Gestiona la respuesta pendiente 0x7F xx 0x78. Devuelve número de tramas.
  int peticion(const uint8_t* datos, uint8_t len, Trama* resp, int maxTramas,
               uint32_t espera = 150);

  void configurarDestino(uint8_t destino, bool funcional) {
    destino_ = destino;
    funcional_ = funcional;
  }
  uint32_t msUltimoEnvio() const { return msUltimoEnvio_; }
  void reposoBus();  // deja TX en reposo (alto) antes de dormir

  bool trazas = true;

 private:
  void escribirByte(uint8_t b, bool pausaP4);
  void vaciarEntrada();
  void esperarP3();
  void soltarTx();      // pasa el pin TX a GPIO
  void conectarTx();    // devuelve el pin TX a la UART
  void traza(const char* pref, const uint8_t* b, int n);

  uint8_t destino_ = 0x33;
  bool funcional_ = true;
  uint32_t msUltimoEnvio_ = 0;
  uint32_t msFinRespuesta_ = 0;
  uint32_t msUltimaInit_ = 0;
};

extern KLine K;
