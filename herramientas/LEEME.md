# Entrenar "Hey Sokari"

Sokari detecta su palabra en tu PC, con un puerto a C de [openWakeWord](https://github.com/dscripka/openWakeWord). El detector tiene dos partes:

- **La parte común** (`res/wakeword.bin`): convierte el audio en rasgos. Es igual para cualquier palabra y ya viene en el repo.
- **El clasificador de la palabra** (`res/hey_sokari.jww`): es lo único que se entrena. Mientras no exista, el exe se compila sin palabra y a Sokari se le habla con **Ctrl+Alt+J**.

## Pasos

1. Abre `entrenar_hey_sokari.ipynb` en [Google Colab](https://colab.research.google.com): *Archivo → Subir cuaderno*.
2. Activa la GPU: *Entorno de ejecución → Cambiar tipo de entorno de ejecución → T4 GPU*.
3. Dale *Entorno de ejecución → Ejecutar todo*. Tarda más o menos 1–2 horas.
   - Genera miles de "hey sokari" con cientos de voces sintéticas y los mezcla con eco, ruido y música.
   - Aprende a distinguirlos de unas 2,000 horas de habla y ruido.
4. Al final se descarga `hey_sokari.jww`. Súbelo al repo en `res/` (*Add file → Upload files*, en una rama nueva) y abre el PR.
5. El CI arma un `Sokari.exe` que ya trae la palabra: bájalo de *Actions* y pruébalo.

## Si algo falla

El cuaderno instala versiones actuales de las librerías y no se pudo probar fuera de Colab. Si una celda sale en rojo, copia el mensaje completo y pásalo para corregir el cuaderno.

## Archivos

| Archivo | Qué es |
|---|---|
| `entrenar_hey_sokari.ipynb` | El cuaderno de Colab. Se genera con `armar_cuaderno.py`: no lo edites a mano. |
| `armar_cuaderno.py` | Arma el cuaderno y le mete el convertidor adentro. |
| `onnx_a_jww.py` | Pasa el modelo que entrena openWakeWord (`.onnx`) al formato de Sokari. Se revisa solo contra onnxruntime y, si no dan lo mismo, no escribe nada. |

Para convertir a mano: `pip install numpy onnx onnxruntime` y luego `python onnx_a_jww.py hey_sokari.onnx hey_sokari.jww`.

## Licencias

- **Código de openWakeWord:** Apache 2.0.
- **Sus modelos preentrenados:** según su README, **todos** están bajo [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/), porque se entrenaron con datos de licencia restrictiva o desconocida. Esto incluye la parte común que usa Sokari (`wakeword.bin`). El modelo de embeddings viene de uno de Google con licencia Apache 2.0, pero openWakeWord publica el suyo bajo CC BY-NC-SA.
- **`hey_sokari.jww`:** se entrena encima de esa parte común y con sus datos, así que se trata igual: CC BY-NC-SA 4.0.
  - Uso no comercial.
  - Hay que dar crédito a openWakeWord.
  - Si alguien lo modifica, tiene que compartirlo con la misma licencia.

Por eso Sokari es gratis y no comercial.
