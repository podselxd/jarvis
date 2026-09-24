"""Entrena el clasificador de "Hey Sokari" (el modelo "dnn" de openWakeWord,
32 neuronas) con los rasgos ya calculados, y elige el mejor punto con datos
de validación (nunca con los de la prueba final).

Umbral de trabajo: 0.4 (sensibilidad 67 de Sokari, la de fábrica)."""
import glob
import json
import os
import sys

import numpy as np
import torch
import torch.nn as nn

R = "/home/user/entreno/rasgos"
UMBRAL = 0.4
REFRACT = 25  # 2 s sin volver a contar (Sokari se pone a escuchar tu orden)
torch.set_num_threads(4)


class FCNBlock(nn.Module):
    def __init__(self, d):
        super().__init__()
        self.fcn_layer = nn.Linear(d, d)
        self.relu = nn.ReLU()
        self.layer_norm = nn.LayerNorm(d)

    def forward(self, x):
        return self.relu(self.layer_norm(self.fcn_layer(x)))


class Net(nn.Module):
    """Mismos nombres de capas que openWakeWord: el convertidor los reconoce."""
    def __init__(self, d=32):
        super().__init__()
        self.flatten = nn.Flatten()
        self.layer1 = nn.Linear(16 * 96, d)
        self.relu1 = nn.ReLU()
        self.layernorm1 = nn.LayerNorm(d)
        self.blocks = nn.ModuleList([FCNBlock(d)])
        self.last_layer = nn.Linear(d, 1)
        self.last_act = nn.Sigmoid()

    def logits(self, x):
        x = self.relu1(self.layernorm1(self.layer1(self.flatten(x))))
        for b in self.blocks:
            x = b(x)
        return self.last_layer(x)

    def forward(self, x):
        return self.last_act(self.logits(x))


def flujos(prefijo):
    return {os.path.basename(p)[:-4]: np.load(p).astype(np.float32) for p in sorted(glob.glob(f"{R}/{prefijo}_*.npy"))
            if p.endswith(".npy") and np.load(p, mmap_mode="r").ndim == 2}


def ventanas_todas(e):
    idx = np.arange(len(e) - 15)[:, None] + np.arange(16)[None]
    return e[idx]


@torch.no_grad()
def puntajes(model, x, lote=65536):
    return np.concatenate([model(torch.from_numpy(x[i:i + lote])).numpy().reshape(-1) for i in range(0, len(x), lote)])


def eventos(s, umbral=UMBRAL):
    """Activaciones como las contaría Sokari: cruzar el umbral y 2 s de pausa."""
    n, i = 0, 0
    while i < len(s):
        if s[i] > umbral:
            n += 1
            i += REFRACT
        else:
            i += 1
    return n


@torch.no_grad()
def recall(model, pos24, umbral=UMBRAL):
    """pos24: (N, 24, 96). Detectado si alguna de las 9 ventanas finales pasa."""
    w = np.stack([pos24[:, k:k + 16] for k in range(9)], 1).reshape(-1, 16, 96)
    s = puntajes(model, w).reshape(len(pos24), 9).max(1)
    return float((s > umbral).mean())


def fa_por_hora(model, streams, umbral=UMBRAL):
    total_ev, total_h, det = 0, 0.0, {}
    for k, e in streams.items():
        s = puntajes(model, ventanas_todas(e))
        ev, h = eventos(s, umbral), len(e) * 0.08 / 3600
        det[k] = (ev, round(h, 2))
        total_ev += ev
        total_h += h
    return total_ev / total_h, det


