// Actualización de la web (pantallas del iPhone) por WiFi sin borrar los datos guardados.
// Se sube un archivo web.tar (lo genera la compilación) que se descomprime en /www_nuevo
// y, si está completo, sustituye a /www. Historial, trayectos, récords, mantenimiento y
// ajustes no se tocan.
#pragma once
#include <Arduino.h>

void actWebEmpezar(size_t tamano);
void actWebDatos(const uint8_t* d, size_t n);
bool actWebTerminar();
const String& actWebError();
