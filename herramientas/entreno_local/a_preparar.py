"""Ecos de cuartos (sintéticos), ruidos y música, a 16 kHz. Separa lo de
entrenamiento de lo que se guarda para la prueba final."""
import csv
import os
import random

import numpy as np
import pyroomacoustics as pra

from comun import DATOS, SR, escribir, leer, lista

rng = np.random.default_rng(1234)
random.seed(1234)


def rirs(n=300):
    out = f"{DATOS}/rirs"
    if len(lista(f"{out}/*.wav")) >= n:
        return
    for i in range(n):
        dims = [rng.uniform(3, 8), rng.uniform(2.5, 6), rng.uniform(2.4, 3.2)]
        rt60 = rng.uniform(0.15, 0.8)
        e_abs, max_order = pra.inverse_sabine(rt60, dims)
        room = pra.ShoeBox(dims, fs=SR, materials=pra.Material(e_abs), max_order=min(max_order, 14))
        pos = lambda: [rng.uniform(0.4, d - 0.4) for d in dims[:2]] + [rng.uniform(0.8, 1.8)]
        src, mic = pos(), pos()
        while np.linalg.norm(np.subtract(src, mic)) < 0.5:
            mic = pos()
        room.add_source(src)
        room.add_microphone(mic)
        room.compute_rir()
        h = np.asarray(room.rir[0][0], dtype=np.float32)
        h = h / np.max(np.abs(h))
        escribir(f"{out}/rir_{i:03d}.wav", h * 0.99)
    print("ecos:", n)


def trozos(x, seg):
    n = len(x) // (seg * SR)
    return [x[i * seg * SR:(i + 1) * seg * SR] for i in range(max(n, 1))]


def ruido_sintetico(n=60, seg=10):
    out = f"{DATOS}/fondos/sintetico"
    if len(lista(f"{out}/*.wav")) >= n:
        return
    t = np.arange(seg * SR) / SR
    for i in range(n):
        kind = i % 5
        w = rng.standard_normal(seg * SR)
        if kind == 0:  # blanco
            x = w
        elif kind in (1, 2):  # rosa / café (ventilador, aire)
            f = np.fft.rfftfreq(len(w), 1 / SR)
            spec = np.fft.rfft(w) / np.maximum(f, 20) ** (0.5 if kind == 1 else 1.0)
            x = np.fft.irfft(spec, len(w))
        elif kind == 3:  # zumbido eléctrico 50/60 Hz con armónicos
            f0 = rng.choice([50, 60])
            x = sum(rng.uniform(0.2, 1) / k * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 6)) for k in range(1, 8))
            x = x + 0.05 * w
        else:  # ventilador de PC: ruido filtrado + tono de aspas
            f = np.fft.rfftfreq(len(w), 1 / SR)
            spec = np.fft.rfft(w) * np.exp(-f / rng.uniform(300, 1500))
            x = np.fft.irfft(spec, len(w)) + 0.3 * np.sin(2 * np.pi * rng.uniform(80, 250) * t)
        x = x / np.max(np.abs(x)) * rng.uniform(0.2, 0.9)
        escribir(f"{out}/sint_{i:03d}.wav", x.astype(np.float32))
    print("ruido sintético:", n)


def speech_commands_ruido():
    out = f"{DATOS}/fondos/sc_ruido"
    if lista(f"{out}/*.wav"):
        return
    for p in lista("/home/user/datos/speech_commands/_background_noise_/*.wav"):
        for j, c in enumerate(trozos(leer(p), 10)):
            escribir(f"{out}/{os.path.basename(p)[:-4]}_{j:02d}.wav", c)


def esc50():
    base = "/home/user/karolpiczak/esc-50"
    rows = list(csv.DictReader(open(f"{base}/meta/esc50.csv")))
    for r in rows:
        dest = f"{DATOS}/{'prueba' if r['fold'] == '5' else 'fondos'}/esc50/{r['filename']}"
        if not os.path.exists(dest):
            escribir(dest, leer(f"{base}/audio/{r['filename']}"))
    print("esc50:", len(rows))


def musica():
    pistas = lista("/home/user/datos/musica_deb/x/usr/share/games/wesnoth/1.16/data/core/music/*.ogg") + \
        lista("/home/user/datos/musica_deb/x/usr/share/games/etr/music/*.ogg") + \
        lista("/home/user/datos/musica_deb/x/usr/share/games/frozen-bubble/snd/*.ogg")
    random.shuffle(pistas)
    n_prueba = max(1, len(pistas) // 6)
    for k, p in enumerate(pistas):
        dest_dir = f"{DATOS}/{'prueba' if k < n_prueba else 'fondos'}/musica"
        nombre = os.path.basename(p)[:-4]
        if lista(f"{dest_dir}/{nombre}_*.wav"):
            continue
        x = leer(p)
        if len(x) < 3 * SR:
            continue
        for j, c in enumerate(trozos(x, 30)):
            escribir(f"{dest_dir}/{nombre}_{j:02d}.wav", c)
    print("música:", len(pistas), "pistas,", n_prueba, "para prueba")


def habla_fondo(n=700):
    """Gente hablando atrás (tele, otra persona): FLEURS de entrenamiento."""
    out = f"{DATOS}/fondos/habla"
    if len(lista(f"{out}/*.wav")) >= 2 * n:
        return
    for lang, d in [("es", "fleurs_es/es_419"), ("en", "fleurs_en/en_us")]:
        fs = lista(f"/home/user/datos/{d}/audio/train/*.wav")
        for p in random.sample(fs, n):
            escribir(f"{out}/{lang}_{os.path.basename(p)}", leer(p))
    print("habla de fondo:", 2 * n)


if __name__ == "__main__":
    rirs()
    ruido_sintetico()
    speech_commands_ruido()
    esc50()
    musica()
    habla_fondo()
    for d in sorted(set(os.path.dirname(p) for p in lista(f"{DATOS}/*/*/*.wav") + lista(f"{DATOS}/*/*.wav"))):
        print(d, len(lista(f"{d}/*.wav")))
