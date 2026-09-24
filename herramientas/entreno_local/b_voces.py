"""Genera los "hey sokari" sintéticos y las frases que NO deben activarlo.

Voces de entrenamiento y de prueba separadas:
- LibriTTS-R (904 hablantes, mezclados de a dos): entrenamiento 0-799, prueba 800-903.
- Piper en español: carlfm y mls_9972 para entrenar; mls_10246 solo para prueba.
"""
import sys

sys.path.insert(0, "/home/user/entreno")
sys.path.insert(0, "/home/user/rhasspy/psg-v2")
import parches  # noqa: F401  (antes que openWakeWord y el generador)

import csv
import json
import os
import random

import numpy as np
import onnxruntime as ort
import soundfile as sf
import torch
import torchaudio
from piper_phonemize import phonemize_espeak

import generate_samples as gs
from openwakeword.data import generate_adversarial_texts

CLIPS = "/home/user/entreno/clips"
LIBRITTS = "/home/user/rhasspy/psg-v2/models/en_US-libritts_r-medium.pt"
VOCES_ES = "/home/user/datos/voces_es"
torch.set_num_threads(4)

# ---------------------------------------------------------------- frases
# Cómo lo dice alguien en inglés...
POS_EN = ["hey sokari", "hey so kari", "hey sowkari", "hey soh kari", "hey, sokari"]
# ...y como lo decimos en español (fonemas directos: o, a y la r suave "ɾ",
# que el modelo en inglés sí conoce por palabras como "better").
POS_FONEMAS = ["/hˈeɪ sokˈaɾi", "/ˈeɪ sokˈaɾi", "/hˈeɪ soʊkˈɑːɾi", "/hˈeɪ sˌoʊkˈɑːɾi", "/ˈeɪ sˌoʊkˈɑːɾi",
               "/hˈeɪ sokˈɑːɾi", "/hˈeɪ, sokˈaɾi", "/ˈeɪ, sokˈaɾi", "/hˌeɪ sokˈaɾi", "/hˈeɪ soʊkˈaɾi"]
POS_ES = ["hey sokari", "jey sokari", "ey sokari", "hey, sokari", "jey, sokari", "ey, sokari"]

# Parecidas pero distintas. Ninguna trae "sokari": no quiero que aprenda a
# rechazar tu forma de decirlo.
NEG_EN = ["hey sakura", "hey safari", "hey sorry", "hey calgary", "hey siri", "hey karen", "hey soccer",
          "hey sugar", "hey sophie", "hey jarvis", "hey google", "okay google", "alexa", "hey cortana",
          "hey sarah", "hey carrie", "hey so", "so sorry", "hey sergei", "hey socrates", "sure thing",
          "hey there", "hey buddy", "okay sorry", "hey cory", "hey kari", "hey scary", "hey salary"]
NEG_ES = ["oye socorro", "ey sacar", "ey Zacarías", "ey Sócrates", "ey sonaría", "ey cariño", "ok gracias",
          "oye Karina", "hey Sara", "ey socio", "sí claro", "ya casi", "hay calor", "eso es", "ey Carolina",
          "ey Sakura", "oye Siri", "así sería", "hoy sí", "ey Sonia", "qué sería", "oye cállate", "ay caray",
          "ya sé", "a ver", "okey", "oye Alexa", "ey Jarvis", "sácalo", "socarrón", "Zacarías", "ey Carlos"]


def adversarias(n, seed):
    np.random.seed(seed)
    out = []
    for t in ["hey sokari", "hey so kari"]:
        out += generate_adversarial_texts(input_text=t, N=n, include_partial_phrase=1.0, include_input_words=0.2)
    out = [t for t in out if "sokari" not in t.split() and "so kari" not in t]
    return out


def frases_fleurs(lang, split, n, seed):
    d = {"es": "fleurs_es/es_419", "en": "fleurs_en/en_us"}[lang]
    rows = [r.split("\t") for r in open(f"/home/user/datos/{d}/{split}.tsv", encoding="utf-8").read().splitlines()]
    textos = sorted(set(r[2] for r in rows if len(r) > 2 and 30 < len(r[2]) < 160))
    random.Random(seed).shuffle(textos)
    return textos[:n]


# ---------------------------------------------------------------- LibriTTS-R
_model = None
_config = None
_resampler = torchaudio.transforms.Resample(22050, 16000, lowpass_filter_width=64, rolloff=0.9475937167399596,
                                            resampling_method="sinc_interp_kaiser", beta=14.769656459379492)


def _ids(text, id_map, voice):
    if text.startswith("/"):
        fon = list(text[1:])
    else:
        fon = [p for s in phonemize_espeak(text, voice) for p in s]
    ids = list(id_map["^"])
    for p in fon:
        if p in id_map:
            ids += id_map[p] + id_map["_"]
    return ids + id_map["$"]


