"""Qué audio de validación dispara activaciones falsas: reconstruye el orden
de archivos de val_fleurs (mismo azar que c_rasgos_reales.py) y ubica cada
activación en su archivo y frase."""
import random
import sys

import numpy as np
import torch

sys.path.insert(0, "/home/user/entreno")
import c_rasgos_reales as C
import e_entrenar as E
from comun import SR, leer


def mapa_val_fleurs():
    rng = random.Random(77)
    archivos = C.fleurs("es", "dev") + C.fleurs("en", "dev")
    archivos = list(archivos)
    rng.shuffle(archivos)
    meta = int(2.0 * 3600 * SR)
    total, buf, bloques, actual = 0, 0, [], []
    for p in archivos:
        if total >= meta:
            break
        n = len(leer(p))
        rng.uniform(-12, 3)
        rng.randint(0, 1 << 30)
        gap = int(rng.uniform(0.0, 0.4) * SR)
        actual.append((p, buf, n))
        buf += n + gap
        total += n + gap
        if buf >= C.BLOQUE:
            bloques.append((actual, buf))
            actual, buf = [], 0
    if actual and buf > 3 * SR:
        bloques.append((actual, buf))
    # fila de embedding -> (archivo, segundo dentro del archivo)
    filas = []
    for archivos_b, largo in bloques:
        nfil = largo // 1280 - C.DESCARTE
        for r in range(nfil):
            fin = (r + C.DESCARTE + 1) * 1280  # la ventana termina aquí (muestras dentro del bloque)
            f = next(((p, (fin - ini) / SR) for p, ini, n in archivos_b if ini <= fin <= ini + n + 0.5 * SR), (None, 0))
            filas.append(f)
    return filas


def transcripciones():
    t = {}
    for d in ["fleurs_es/es_419", "fleurs_en/en_us"]:
        for line in open(f"/home/user/datos/{d}/dev.tsv", encoding="utf-8"):
            c = line.split("\t")
            if len(c) > 2:
                t[c[1]] = c[2]
    return t


if __name__ == "__main__":
    model = E.Net()
    model.load_state_dict(torch.load(sys.argv[1]))
    model.eval()
    e = np.load(f"{E.R}/val_fleurs.npy").astype(np.float32)
    filas = mapa_val_fleurs()
    print("filas reconstruidas", len(filas), "embeddings", len(e))
    s = E.puntajes(model, E.ventanas_todas(e))
    t = transcripciones()
    i = 0
    while i < len(s):
        if s[i] > E.UMBRAL:
            r = i + 15  # última fila de la ventana
            p, seg = filas[r] if r < len(filas) else (None, 0)
            nombre = p.split("/")[-1] if p else "?"
            print(f"{s[i]:.2f}  {nombre}  seg {seg:.1f}  {t.get(nombre, '')[:150]}")
            i += E.REFRACT
        else:
            i += 1
