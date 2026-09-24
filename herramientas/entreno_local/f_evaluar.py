"""Evaluación final con datos que el entrenamiento nunca vio:
- activaciones falsas por hora en audio real (FLEURS test, Speech Commands
  prueba, ESC-50 pliegue 5, música aparte);
- recall en voces nuevas: LibriTTS (hablantes 800-903), Piper en español
  (carlfm) y espeak en español, en silencio y con ruido nuevo;
- cuántas frases parecidas lo activan."""
import json
import sys

import numpy as np
import torch

sys.path.insert(0, "/home/user/entreno")
from e_entrenar import R, Net, eventos, flujos, puntajes, ventanas_todas

UMBRALES = [0.3, 0.4, 0.5, 0.6, 0.7]


def max9(model, x24):
    w = np.stack([x24[:, k:k + 16] for k in range(9)], 1).reshape(-1, 16, 96)
    return puntajes(model, w).reshape(len(x24), 9).max(1)


def evaluar(pt):
    sd = torch.load(pt)
    model = Net(sd["layer1.weight"].shape[0])
    model.load_state_dict(sd)
    model.eval()
    res = {"falsas_por_hora": {}, "recall": {}, "parecidas": {}}
    streams = flujos("test")
    total_h = 0
    tot = {u: 0 for u in UMBRALES}
    for k, e in streams.items():
        s = puntajes(model, ventanas_todas(e))
        h = len(e) * 0.08 / 3600
        total_h += h
        res["falsas_por_hora"][k] = {"horas": round(h, 2), **{str(u): eventos(s, u) for u in UMBRALES}}
        for u in UMBRALES:
            tot[u] += eventos(s, u)
    res["falsas_por_hora"]["TOTAL"] = {"horas": round(total_h, 2), **{str(u): tot[u] for u in UMBRALES},
                                       **{f"por_hora@{u}": round(tot[u] / total_h, 3) for u in UMBRALES}}
    # carlfm y espeak (primera tanda) ya son voces vistas en el entrenamiento: se reportan aparte.
    for c in ["pos_test", "pos_test_mx1", "pos_test_espeak2", "pos_test_carlfm", "pos_test_espeak"]:
        for cond in ["limpio", "ruido"]:
            x = np.load(f"{R}/{c}_{cond}.npy").astype(np.float32)
            if c == "pos_test":
                x = x[400:]  # las primeras 400 se usaron para elegir el modelo
            s = max9(model, x)
            res["recall"][f"{c}_{cond}"] = {str(u): round(float((s > u).mean()), 3) for u in UMBRALES}
    for c in ["neg_test", "neg_test_es", "neg_test_es2"]:
        for cond in ["limpio", "ruido"]:
            s = max9(model, np.load(f"{R}/{c}_{cond}.npy").astype(np.float32))
            res["parecidas"][f"{c}_{cond}"] = {str(u): round(float((s > u).mean()), 3) for u in UMBRALES}
    return res


if __name__ == "__main__":
    r = evaluar(sys.argv[1])
    print(json.dumps(r, indent=1, ensure_ascii=False))
    json.dump(r, open(sys.argv[1].replace(".pt", "_prueba.json"), "w"), indent=1, ensure_ascii=False)
