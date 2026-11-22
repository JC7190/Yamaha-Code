// Configuración general del aparato de diagnosis NMAX 125.
#pragma once
#include <Arduino.h>

// ---------------- WiFi ----------------
#define WIFI_SSID        "NMAX-Diag"
#define WIFI_PASSWORD    "nmax2023diag"   // WPA2, mínimo 8 caracteres. Cámbiala.
#define WIFI_CANAL       6

// ---------------- Línea K (UART2) ----------------
#define KLINE_RX_PIN     16   // L9637D pin 1 (RX) -> TXS0108E B2/A2 -> GPIO16
#define KLINE_TX_PIN     17   // GPIO17 -> TXS0108E A1/B1 -> L9637D pin 4 (TX)
#define KLINE_BAUDIOS    10400

// Direcciones ISO 14230
#define DIR_PROBADOR     0xF1
#define DIR_FUNCIONAL    0x33
// Direcciones físicas a probar si la funcional no responde (motor suele ser 0x10/0x11/0x12)
static const uint8_t DIRS_FISICAS[] = {0x10, 0x11, 0x12};

// Tiempos ISO 14230 (ms)
#define T_P1_MAX         20    // entre bytes de la respuesta de la centralita
#define T_P2_MAX         50    // petición -> respuesta (normal)
#define T_P2_MAX_EXT     1000  // margen ampliado para respuestas lentas / múltiples tramas
#define T_P3_MIN         55    // fin de respuesta -> nueva petición
#define T_P3_MAX         5000  // máximo sin peticiones antes de que se cierre la sesión
#define T_P4             5     // entre bytes de la petición del probador
#define T_KEEPALIVE      2000  // si no se ha enviado nada en este tiempo, TesterPresent

// Ritmo de datos en vivo
#define LECTURAS_POR_SEG 5

// Reposo profundo
#define MIN_SIN_RESPUESTA_REPOSO  10        // minutos sin centralita antes de dormir
#define SEG_ENTRE_DESPERTARES     120       // cada cuánto despierta a comprobar
#define SEG_COMPROBACION_DESPIERTO 20       // cuánto intenta conectar al despertar

// Alertas
#define TEMP_LIMITE_DEFECTO   100.0f   // °C, configurable desde la web
#define BATERIA_BAJA_V        11.8f    // V

// Grabación
#define GRAB_INTERVALO_MS     1000     // una fila por segundo
#define FS_AVISO_LIBRE        (96 * 1024)   // aviso de memoria casi llena
#define FS_MINIMO_LIBRE       (24 * 1024)   // por debajo se detiene la grabación

#ifndef SIMULADOR_POR_DEFECTO
#define SIMULADOR_POR_DEFECTO 1
#endif

#define VERSION_FIRMWARE "1.0.0"
