"""Rasgos de openWakeWord calculados igual que en vivo (bloques de 80 ms, como
wakeword.c y AudioFeatures en streaming), pero en lote para que sea rápido."""
import numpy as np
import onnxruntime as ort
import os

MODELS = "/home/user/dscripka/openwakeword/openwakeword/resources/models"
CHUNK, CTX = 1280, 480
_opts = ort.SessionOptions()
_opts.intra_op_num_threads = int(os.environ.get("RASGOS_HILOS", "4"))
_opts.inter_op_num_threads = 1
_mel = ort.InferenceSession(f"{MODELS}/melspectrogram.onnx", _opts, providers=["CPUExecutionProvider"])
_emb = ort.InferenceSession(f"{MODELS}/embedding_model.onnx", _opts, providers=["CPUExecutionProvider"])


def _load_mel():
    """Pesos del mel que usa Sokari (res/wakeword.bin): DFT real/imaginaria y
    banco de filtros. Así la cuenta es la de wakeword.c."""
    import struct
    d = open("/home/user/jarvis/res/wakeword.bin", "rb").read()
    n, off, t = struct.unpack("<I", d[4:8])[0], 8, {}
    for _ in range(n):
        ln = struct.unpack("<I", d[off:off + 4])[0]; off += 4
        name = d[off:off + ln].decode(); off += ln
        nd = struct.unpack("<I", d[off:off + 4])[0]; off += 4
        dims = struct.unpack(f"<{nd}I", d[off:off + 4 * nd]); off += 4 * nd
        cnt = int(np.prod(dims))
        if name.startswith("mel."):
            t[name] = np.frombuffer(d[off:off + 4 * cnt], "<f4").reshape(dims).astype(np.float32)
        off += 4 * cnt
    return t["mel.real"].T, t["mel.imag"].T, t["mel.melW"]


_RE, _IM, _MW = _load_mel()


def _melspec_frames(frames):
    """frames: (..., 512) -> dB (..., 32) sin el tope de 80 dB."""
    re, im = frames @ _RE, frames @ _IM
    mel = (re * re + im * im) @ _MW
    return 10 * np.log10(np.maximum(mel, 1e-10))


def _melspec_chunks(x):
    """x: int16 de largo múltiplo de 1280. Lista de mels por bloque: el primero
    da 5 filas y los demás 8 (como en vivo); el tope de 80 dB es por bloque."""
    x = x.astype(np.float32)
    n = len(x) // CHUNK
    out = []
    f0 = np.stack([x[i * 160:i * 160 + 512] for i in range(5)])
    m0 = _melspec_frames(f0)
    out.append(np.maximum(m0, m0.max() - 80) / 10 + 2)
    if n > 1:
        starts = np.arange(1, n)[:, None] * CHUNK - CTX + (np.arange(8) * 160)[None, :]  # (n-1, 8)
        idx = starts[..., None] + np.arange(512)[None, None, :]
        m = _melspec_frames(x[idx])  # (n-1, 8, 32)
        top = m.max(axis=(1, 2), keepdims=True) - 80
        m = np.maximum(m, top) / 10 + 2
        out.extend(list(m))
    return [o.astype(np.float32) for o in out]


def stream_embeddings(x):
    """Embeddings (n_bloques, 96): el que se agrega tras cada bloque de 80 ms,
    partiendo del estado inicial de Sokari (buffer de mel en unos)."""
    x = np.asarray(x)
    x = x[:len(x) // CHUNK * CHUNK]
    mels = _melspec_chunks(x)
    stream = np.vstack([np.ones((76, 32), np.float32)] + mels)
    ends = np.cumsum([len(m) for m in mels]) + 76
    wins = np.stack([stream[e - 76:e] for e in ends]).astype(np.float32)[..., None]
    out = []
    for i in range(0, len(wins), 512):
        out.append(_emb.run(None, {"input_1": wins[i:i + 512]})[0].reshape(-1, 96))
    return np.vstack(out)


def windows(emb, stride=1, n=16):
    """Ventanas de 16 embeddings seguidos, como las ve el clasificador."""
    idx = np.arange(0, len(emb) - n + 1, stride)
    return np.stack([emb[i:i + n] for i in idx]) if len(idx) else np.zeros((0, n, 96), np.float32)
