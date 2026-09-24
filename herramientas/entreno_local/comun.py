import glob
import math
import os

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

DATOS = "/home/user/entreno/datos"
SR = 16000


def leer(path, sr=SR):
    """Audio mono float32 a 16 kHz."""
    x, s = sf.read(path, dtype="float32", always_2d=True)
    x = x.mean(axis=1)
    if s != sr:
        g = math.gcd(s, sr)
        x = resample_poly(x, sr // g, s // g).astype(np.float32)
    return x


def escribir(path, x, sr=SR):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sf.write(path, np.clip(x, -1, 1), sr, subtype="PCM_16")


def a_int16(x):
    return (np.clip(x, -1, 1) * 32767).astype(np.int16)


def lista(pat):
    return sorted(glob.glob(pat))
