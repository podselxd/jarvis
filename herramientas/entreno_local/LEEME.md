# Entrenamiento local de "Hey Sokari"

Son los scripts exactos con los que se entrenó el `res/hey_sokari.jww` de Sokari 2.3.0: en Linux, en CPU y sin Hugging Face. Tienen las rutas de la máquina donde corrieron (`/home/user/...`); para repetirlo en otra, cámbialas en `comun.py` y al principio de cada script.

Las cifras del modelo resultante y las licencias de los datos están en `../LEEME.md`.

## Orden

| Paso | Script | Qué hace |
|---|---|---|
| 0 | `parches.py` | Arreglos para usar openWakeWord 0.6 y piper-sample-generator 2.0 con librerías actuales. Los demás lo importan. |
| 1 | `a_preparar.py` | Ecos de cuartos sintéticos (pyroomacoustics), ruidos, música y habla de fondo, a 16 kHz. Separa lo de entrenamiento de lo de prueba. |
| 2 | `b_voces.py entrenamiento`, `prueba`, `espanol` | Genera los "hey sokari" y las frases parecidas con LibriTTS-R, Piper, MBROLA y espeak. |
| 3 | `c_rasgos_reales.py validacion`, `prueba`, `entrenamiento`, `mas` | Rasgos del audio real que no es "hey sokari" (FLEURS, Speech Commands, ESC-50, música, ruidos). |
| 4 | `d_rasgos_voces.py entrenamiento`, `prueba`, `espanol` | Mezcla cada voz sintética con eco y ruido y saca sus rasgos. |
| 5 | `h_experimentos.py C '{"peso_max": 1500, "pasos": 12000, "dropout": 0.1, "wd": 0.01, "d": 64, "frac_es": 0.5}'` | Entrena y muestra recall y activaciones falsas en validación. El modelo incluido es el del paso 11 000. |
| 6 | `f_evaluar.py modelo.pt` | Prueba final con voces y audio que el entrenamiento nunca vio. |

`rasgos.py` calcula los rasgos igual que `wakeword.c` en vivo, en bloques de 80 ms: la diferencia contra el C es de 6e-5. `e_entrenar.py` tiene el modelo y el entrenamiento, y `g_diagnostico.py` dice qué frase de validación disparó cada activación falsa.

Para pasar el modelo al formato de Sokari:
1. Expórtalo a ONNX con `torch.onnx.export(..., opset_version=13, dynamo=False)`.
2. Conviértelo con `../onnx_a_jww.py`.

## Qué se necesita

- Python 3.11 con `torch`, `onnxruntime`, `numpy`, `scipy`, `soundfile`, `pyroomacoustics`, `piper-phonemize`, `webrtcvad` y `setuptools<81`.
- openWakeWord 0.6.0 (código) con `melspectrogram.onnx` y `embedding_model.onnx` de su release v0.5.1.
- piper-sample-generator **v2.0.0** con `en_US-libritts_r-medium.pt`.
- Los paquetes `espeak-ng` y `mbrola`, con las voces `mbrola-mx1`, `mx2` y `es1` a `es4`.
- **Datos**, que se bajan de Google Cloud Storage y GitHub:
  - FLEURS `es_419` y `en_us`;
  - Speech Commands v0.02;
  - ESC-50;
  - las voces de Piper en español de `rhasspy/piper` v0.0.2.
- Dos voces de Piper en español (`mls_9972` y `mls_10246`) se descartaron: daban 3–11 s para decir "hey sokari".
