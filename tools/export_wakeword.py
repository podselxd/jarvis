"""Convierte los modelos ONNX de openWakeWord ("hey jarvis" v0.1 + el
melspectrograma y el modelo de embeddings que usa) en res/wakeword.bin,
que se embebe dentro de Jarvis.exe. Solo hace falta correrlo si cambian los
modelos; el .exe nunca necesita Python ni onnxruntime.

Uso: python tools/export_wakeword.py [carpeta_con_los_onnx]
(por defecto %LOCALAPPDATA%\\Jarvis\\models, donde los dejaba la versión
en Python.)

Los modelos de openWakeWord son de David Scripka, licencia CC BY-NC-SA 4.0:
https://github.com/dscripka/openWakeWord"""

import os
import struct
import sys

import numpy as np
import onnx
from onnx import numpy_helper

EXPECTED_POOLS = {2: (2, 2), 6: (1, 2), 10: (2, 2), 14: (1, 2), 18: (2, 2)}


def load(path):
    model = onnx.load(path)
    inits = {t.name: numpy_helper.to_array(t) for t in model.graph.initializer}
    return model.graph, inits


def export(models_dir, out_path):
    tensors = []

    g, inits = load(os.path.join(models_dir, "melspectrogram.onnx"))
    convs = [n for n in g.node if n.op_type == "Conv"]
    assert len(convs) == 2
    real = inits[convs[0].input[1]]
    imag = inits[convs[1].input[1]]
    assert real.shape == (257, 1, 512) and imag.shape == (257, 1, 512)
    tensors += [("mel.real", real.reshape(257, 512)), ("mel.imag", imag.reshape(257, 512))]
    melw = [inits[n.input[1]] for n in g.node if n.op_type == "MatMul"][0]
    assert melw.shape == (257, 32)
    tensors.append(("mel.melW", melw))

    g, inits = load(os.path.join(models_dir, "embedding_model.onnx"))
    conv_i = 0
    for node in g.node:
        if node.op_type == "Conv":
            w = inits[node.input[1]]
            attrs = {a.name: onnx.helper.get_attribute_value(a) for a in node.attribute}
            pads = list(attrs.get("pads", [0, 0, 0, 0]))
            kw = w.shape[3]
            assert pads == ([0, 1, 0, 1] if kw == 3 else [0, 0, 0, 0]), (conv_i, pads)
            tensors.append((f"emb.conv{conv_i}.w", w))
            if len(node.input) > 2:
                tensors.append((f"emb.conv{conv_i}.b", inits[node.input[2]]))
            conv_i += 1
        elif node.op_type == "MaxPool":
            attrs = {a.name: onnx.helper.get_attribute_value(a) for a in node.attribute}
            assert tuple(attrs["kernel_shape"]) == EXPECTED_POOLS[conv_i - 1], (conv_i, attrs)
    assert conv_i == 20

    g, inits = load(os.path.join(models_dir, "hey_jarvis_v0.1.onnx"))
    for prefix, src in (("kw", "model"), ("kv", "verifier_model")):
        for layer, idx in (("fc1", 1), ("ln1", 2), ("fc2", 4), ("ln2", 5), ("fc3", 7)):
            tensors.append((f"{prefix}.{layer}.w", inits[f"{src}.{idx}.weight"]))
            tensors.append((f"{prefix}.{layer}.b", inits[f"{src}.{idx}.bias"]))

    with open(out_path, "wb") as f:
        f.write(b"JWW1")
        f.write(struct.pack("<I", len(tensors)))
        for name, arr in tensors:
            arr = np.ascontiguousarray(arr, dtype="<f4")
            raw = name.encode("utf-8")
            f.write(struct.pack("<I", len(raw)))
            f.write(raw)
            f.write(struct.pack("<I", arr.ndim))
            f.write(struct.pack(f"<{arr.ndim}I", *arr.shape))
            f.write(arr.tobytes())
    total = sum(a.size for _, a in tensors)
    print(f"{out_path}: {len(tensors)} tensores, {total} parámetros")


if __name__ == "__main__":
    models_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ["LOCALAPPDATA"], "Jarvis", "models")
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    export(models_dir, os.path.join(root, "res", "wakeword.bin"))
