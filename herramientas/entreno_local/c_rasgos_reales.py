"""Rasgos (embeddings en streaming, iguales a los de Sokari en vivo) de audio
real que NO es "hey sokari". Se guarda el flujo de embeddings (T, 96) de cada
fuente; las ventanas de 16 se arman al entrenar.

  entrenamiento: FLEURS es/en train, Speech Commands train, ESC-50 1-4, música, ruidos
  validacion:    FLEURS dev, Speech Commands validación   (para elegir el modelo)
  prueba:        FLEURS test, Speech Commands prueba, ESC-50 5, música aparte  (evaluación final)
"""
import os
import random
import sys

import numpy as np

sys.path.insert(0, "/home/user/entreno")
from comun import DATOS, SR, leer, lista
import rasgos

OUT = "/home/user/entreno/rasgos"
BLOQUE = 5 * 60 * SR      # se procesa de a 5 minutos...
DESCARTE = 20             # ...y se tiran los primeros embeddings (arranque del estado)
rng = random.Random(77)


def flujo(archivos, horas, nombre, gap=(0.0, 0.4), ganancia=(-12, 3)):
    """Concatena archivos (orden aleatorio) hasta `horas`, con ganancia al azar
    por archivo y pausas cortas, y calcula los embeddings."""
    dest = f"{OUT}/{nombre}.npy"
    if os.path.exists(dest):
        print(nombre, "ya existe")
        return
    archivos = list(archivos)
    rng.shuffle(archivos)
    meta = int(horas * 3600 * SR)
    partes, total, embs, usados = [], 0, [], 0
    buf_len = 0

    def procesar(partes):
        x = np.concatenate(partes)
        x = (np.clip(x, -1, 1) * 32767).astype(np.int16)
        e = rasgos.stream_embeddings(x)
        return e[DESCARTE:]

    for p in archivos:
        if total >= meta:
            break
        try:
            x = leer(p)
        except Exception as ex:
            print("no se pudo leer", p, ex)
            continue
        x = x * 10 ** (rng.uniform(*ganancia) / 20)
        pk = np.max(np.abs(x)) if len(x) else 0
        if pk > 1:
            x = x / pk * 0.98
        pausa = np.random.default_rng(rng.randint(0, 1 << 30)).normal(0, 3e-4, int(rng.uniform(*gap) * SR))
        partes += [x.astype(np.float32), pausa.astype(np.float32)]
        buf_len += len(x) + len(pausa)
        total += len(x) + len(pausa)
        usados += 1
        if buf_len >= BLOQUE:
            embs.append(procesar(partes))
            partes, buf_len = [], 0
            print(f"{nombre}: {total / SR / 3600:.2f} h", flush=True)
    if partes and buf_len > 3 * SR:
        embs.append(procesar(partes))
    e = np.vstack(embs).astype(np.float16)
    os.makedirs(OUT, exist_ok=True)
    np.save(dest, e)
    print(f"{nombre}: {usados} archivos, {total / SR / 3600:.2f} h, {len(e)} embeddings", flush=True)


def fleurs(lang, split):
    d = {"es": "fleurs_es/es_419", "en": "fleurs_en/en_us"}[lang]
    return lista(f"/home/user/datos/{d}/audio/{split}/*.wav")


def speech_commands(cual):
    base = "/home/user/datos/speech_commands"
    val = set(open(f"{base}/validation_list.txt").read().split())
    test = set(open(f"{base}/testing_list.txt").read().split())
    todos = [p[len(base) + 1:] for p in lista(f"{base}/*/*.wav") if "_background_noise_" not in p]
    sel = {"train": [p for p in todos if p not in val and p not in test], "val": sorted(val), "test": sorted(test)}[cual]
    return [f"{base}/{p}" for p in sel]

def usados_en_entrenamiento():
    """Repite el azar de la etapa de entrenamiento para saber qué archivos de
    FLEURS ya entraron (sin volver a calcular nada)."""
    import soundfile as sf
    r = random.Random(77)
    usados = set()
    for archivos, horas in [(fleurs("es", "train"), 6.0), (fleurs("en", "train"), 3.0)]:
        archivos = list(archivos)
        r.shuffle(archivos)
        total, meta = 0, int(horas * 3600 * SR)
        for p in archivos:
            if total >= meta:
                break
            n = sf.info(p).frames
            r.uniform(-12, 3)
            r.randint(0, 1 << 30)
            total += n + int(r.uniform(0.0, 0.4) * SR)
            usados.add(p)
    return usados


def mas_negativos():
    usados = usados_en_entrenamiento()
    print("archivos ya usados:", len(usados), flush=True)
    flujo([p for p in fleurs("en", "train") if p not in usados], 11.0, "train_fleurs_en_2")
    flujo([p for p in fleurs("es", "train") if p not in usados], 5.0, "train_fleurs_es_2")


if __name__ == "__main__":
    etapa = sys.argv[1]
    if etapa == "validacion":
        flujo(fleurs("es", "dev") + fleurs("en", "dev"), 2.0, "val_fleurs")
        flujo(speech_commands("val"), 0.8, "val_sc", gap=(0.1, 0.8))
    elif etapa == "prueba":
        flujo(fleurs("es", "test"), 2.5, "test_fleurs_es")
        flujo(fleurs("en", "test"), 1.5, "test_fleurs_en")
        flujo(speech_commands("test"), 0.8, "test_sc", gap=(0.1, 0.8))
        flujo(lista(f"{DATOS}/prueba/esc50/*.wav"), 1.0, "test_esc50")
        flujo(lista(f"{DATOS}/prueba/musica/*.wav"), 1.0, "test_musica")
    elif etapa == "mas":
        mas_negativos()
    elif etapa == "entrenamiento":
        flujo(fleurs("es", "train"), 6.0, "train_fleurs_es")
        flujo(fleurs("en", "train"), 3.0, "train_fleurs_en")
        flujo(speech_commands("train"), 2.5, "train_sc", gap=(0.1, 0.8))
        flujo(lista(f"{DATOS}/fondos/esc50/*.wav"), 3.0, "train_esc50")
        flujo(lista(f"{DATOS}/fondos/musica/*.wav"), 3.0, "train_musica")
        flujo(lista(f"{DATOS}/fondos/sintetico/*.wav") + lista(f"{DATOS}/fondos/sc_ruido/*.wav"), 1.0,
              "train_ruido", gap=(0, 0.1), ganancia=(-25, 0))
