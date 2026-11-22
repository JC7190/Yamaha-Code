# Script de PlatformIO: en cada compilación, empaqueta data/www en web.tar
# (junto a firmware.bin) para actualizar la web por WiFi sin borrar datos.
import os
import tarfile

Import("env")  # noqa: F821


def empaquetar(env):
    origen = os.path.join(env.subst("$PROJECT_DIR"), "data", "www")
    carpeta = env.subst("$BUILD_DIR")
    os.makedirs(carpeta, exist_ok=True)
    destino = os.path.join(carpeta, "web.tar")
    with tarfile.open(destino, "w", format=tarfile.USTAR_FORMAT) as tar:
        for raiz, dirs, archivos in os.walk(origen):
            dirs.sort()
            for nombre in sorted(archivos):
                if nombre.startswith("."):
                    continue
                ruta = os.path.join(raiz, nombre)
                rel = os.path.relpath(ruta, origen).replace(os.sep, "/")
                for parte in rel.split("/"):
                    if len(parte) > 31:
                        raise SystemExit("Nombre demasiado largo para LittleFS: " + rel)
                info = tar.gettarinfo(ruta, arcname=rel)
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                with open(ruta, "rb") as f:
                    tar.addfile(info, f)
    print("Web empaquetada para OTA: " + destino)


# Se genera en cada ejecución de PlatformIO, aunque el programa no haya cambiado
empaquetar(env)  # noqa: F821
