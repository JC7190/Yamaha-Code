// Punto de acceso WiFi, servidor web (REST + WebSocket) y actualización OTA.
#pragma once
#include <Arduino.h>

void webIniciar();
void webBucle();               // emite datos en vivo y eventos
bool webOtaEnCurso();
bool webReinicioPendiente();
bool webCambioModoPendiente(); // la web ha cambiado entre moto y simulador
