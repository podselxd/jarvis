"""Prueba configuraciones y muestra, en validación, recall y falsas/h a varios umbrales."""
import json
import sys

import numpy as np
import torch

sys.path.insert(0, "/home/user/entreno")
import e_entrenar as E

UMBRALES = [0.4, 0.6, 0.8, 0.9]


def resumen(model, val_pos, val_streams, val_es=None):
    out = {}
    for u in UMBRALES:
        fa, det = E.fa_por_hora(model, val_streams, u)
        rec = E.recall(model, val_pos, u)
        if val_es is not None:
            rec = min(rec, E.recall(model, val_es, u))
        out[u] = (round(rec, 3), round(fa, 2))
    return out


if __name__ == "__main__":
    nombre = sys.argv[1]
    cfg = json.loads(sys.argv[2])
    model, mejores = E.entrenar(log=lambda s: print(s, flush=True), **cfg)
    val_pos = np.load(f"{E.R}/pos_val.npy").astype(np.float32)[:400]
    val_streams = E.flujos("val")
    val_es = np.load(f"{E.R}/pos_val_es.npy").astype(np.float32)
    filas = []
    for fa, rec, paso, sd in mejores:
        model.load_state_dict(sd)
        model.eval()
        r = resumen(model, val_pos, val_streams, val_es)
        filas.append((paso, r))
        print(paso, "  ".join(f"u{u}: rec {a:.3f} fa {b:.2f}" for u, (a, b) in r.items()), flush=True)
        torch.save(sd, f"/home/user/entreno/modelos/{nombre}_{paso}.pt")
