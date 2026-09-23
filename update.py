"""Se actualiza solo bajando el último ZIP de GitHub, sin necesitar git — para
quien descargó el repo como ZIP en vez de clonarlo. Nunca toca lo personal
(.env, memoria, comandos/dispositivos propios, sonidos elegidos, etc): solo
pisa los archivos de código de la lista de abajo.

No se actualiza a sí mismo ni a setup.bat desde acá: un .bat modificándose a
sí mismo a mitad de ejecución puede confundir a cmd.exe (lee el archivo por
posición, no lo carga entero de antemano como Python). Si algún día cambia
setup.bat, hay que bajarlo a mano una vez."""

import os
import shutil
import tempfile
import urllib.request
import zipfile

REPO_ZIP_URL = "https://github.com/podselxd/jarvis/archive/refs/heads/master.zip"
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))

CODE_ITEMS = [
    "jarvis.py",
    "ui.py",
    "requirements.txt",
    "detener.bat",
    "README.md",
    ".gitignore",
    "assets",
    os.path.join("sounds", "README.md"),
]


def main() -> None:
    if os.path.isdir(os.path.join(PROJECT_DIR, ".git")):
        return  # clonado con git: setup.bat ya hace "git pull" para este caso

    print("Buscando actualizaciones...")
    tmp_dir = tempfile.mkdtemp(prefix="jarvis_update_")
    try:
        zip_path = os.path.join(tmp_dir, "update.zip")
        try:
            urllib.request.urlretrieve(REPO_ZIP_URL, zip_path)
        except OSError as exc:
            print(f"No pude revisar actualizaciones (¿sin internet?): {exc}")
            return

        with zipfile.ZipFile(zip_path) as zf:
            zf.extractall(tmp_dir)

        extracted = [
            d for d in os.listdir(tmp_dir)
            if os.path.isdir(os.path.join(tmp_dir, d))
        ]
        if not extracted:
            print("La descarga de actualización vino vacía, sigo con lo que ya tenés.")
            return
        source_root = os.path.join(tmp_dir, extracted[0])

        for item in CODE_ITEMS:
            src = os.path.join(source_root, item)
            dst = os.path.join(PROJECT_DIR, item)
            if not os.path.exists(src):
                continue
            if os.path.isdir(src):
                shutil.copytree(src, dst, dirs_exist_ok=True)
            else:
                os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
                shutil.copy2(src, dst)

        print("Listo, código actualizado.")
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
