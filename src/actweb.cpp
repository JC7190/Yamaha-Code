#include "actweb.h"
#include <LittleFS.h>
#include "almacen.h"

static const char* DIR_NUEVO = "/www_nuevo";
static const char* DIR_WEB = "/www";

static uint8_t cab[512];
static size_t lenCab = 0;
static File fich;
static size_t restante = 0, relleno = 0, tamActual = 0;
static bool fin = false, vistoIndex = false;
static int ficheros = 0;
static String error;

const String& actWebError() { return error; }

static void borrarRecursivo(const String& ruta) {
  File d = LittleFS.open(ruta);
  if (!d) return;
  if (!d.isDirectory()) {
    d.close();
    LittleFS.remove(ruta);
    return;
  }
  File f = d.openNextFile();
  while (f) {
    String hijo = String(f.path());
    bool esDir = f.isDirectory();
    f.close();
    if (esDir) borrarRecursivo(hijo);
    else LittleFS.remove(hijo);
    f = d.openNextFile();
  }
  d.close();
  LittleFS.rmdir(ruta);
}

static void crearCarpetas(const String& ruta) {
  // crea cada carpeta intermedia de la ruta (sin el último componente)
  for (int i = 1; i < (int)ruta.length(); i++)
    if (ruta[i] == '/') LittleFS.mkdir(ruta.substring(0, i));
}

static size_t octal(const uint8_t* p, size_t n) {
  size_t v = 0;
  for (size_t i = 0; i < n && p[i]; i++)
    if (p[i] >= '0' && p[i] <= '7') v = v * 8 + (p[i] - '0');
  return v;
}

static bool nombreValido(const String& n) {
  if (n.length() == 0 || n.startsWith("/") || n.indexOf("..") >= 0) return false;
  int ini = 0;
  while (ini < (int)n.length()) {
    int fin = n.indexOf('/', ini);
    if (fin < 0) fin = n.length();
    if (fin - ini > 31) return false;   // límite de LittleFS por nombre
    ini = fin + 1;
  }
  return true;
}

static void procesarCabecera() {
  bool vacia = true;
  for (int i = 0; i < 512; i++)
    if (cab[i]) { vacia = false; break; }
  if (vacia) {   // dos bloques a cero = fin del archivo
    fin = true;
    return;
  }
  if (memcmp(cab + 257, "ustar", 5) != 0) {
    error = "El archivo no es un web.tar válido";
    return;
  }
  char nombre[101], prefijo[156];
  memcpy(nombre, cab, 100);
  nombre[100] = 0;
  memcpy(prefijo, cab + 345, 155);
  prefijo[155] = 0;
  String n = prefijo[0] ? String(prefijo) + "/" + nombre : String(nombre);
  while (n.startsWith("./")) n = n.substring(2);
  char tipo = cab[156];
  size_t tam = octal(cab + 124, 12);

  restante = tam;
  tamActual = tam;
  if (n.endsWith("/")) n.remove(n.length() - 1);
  if (tipo == '5') {   // carpeta
    if (n.length() && nombreValido(n)) LittleFS.mkdir(String(DIR_NUEVO) + "/" + n);
    restante = 0;
    return;
  }
  if (tipo != '0' && tipo != 0) return;   // otros tipos: se saltan sus datos
  if (n.length() == 0) return;
  if (!nombreValido(n)) {
    error = "Nombre no admitido en el archivo: " + n;
    return;
  }
  String ruta = String(DIR_NUEVO) + "/" + n;
  crearCarpetas(ruta);
  fich = LittleFS.open(ruta, "w");
  if (!fich) {
    error = "No se puede escribir " + n;
    return;
  }
  if (n == "index.html") vistoIndex = true;
  ficheros++;
  if (tam == 0) fich.close();
}

void actWebEmpezar(size_t tamano) {
  if (fich) fich.close();   // por si una subida anterior se cortó a medias
  error = "";
  lenCab = restante = relleno = 0;
  fin = vistoIndex = false;
  ficheros = 0;
  borrarRecursivo(DIR_NUEVO);
  if (tamano && fsLibre() < tamano + 32 * 1024) {
    error = "No hay memoria libre suficiente: borra algún trayecto";
    return;
  }
  LittleFS.mkdir(DIR_NUEVO);
  Serial.printf("[WEBUPD] Recibiendo web (%u bytes)\n", (unsigned)tamano);
}

void actWebDatos(const uint8_t* d, size_t n) {
  while (n > 0 && error.length() == 0 && !fin) {
    if (restante > 0) {
      size_t k = min(n, restante);
      if (fich && fich.write(d, k) != k) {
        error = "Memoria llena al escribir la web";
        fich.close();
        return;
      }
      d += k;
      n -= k;
      restante -= k;
      if (restante == 0) {
        if (fich) fich.close();
        relleno = (512 - tamActual % 512) % 512;
      }
    } else if (relleno > 0) {
      size_t k = min(n, relleno);
      d += k;
      n -= k;
      relleno -= k;
    } else {
      size_t k = min(n, 512 - lenCab);
      memcpy(cab + lenCab, d, k);
      lenCab += k;
      d += k;
      n -= k;
      if (lenCab == 512) {
        lenCab = 0;
        procesarCabecera();
        if (restante == 0 && !fin) relleno = 0;
      }
    }
  }
}

bool actWebTerminar() {
  if (fich) fich.close();
  fsCambiado();
  if (error.length() == 0 && (restante > 0 || !vistoIndex))
    error = restante > 0 ? "El archivo está incompleto" : "Falta index.html en el archivo";
  if (error.length()) {
    Serial.printf("[WEBUPD] Error: %s\n", error.c_str());
    borrarRecursivo(DIR_NUEVO);
    return false;
  }
  // La web anterior se guarda hasta que la nueva está en su sitio
  borrarRecursivo("/www_viejo");
  bool habiaWeb = LittleFS.exists(DIR_WEB);
  if (habiaWeb && !LittleFS.rename(DIR_WEB, "/www_viejo")) {
    error = "No se ha podido sustituir la web";
    borrarRecursivo(DIR_NUEVO);
    return false;
  }
  if (!LittleFS.rename(DIR_NUEVO, DIR_WEB)) {
    if (habiaWeb) LittleFS.rename("/www_viejo", DIR_WEB);
    error = "No se ha podido instalar la web nueva";
    borrarRecursivo(DIR_NUEVO);
    return false;
  }
  borrarRecursivo("/www_viejo");
  Serial.printf("[WEBUPD] Web actualizada: %d archivos\n", ficheros);
  return true;
}