def libritts(textos, n, out_dir, hablantes, seed, batch=32):
    global _model, _config
    if _model is None:
        _model = torch.load(LIBRITTS)
        _model.eval()
        _config = json.load(open(LIBRITTS + ".json"))
    os.makedirs(out_dir, exist_ok=True)
    hechos = len([f for f in os.listdir(out_dir) if f.endswith(".wav")])
    rng = random.Random(seed + hechos)
    torch.manual_seed(seed + hechos)
    meta = open(f"{out_dir}/textos.tsv", "a", encoding="utf-8")
    i = hechos
    while i < n:
        b = min(batch, n - i)
        sp1 = torch.LongTensor([rng.choice(hablantes) for _ in range(b)])
        sp2 = torch.LongTensor([rng.choice(hablantes) for _ in range(b)])
        txt = [rng.choice(textos) for _ in range(b)]
        ids = [_ids(t, _config["phoneme_id_map"], "en-us") for t in txt]
        m = max(len(x) for x in ids)
        ids = [x + [1] * (m - len(x)) for x in ids]
        slerp = rng.choice([0.3, 0.5, 0.7])
        length = rng.choice([0.75, 0.85, 1.0, 1.15, 1.3])
        noise = rng.choice([0.667, 0.8, 0.98])
        noise_w = rng.choice([0.8, 0.98])
        with torch.no_grad():
            audio = gs.generate_audio(_model, sp1, sp2, ids, slerp, noise, noise_w, length, None)
            audio = _resampler(audio.cpu()).numpy()
        audio = gs.audio_float_to_int16(audio)
        for k in range(b):
            a = gs.remove_silence(audio[k].flatten())
            sf.write(f"{out_dir}/{i:06d}.wav", a, 16000, subtype="PCM_16")
            meta.write(f"{i:06d}\t{txt[k]}\t{int(sp1[k])}\t{int(sp2[k])}\t{slerp}\t{length}\n")
            i += 1
        meta.flush()
        if (i // batch) % 20 == 0:
            print(f"{out_dir}: {i}/{n}", flush=True)
    meta.close()


# ---------------------------------------------------------------- Piper en español
def piper_es(voz, textos, n, out_dir, seed):
    cfg = json.load(open(f"{VOCES_ES}/{voz}.onnx.json"))
    sess = ort.InferenceSession(f"{VOCES_ES}/{voz}.onnx", providers=["CPUExecutionProvider"])
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(seed)
    meta = open(f"{out_dir}/textos.tsv", "a", encoding="utf-8")
    hechos = len([f for f in os.listdir(out_dir) if f.startswith(voz)])
    for i in range(hechos, n):
        t = rng.choice(textos)
        ids = np.array([_ids(t, cfg["phoneme_id_map"], cfg["espeak"]["voice"])], dtype=np.int64)
        scales = np.array([rng.uniform(0.4, 0.9), rng.choice([0.8, 0.9, 1.0, 1.1, 1.25]), rng.uniform(0.6, 1.0)],
                          dtype=np.float32)
        a = sess.run(None, {"input": ids, "input_lengths": np.array([ids.shape[1]], dtype=np.int64),
                            "scales": scales})[0].squeeze()
        a = gs.remove_silence(gs.audio_float_to_int16(a).flatten())
        sf.write(f"{out_dir}/{voz}_{i:05d}.wav", a, 16000, subtype="PCM_16")
        meta.write(f"{voz}_{i:05d}\t{t}\n")
    meta.close()


# Fonemas del español llevados a los que el modelo en inglés sí aprendió.
_ES_A_EN = {"x": "h", "ʝ": "j", "ɲ": "nj", "r": "ɾ", "β": "b", "ɣ": "ɡ", "ʎ": "j"}


def fon_es(texto):
    fon = "".join(p for s in phonemize_espeak(texto, "es-419") for p in s)
    return "/" + "".join(_ES_A_EN.get(c, c) for c in fon)


def espeak_es(textos, n, out_dir, seed):
    """Voz robótica de espeak-ng en español, con sus variantes: solo para probar."""
    import glob
    import io
    import subprocess
    from comun import leer
    variantes = sorted(os.path.basename(p) for p in glob.glob("/usr/lib/x86_64-linux-gnu/espeak-ng-data/voices/!v/*"))
    os.makedirs(out_dir, exist_ok=True)
    rng = random.Random(seed)
    meta = open(f"{out_dir}/textos.tsv", "a", encoding="utf-8")
    for i in range(n):
        t, v = rng.choice(textos), rng.choice(variantes)
        wav = subprocess.run(["espeak-ng", "-v", f"es-419+{v}", "-s", str(rng.randint(120, 190)), "-p",
                              str(rng.randint(20, 80)), "--stdout", t], capture_output=True).stdout
        tmp = f"{out_dir}/_tmp.wav"
        open(tmp, "wb").write(wav)
        a = gs.remove_silence(gs.audio_float_to_int16(leer(tmp)).flatten())
        sf.write(f"{out_dir}/espeak_{i:05d}.wav", a, 16000, subtype="PCM_16")
        meta.write(f"espeak_{i:05d}\t{t}\t{v}\n")
    os.remove(tmp)
    meta.close()


if __name__ == "__main__":
    etapa = sys.argv[1]
    TRAIN, TEST = list(range(0, 800)), list(range(800, 904))
    NEG_ES_FON = [fon_es(t) for t in NEG_ES]
    if etapa == "entrenamiento":
        # Plan corto (créditos de la nube limitados).
        libritts(POS_EN * 2 + POS_FONEMAS, 6000, f"{CLIPS}/pos_train", TRAIN, 11)
        libritts(adversarias(4000, 21) + NEG_EN * 30 + NEG_ES_FON * 25, 6000, f"{CLIPS}/neg_train", TRAIN, 22)
    elif etapa == "prueba":
        libritts(POS_EN * 2 + POS_FONEMAS, 800, f"{CLIPS}/pos_test", TEST, 13)
        piper_es("es-carlfm-x-low", POS_ES, 300, f"{CLIPS}/pos_test_carlfm", 14)
        espeak_es(POS_ES, 200, f"{CLIPS}/pos_test_espeak", 15)
        libritts(adversarias(600, 24) + NEG_EN * 6 + NEG_ES_FON * 5, 800, f"{CLIPS}/neg_test", TEST, 25)
        piper_es("es-carlfm-x-low", NEG_ES, 200, f"{CLIPS}/neg_test_es", 26)
        espeak_es(NEG_ES, 100, f"{CLIPS}/neg_test_es", 27)