def entrenar(peso_max=500, pasos=20000, d=32, semilla=0, log=print, dropout=0.0, wd=0.0, lr=1e-3, frac_es=None):
    torch.manual_seed(semilla)
    rng = np.random.default_rng(semilla)
    pos = np.load(f"{R}/pos_train.npy").astype(np.float32)
    adv = np.load(f"{R}/neg_train_tts.npy").astype(np.float32)
    pos_es = None
    if os.path.exists(f"{R}/pos_train_es.npy"):  # otros sintetizadores en español
        pos_es = np.load(f"{R}/pos_train_es.npy").astype(np.float32)
        if frac_es is None:
            pos = np.concatenate([pos, pos_es])
        adv = np.concatenate([adv, np.load(f"{R}/neg_train_es_tts.npy").astype(np.float32)])
    reales = flujos("train")
    for k in [k for k in reales if k.endswith("_2")]:  # tandas extra de la misma fuente
        reales[k[:-2]] = np.concatenate([reales[k[:-2]], reales.pop(k)])
    # Cuánto pesa cada fuente en cada lote de negativos reales.
    mezcla = {"train_fleurs_es": 0.27, "train_fleurs_en": 0.28, "train_sc": 0.13, "train_esc50": 0.10,
              "train_musica": 0.14, "train_ruido": 0.08}
    fuentes = [k for k in mezcla if k in reales]
    p = np.array([mezcla[k] for k in fuentes]); p /= p.sum()
    # Las primeras 400 frases de prueba de LibriTTS eligen el modelo; las otras 400 quedan para la prueba final.
    val_pos = np.load(f"{R}/pos_val.npy").astype(np.float32)[:400]
    val_es = np.load(f"{R}/pos_val_es.npy").astype(np.float32) if os.path.exists(f"{R}/pos_val_es.npy") else None
    val_streams = flujos("val")
    model = Net(d)
    opt = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=wd)
    sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=lr, total_steps=pasos, pct_start=0.1)
    N_REAL, N_ADV, N_POS = 1024, 128, 128
    mejores = []
    for paso in range(pasos):
        cuantos = rng.multinomial(N_REAL, p)
        partes = []
        for k, c in zip(fuentes, cuantos):
            e = reales[k]
            ini = rng.integers(0, len(e) - 16, c)
            partes.append(e[ini[:, None] + np.arange(16)[None]])
        neg = np.concatenate(partes + [adv[rng.integers(0, len(adv), N_ADV)]])
        if frac_es is not None and pos_es is not None:
            n_es = int(N_POS * frac_es)
            ps = np.concatenate([pos[rng.integers(0, len(pos), N_POS - n_es)], pos_es[rng.integers(0, len(pos_es), n_es)]])
        else:
            ps = pos[rng.integers(0, len(pos), N_POS)]
        x = torch.from_numpy(np.concatenate([ps, neg]))
        y = torch.cat([torch.ones(N_POS), torch.zeros(len(neg))])
        w_neg = 1 + (peso_max - 1) * min(1.0, paso / (0.7 * pasos))
        w = torch.cat([torch.ones(N_POS), torch.full((len(neg),), w_neg)])
        if dropout:  # solo al entrenar: apaga rasgos al azar para que no memorice
            x = nn.functional.dropout(x, dropout, training=True)
        z = model.logits(x).reshape(-1)
        # Como openWakeWord: solo cuentan los ejemplos que todavía no domina
        # (negativos con p >= 0.001 y positivos con p < 0.999).
        with torch.no_grad():
            pr = torch.sigmoid(z)
            dificil = ((y == 0) & (pr >= 0.001)) | ((y == 1) & (pr < 0.999))
        if dificil.any():
            bce = nn.functional.binary_cross_entropy_with_logits(z[dificil], y[dificil], reduction="none")
            loss = (bce * w[dificil]).mean()
            opt.zero_grad()
            loss.backward()
            opt.step()
        sched.step()
        if (paso + 1) % 1000 == 0 and paso + 1 >= pasos * 0.5:
            model.eval()
            rec_lib = recall(model, val_pos)
            rec_es = recall(model, val_es) if val_es is not None else rec_lib
            rec = min(rec_lib, rec_es)  # el peor de los dos manda
            fa, det = fa_por_hora(model, val_streams)
            model.train()
            log(f"paso {paso + 1}: recall libritts {rec_lib:.3f} español {rec_es:.3f}, falsas/h {fa:.2f} {det}, "
                f"peso neg {w_neg:.0f}")
            mejores.append((fa, rec, paso + 1, {k: v.clone() for k, v in model.state_dict().items()}))
    return model, mejores


def elegir(mejores, fa_max=0.5):
    """El de mayor recall entre los que no pasan de fa_max falsas por hora."""
    ok = [m for m in mejores if m[0] <= fa_max]
    if not ok:
        return None
    return max(ok, key=lambda m: (m[1], -m[0], m[2]))


if __name__ == "__main__":
    salida = "/home/user/entreno/modelos"
    os.makedirs(salida, exist_ok=True)
    resultados = []
    for peso in [int(a) for a in sys.argv[1:]] or [100, 500, 1500]:
        print(f"=== peso máximo de negativos {peso} ===", flush=True)
        model, mejores = entrenar(peso_max=peso, log=lambda s: print(s, flush=True))
        m = elegir(mejores)
        if m is None:
            print("ninguno con <= 0.5 falsas/h en validación", flush=True)
            continue
        fa, rec, paso, sd = m
        torch.save(sd, f"{salida}/peso{peso}.pt")
        resultados.append({"peso": peso, "paso": paso, "recall_val": rec, "falsas_h_val": fa})
        print(f"elegido: paso {paso}, recall {rec:.3f}, falsas/h {fa:.2f}", flush=True)
    json.dump(resultados, open(f"{salida}/resumen.json", "w"), indent=1)
    print(json.dumps(resultados, indent=1))
