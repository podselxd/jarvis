#!/usr/bin/env python3
"""Arma entrenar_hey_sokari.ipynb con onnx_a_jww.py adentro (así el cuaderno
no depende de que el repo sea público). Si cambias el convertidor, vuelve a
correr:  python herramientas/armar_cuaderno.py"""
import json
import os

AQUI = os.path.dirname(os.path.abspath(__file__))


def md(text):
    return {"cell_type": "markdown", "metadata": {}, "source": text.strip("\n").splitlines(keepends=True)}


def code(text):
    return {"cell_type": "code", "metadata": {}, "execution_count": None, "outputs": [],
            "source": text.strip("\n").splitlines(keepends=True)}


convertidor = open(os.path.join(AQUI, "onnx_a_jww.py"), encoding="utf-8").read()

cells = [
    md("""
# Entrenar "Hey Sokari"

Entrena el detector de **"Hey Sokari"** con cientos de voces sintéticas distintas, para que responda a cualquiera.
Al final se descarga un archivo **`hey_sokari.jww`**.

**Antes de empezar**
1. *Entorno de ejecución → Cambiar tipo de entorno de ejecución →* **T4 GPU** → Guardar.
2. *Entorno de ejecución →* **Ejecutar todo**.

Tarda más o menos 1–2 horas. Si Colab se desconecta, vuelve a darle *Ejecutar todo*: lo que ya se generó no se repite.

**Si algo falla:** copia completo el mensaje de la celda que salió en rojo y mándaselo a Claude.
"""),
    md("## 1. Revisar la GPU y la versión de Python"),
    code("""
import subprocess, sys
print("Python", sys.version.split()[0])
gpu = subprocess.run(["nvidia-smi", "-L"], capture_output=True, text=True).stdout.strip()
print(gpu or "SIN GPU")
assert gpu, "Activa la GPU: Entorno de ejecución → Cambiar tipo de entorno de ejecución → T4 GPU, y dale Ejecutar todo otra vez."
if sys.version_info >= (3, 13):
    print("AVISO: las voces sintéticas (piper-phonemize) solo tienen paquetes hasta Python 3.12; la instalación puede fallar.")
"""),
    md("## 2. Instalar openWakeWord y el generador de voces"),
    code("""
%%bash
set -e
cd /content
[ -d openWakeWord ] || git clone -q --depth 1 https://github.com/dscripka/openWakeWord
pip install -q -e ./openWakeWord --no-deps
[ -d piper-sample-generator ] || git clone -q --depth 1 https://github.com/rhasspy/piper-sample-generator
mkdir -p piper-sample-generator/models
MODELO=piper-sample-generator/models/en_US-libritts_r-medium.pt
[ -s $MODELO ] || wget -q -O $MODELO https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
pip install -q piper-phonemize webrtcvad mutagen torchinfo torchmetrics "speechbrain<1" audiomentations \\
    torch-audiomentations acoustics pronouncing "datasets<3" deep-phonemizer onnx onnxruntime pyyaml
echo "Instalado."
"""),
    md("""
## 3. Bajar los audios de apoyo

- Respuestas de cuartos reales (eco), ruido de fondo y música: para que funcione en casa, no solo en silencio.
- Rasgos ya calculados de ~2,000 horas de habla y ruido que **no** son "Hey Sokari" (unos 17 GB): con ellos aprende a no activarse con cualquier cosa.
"""),
    code("""
import os
from pathlib import Path
import numpy as np
import scipy.io.wavfile
import datasets
from tqdm.auto import tqdm
os.chdir("/content")

def guardar(carpeta, nombre, audio):
    scipy.io.wavfile.write(os.path.join(carpeta, nombre), 16000, (np.clip(audio, -1, 1) * 32767).astype(np.int16))

os.makedirs("mit_rirs", exist_ok=True)
if len(os.listdir("mit_rirs")) < 200:
    for row in tqdm(datasets.load_dataset("davidscripka/MIT_environmental_impulse_responses", split="train", streaming=True), desc="ecos"):
        guardar("mit_rirs", row["audio"]["path"].split("/")[-1], row["audio"]["array"])

os.makedirs("audioset_16k", exist_ok=True)
if len(os.listdir("audioset_16k")) < 500:
    if not Path("audioset/audio").exists():
        os.makedirs("audioset", exist_ok=True)
        !wget -q -O audioset/bal_train09.tar https://huggingface.co/datasets/agkphysics/AudioSet/resolve/main/bal_train09.tar
        !tar -xf audioset/bal_train09.tar -C audioset
    ds = datasets.Dataset.from_dict({"audio": [str(p) for p in Path("audioset/audio").glob("**/*.flac")]})
    for row in tqdm(ds.cast_column("audio", datasets.Audio(sampling_rate=16000)), desc="ruido"):
        guardar("audioset_16k", Path(row["audio"]["path"]).stem + ".wav", row["audio"]["array"])

os.makedirs("fma", exist_ok=True)
if len(os.listdir("fma")) < 100:
    fma = iter(datasets.load_dataset("rudraml/fma", name="small", split="train", streaming=True)
               .cast_column("audio", datasets.Audio(sampling_rate=16000)))
    for _ in tqdm(range(3600 // 30), desc="música"):
        row = next(fma)
        guardar("fma", row["audio"]["path"].split("/")[-1].replace(".mp3", ".wav"), row["audio"]["array"])

for f in ["openwakeword_features_ACAV100M_2000_hrs_16bit.npy", "validation_set_features.npy"]:
    if not os.path.exists(f) or os.path.getsize(f) < 1_000_000:
        !wget -q -O {f} https://huggingface.co/datasets/davidscripka/openwakeword_features/resolve/main/{f}
print("Audios listos.")
"""),
    md("""
## 4. Configurar el entrenamiento

`CALIDAD = "buena"` hace ~15,000 ejemplos (más lento, mejor). `"rapida"` sirve para probar que todo corre.
"""),
    code("""
import yaml
CALIDAD = "buena"
n, pasos = (15000, 40000) if CALIDAD == "buena" else (3000, 10000)
config = {
    "model_name": "hey_sokari",
    "target_phrase": ["hey sokari", "hey so kari"],
    # Frases que suenan parecido y NO deben activarlo (además, openWakeWord arma otras solo).
    # Ninguna demasiado parecida a "sokari": eso le enseñaría a rechazar tu propia pronunciación.
    "custom_negative_phrases": ["hey sakura", "hey safari", "hey sorry", "hey calgary", "hey siri", "hey karen",
                                "hey soccer", "hey sugar", "hey sophie"],
    "n_samples": n,
    "n_samples_val": max(1000, n // 10),
    "tts_batch_size": 50,
    "augmentation_batch_size": 16,
    "augmentation_rounds": 1,
    "piper_sample_generator_path": "./piper-sample-generator",
    "output_dir": "./modelo",
    "rir_paths": ["./mit_rirs"],
    "background_paths": ["./audioset_16k", "./fma"],
    "background_paths_duplication_rate": [1, 1],
    "false_positive_validation_data_path": "validation_set_features.npy",
    "feature_data_files": {"ACAV100M_sample": "openwakeword_features_ACAV100M_2000_hrs_16bit.npy"},
    "batch_n_per_class": {"ACAV100M_sample": 1024, "adversarial_negative": 50, "positive": 50},
    # El modelo que Sokari sabe leer: "dnn" con una capa oculta.
    "model_type": "dnn",
    "layer_size": 32,
    "steps": pasos,
    "max_negative_weight": 1500,
    "target_false_positives_per_hour": 0.2,
}
with open("hey_sokari.yaml", "w") as f:
    yaml.dump(config, f)
print(open("hey_sokari.yaml").read())
"""),
    md("## 5. Generar voces, mezclarlas con ruido y entrenar"),
    code("""
%%bash
cd /content
set -e
python openWakeWord/openwakeword/train.py --training_config hey_sokari.yaml --generate_clips
python openWakeWord/openwakeword/train.py --training_config hey_sokari.yaml --augment_clips
# Al final intenta pasarlo también a tflite; si solo eso falla no importa: Sokari usa el .onnx.
python openWakeWord/openwakeword/train.py --training_config hey_sokari.yaml --train_model || true
test -s modelo/hey_sokari.onnx || { echo "No se generó modelo/hey_sokari.onnx: revisa los mensajes de arriba."; exit 1; }
ls -la modelo/hey_sokari.onnx
"""),
    md("## 6. Convertir al formato de Sokari (se revisa solo)"),
    code("%%writefile onnx_a_jww.py\n" + convertidor),
    code("""
import subprocess, sys
r = subprocess.run([sys.executable, "onnx_a_jww.py", "modelo/hey_sokari.onnx", "hey_sokari.jww"], capture_output=True, text=True)
print(r.stdout + r.stderr)
assert r.returncode == 0, "La conversión falló: manda este mensaje a Claude."
from google.colab import files
files.download("hey_sokari.jww")
"""),
    md("""
## 7. Qué sigue

1. En GitHub abre el repo **sokari** → carpeta **`res`** → *Add file → Upload files*.
2. Arrastra **`hey_sokari.jww`**, elige *Create a new branch* y dale *Propose changes* → *Create pull request*.
3. El CI arma un `Sokari.exe` que ya trae "Hey Sokari". Bájalo de la pestaña *Actions* (artefacto **Sokari-exe**) y pruébalo unos días.

Si se activa solo con otras palabras o le cuesta oírte, se reentrena con más ejemplos o con otras frases en `custom_negative_phrases`.
"""),
]

nb = {
    "cells": cells,
    "metadata": {"accelerator": "GPU", "colab": {"provenance": [], "gpuType": "T4"},
                 "kernelspec": {"name": "python3", "display_name": "Python 3"},
                 "language_info": {"name": "python"}},
    "nbformat": 4,
    "nbformat_minor": 0,
}
with open(os.path.join(AQUI, "entrenar_hey_sokari.ipynb"), "w", encoding="utf-8") as f:
    json.dump(nb, f, ensure_ascii=False, indent=1)
    f.write("\n")
print("Listo: herramientas/entrenar_hey_sokari.ipynb")
