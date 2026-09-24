"""Arreglos para correr openWakeWord 0.6 y piper-sample-generator 2.0 con
librerías actuales. Importar antes que nada."""
import scipy.special as _ss

# acoustics (lo importa openWakeWord) usa sph_harm, que SciPy 1.17 quitó.
if not hasattr(_ss, "sph_harm"):
    _ss.sph_harm = lambda m, n, theta, phi: _ss.sph_harm_y(n, m, phi, theta)

import torch as _torch

# El generador de voces es un modelo entero guardado con pickle (versión
# oficial v2.0.0). Solo ese archivo se carga con weights_only=False.
_load = _torch.load
_CONFIABLE = "/home/user/rhasspy/psg-v2/models/en_US-libritts_r-medium.pt"


def _torch_load(f, *a, **k):
    if str(f) == _CONFIABLE:
        k["weights_only"] = False
    return _load(f, *a, **k)


_torch.load = _torch_load

import pronouncing as _pr

# "sokari" no está en el diccionario CMU; el modelo que lo adivinaba ya no se
# puede bajar.
_pr.init_cmu()
for _w, _ph in {"sokari": "S OW0 K AA1 R IY0"}.items():
    if not _pr.phones_for_word(_w):
        _pr.lookup[_w].append(_ph)
        _pr.pronunciations.append((_w, _ph))
