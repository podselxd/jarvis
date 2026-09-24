"""Mezcla cada clip sintético con eco, ruido, música o gente hablando, y
saca los rasgos en streaming (igual que Sokari en vivo).

Cada clip va en un lienzo de 3.2 s y termina cerca del final; el clasificador
ve las últimas 16 filas (~2 s). Entrenamiento y prueba usan ruidos distintos.
"""
import os
import random
import sys

import numpy as np
from scipy.signal import fftconvolve, lfilter

sys.path.insert(0, "/home/user/entreno")
from comun import DATOS, SR, lista
import rasgos
import soundfile as sf

OUT = "/home/user/entreno/rasgos"
CLIPS = "/home/user/entreno/clips"
LIENZO = 40 * 1280  # 3.2 s
POR_TANDA = 150


def cargar(paths):
    out = []
    for p in paths:
        x, _ = sf.read(p, dtype="int16")
        if x.ndim > 1:
            x = x[:, 0]
        if len(x) >= SR:
            out.append(x)
    return out


class Mezclador:
    def __init__(self, fondos, rirs, seed):
        self.fondos = cargar(fondos)
        self.rirs = [sf.read(p, dtype="float32")[0] for p in rirs]
        self.r = np.random.default_rng(seed)
        print(f"fondos: {len(self.fondos)} archivos, {sum(map(len, self.fondos)) / SR / 3600:.1f} h", flush=True)

    def fondo(self, n):
        x = self.fondos[self.r.integers(len(self.fondos))]
        while len(x) < n:
            x = np.concatenate([x, self.fondos[self.r.integers(len(self.fondos))]])
        i = self.r.integers(0, len(x) - n + 1)
        return x[i:i + n].astype(np.float32) / 32768

    def aumentar(self, voz, fin_min, fin_max, snr=(0, 20), p_fondo=0.75):
        r = self.r
        x = voz.astype(np.float32) / 32768
        if r.random() < 0.3:  # velocidad/tono (±2 semitonos, como hablar más agudo o grave)
            f = 2 ** (r.uniform(-2, 2) / 12)
            idx = np.arange(0, len(x) - 1, f)
            x = np.interp(idx, np.arange(len(x)), x).astype(np.float32)
        if r.random() < 0.25:  # ecualización: un filtro de un polo al azar (micrófonos distintos)
            a = r.uniform(-0.9, 0.9)
            y = lfilter([1.0], [1.0, -a * 0.5], x)
            x = (y / (np.max(np.abs(y)) + 1e-9) * np.max(np.abs(x))).astype(np.float32)
        if r.random() < 0.2:  # distorsión suave (micrófono saturado)
            k = r.uniform(1, 4)
            x = np.tanh(k * x) / np.tanh(k)
        if r.random() < 0.5:  # eco de cuarto
            h = self.rirs[r.integers(len(self.rirs))]
            y = fftconvolve(x, h)[:len(x) + int(0.3 * SR)]
            x = (y / (np.max(np.abs(y)) + 1e-9) * np.max(np.abs(x))).astype(np.float32)
        lienzo = np.zeros(LIENZO, np.float32)
        fin = LIENZO - int(r.uniform(fin_min, fin_max) * SR)
        ini = fin - len(x)
        if ini < 0:
            x, ini = x[-ini:], 0
        lienzo[ini:fin] += x[:fin - ini]
        pot_voz = np.mean(x ** 2) + 1e-10
        if r.random() < p_fondo:
            b = self.fondo(LIENZO)
            pot_b = np.mean(b ** 2) + 1e-10
            lienzo += b * np.sqrt(pot_voz / pot_b / 10 ** (r.uniform(*snr) / 10))
        if r.random() < 0.25:  # ruido de fondo leve siempre presente en un micrófono barato
            lienzo += r.normal(0, 1, LIENZO).astype(np.float32) * np.sqrt(pot_voz / 10 ** (r.uniform(20, 40) / 10))
        pico = np.max(np.abs(lienzo)) + 1e-9
        lienzo = lienzo / pico * 10 ** (r.uniform(-20, -1) / 20)
        return (lienzo * 32767).astype(np.int16)


def procesar(mez, carpetas, nombre, rondas=1, filas=16, fin=(0.0, 0.24), **kw):
    dest = f"{OUT}/{nombre}.npy"
    if os.path.exists(dest):
        print(nombre, "ya existe")
        return
    paths = [p for c in carpetas for p in lista(f"{CLIPS}/{c}/*.wav")] * rondas

    out = []
    for i in range(0, len(paths), POR_TANDA):
        lote = []
        for p in paths[i:i + POR_TANDA]:
            v, _ = sf.read(p, dtype="int16")
            lote.append(mez.aumentar(v, fin[0], fin[1], **kw))
        e = rasgos.stream_embeddings(np.concatenate(lote))
        n = LIENZO // 1280
        for k in range(len(lote)):
            out.append(e[(k + 1) * n - filas:(k + 1) * n])
        print(f"{nombre}: {min(i + POR_TANDA, len(paths))}/{len(paths)}", flush=True)
    np.save(dest, np.stack(out).astype(np.float16))
    print(f"{nombre}: {len(out)} ejemplos", flush=True)


if __name__ == "__main__":
    etapa = sys.argv[1]
    rirs = lista(f"{DATOS}/rirs/*.wav")
    if etapa == "entrenamiento":
        fondos = lista(f"{DATOS}/fondos/*/*.wav")
        mez = Mezclador(fondos, rirs[:250], 101)
        procesar(mez, ["pos_train"], "pos_train", rondas=2)
        procesar(mez, ["neg_train"], "neg_train_tts")
        procesar(mez, ["pos_test"], "pos_val", filas=24, fin=(0.1, 0.5))  # para elegir el modelo
    elif etapa == "prueba":
        # Ruidos que el modelo nunca oyó: ESC-50 pliegue 5, música aparte y habla de FLEURS test.
        habla = lista("/home/user/datos/fleurs_es/es_419/audio/test/*.wav")[:150] + \
            lista("/home/user/datos/fleurs_en/en_us/audio/test/*.wav")[:150]
        fondos = lista(f"{DATOS}/prueba/*/*.wav") + habla
        for cond, kw in [("limpio", dict(p_fondo=0.0)), ("ruido", dict(snr=(5, 15), p_fondo=1.0))]:
            mez = Mezclador(fondos, rirs[250:], 202)
            for c in ["pos_test", "pos_test_carlfm", "pos_test_espeak", "neg_test", "neg_test_es"]:
                procesar(mez, [c], f"{c}_{cond}", filas=24, fin=(0.1, 0.5), **kw)
