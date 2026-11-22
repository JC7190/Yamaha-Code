# Diagnosis ESP32 para Yamaha NMAX 125 (2023)

Aparato de diagnosis con ESP32 que se enchufa al conector de diagnosis de la NMAX 125
(GPD125-A, código BALD), habla con la centralita por **línea K (KWP2000 / ISO 14230)** y
muestra todo en el **iPhone** con una web instalable (PWA) servida por la propia ESP32.

> ⚠️ El protocolo **no está verificado en esta moto**. El firmware prueba varios métodos
> de conexión y deja trazas detalladas en el puerto serie (115.200 baudios) para depurar.

## Contenido

```
platformio.ini        Configuración de PlatformIO
src/
  config.h            WiFi, pines, tiempos ISO 14230, límites
  kline.*             Línea K: init rápida/lenta, tramas, eco, suma de control, tiempos
  obd.*               Servicios OBD2: PIDs (01), foto del fallo (02), averías (03/07), borrar (04), VIN (09)
  pids.h              Tabla de PIDs y fórmulas
  sim.*               Modo simulador (datos y averías de ejemplo)
  estado.h            Estado compartido entre tareas
  almacen.*           LittleFS, ajustes e historial de averías
  records.cpp         Velocidad y rpm máximas, medición 0–50 km/h
  mantenimiento.cpp   Kilómetros, horas de motor, aceite/correa/filtro
  grabacion.cpp       Trayectos en CSV
  web.*               WiFi, REST, WebSocket y OTA
  main.cpp            Arranque, tarea de comunicación y reposo profundo
data/www/             Web del iPhone (se sube a LittleFS)
  index.html app.css app.js dtc.js pdf.js manifest.webmanifest
  fonts/              Barlow y Barlow Semi Condensed (licencia OFL)
  icons/              Iconos de la app
  logo-yamaha.png     Logo de la pantalla de arranque (blanco sobre fondo transparente)
```

## Hardware

| ESP32 | | L9637D |
|---|---|---|
| GPIO 17 (TX2) | → TXS0108E A1 → B1 → | pin 4 (TX) |
| GPIO 16 (RX2) | ← TXS0108E A2 ← B2 ← | pin 1 (RX) |

- L9637D: VCC (3) a 5 V, VS (7) a +12 V, GND (5) a masa, K (6) al cable Lg/L, 510 Ω entre +12 V y K. LI y LO libres.
- TXS0108E: VA = 3,3 V, VB = 5 V, OE unido a VA.
- Alimentación: +12 V (cable R) → fusible 1 A → convertidor 12→5 V → VIN. Masa: cable B.

## Compilar y subir (desde tu ordenador, con la ESP32 por USB)

1. Instala [PlatformIO](https://platformio.org/install) (extensión de VS Code o `pip install platformio`).
2. En la carpeta del proyecto:

```bash
pio run                 # compilar
pio run -t upload       # subir el firmware
pio run -t uploadfs     # subir la web (data/) a LittleFS
pio device monitor      # ver las trazas
```

`uploadfs` borra todo LittleFS, **también el historial, los trayectos y los ajustes**. Hazlo
la primera vez y cuando cambie la web.

### Modo simulador

En `platformio.ini`, `-DSIMULADOR_POR_DEFECTO=1` arranca en simulador (viene así para probar
la web sin moto). También se cambia en la web: **Más → Ajustes → Modo simulador** (se guarda).
El simulador usa su propio historial, récords, mantenimiento y trayectos (carpeta `/sim`),
así que no ensucia los datos de la moto.

El simulador repite un ciclo de 100 s (parado, ralentí, aceleración, crucero, frenada). Uno
de cada cuatro ciclos sube la temperatura a unos 108 °C para probar la alerta. Las averías
de ejemplo son P0115, P0122 y P0130 (pendiente); al borrarlas vuelven a los 3 minutos.

## Instalar la web en el iPhone

1. Ajustes → WiFi → **NMAX-Diag** (contraseña `nmax2023diag`, cámbiala en `src/config.h`).
   iOS dirá «Sin conexión a Internet»: es normal.
2. Abre **Safari** en `http://192.168.4.1`.
3. Botón Compartir → **Añadir a pantalla de inicio**.
4. A partir de ahí se abre a pantalla completa desde el icono. Todo funciona sin internet.

Notas de iOS:
- Los PDF y los CSV se descargan en Archivos → Descargas.
- Al volver a la app desde segundo plano, se reconecta sola.

## Uso en la moto

1. Desenchufa la CCU (módulo de Y-Connect) del conector de diagnosis y enchufa el aparato.
2. Pon **Más → Ajustes → Modo simulador** en apagado.
3. Pon el contacto. El aparato busca la centralita cada 5 s. En el monitor serie verás algo como:

```
[K] Init RÁPIDA -> destino 33 (funcional)
[K] TX: C1 33 F1 81 66
[K] RX: 83 F1 10 C1 8F EF C4
[OBD] *** MÉTODO QUE FUNCIONA: ISO 14230-4 KWP2000 · init rápida ... ***
```

Orden de prueba: init rápida funcional (0x33) → init rápida física (0x10, 0x11, 0x12) →
init lenta a 5 baudios (0x33). Se recuerda el método que funcionó.

### Reposo profundo

Si la centralita no responde durante **10 minutos** (contacto quitado), **no estás en
simulador** y **no hay ningún iPhone conectado al WiFi**, la ESP32 entra en reposo profundo
y despierta cada 2 minutos para ver si la centralita vuelve (sin encender el WiFi). Para
despertarla al momento, desenchúfala y vuelve a enchufarla. Los tiempos están en `src/config.h`.

## Actualizar el programa por WiFi (OTA)

1. Compila con `pio run`. El archivo queda en `.pio/build/esp32dev/firmware.bin`.
2. Pásalo al iPhone (AirDrop o iCloud Drive → Archivos).
3. En la web: **Más → Actualizar programa** → elige el archivo → Actualizar.

Solo actualiza el programa, no la web ni los datos guardados.

## Datos guardados (LittleFS)

- Historial de averías: `/datos/historial.json` (fecha y hora del iPhone, activa/borrada).
- Récords: `/datos/records.json`. Mantenimiento: `/datos/mant.json`. Ajustes: `/ajustes.json`.
- Trayectos: `/datos/viajes/<fecha>.csv`, una fila por segundo con rpm, velocidad, temperaturas,
  presión, acelerador, carga, avance, lambda y batería. Unos 130 KB por hora.

Los kilómetros se leen de la centralita si admite el PID 0xA6. Si no, se calculan con la
velocidad a partir del valor que escribas en **Mantenimiento → Corregir cuentakilómetros**.
Los intervalos iniciales (aceite 6.000 km, correa 24.000 km, filtro 12.000 km) son
orientativos: compruébalos en el manual de tu moto y cámbialos en la web.
