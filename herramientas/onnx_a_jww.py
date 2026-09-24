#!/usr/bin/env python3
"""Convierte el clasificador que entrena openWakeWord (hey_sokari.onnx) al
formato que Sokari lleva adentro del exe (res/hey_sokari.jww).

Se revisa solo: calcula la salida con los pesos convertidos (la misma cuenta
que hace Sokari en C) y la compara con onnxruntime sobre el modelo original.
Si no coinciden, no escribe nada.

Uso:  python onnx_a_jww.py hey_sokari.onnx hey_sokari.jww
Pide: pip install numpy onnx onnxruntime
"""
import hashlib
import struct
import sys

import numpy as np

FEAT_FRAMES, EMB_DIM = 16, 96  # lo que Sokari le pasa al clasificador
IN_DIM = FEAT_FRAMES * EMB_DIM
ORDER = ["fc1.w", "fc1.b", "ln1.w", "ln1.b", "fc2.w", "fc2.b", "ln2.w", "ln2.b", "fc3.w", "fc3.b"]
# Nombres que les pone PyTorch al exportar el modelo "dnn" de openWakeWord.
TORCH_NAMES = {
    "fc1.w": "layer1.weight", "fc1.b": "layer1.bias",
    "ln1.w": "layernorm1.weight", "ln1.b": "layernorm1.bias",
    "fc2.w": "blocks.0.fcn_layer.weight", "fc2.b": "blocks.0.fcn_layer.bias",
    "ln2.w": "blocks.0.layer_norm.weight", "ln2.b": "blocks.0.layer_norm.bias",
    "fc3.w": "last_layer.weight", "fc3.b": "last_layer.bias",
}


class ConversionError(Exception):
    pass


def load_constants(model):
    from onnx import numpy_helper
    consts = {i.name: numpy_helper.to_array(i) for i in model.graph.initializer}
    for n in model.graph.node:
        if n.op_type == "Constant":
            for a in n.attribute:
                if a.name == "value":
                    consts[n.output[0]] = numpy_helper.to_array(a.t)
    return {k: np.asarray(v, dtype=np.float32) for k, v in consts.items()}


def by_name(consts):
    if all(v in consts for v in TORCH_NAMES.values()):
        return {k: consts[v] for k, v in TORCH_NAMES.items()}
    return None


def by_graph(model, consts):
    """Si los nombres no son los de PyTorch: recorre el grafo en orden y toma
    las 3 capas lineales (Gemm, o MatMul + Add) y las 2 LayerNorm (el operador,
    o la versión desarmada que termina en Div -> Mul(escala) -> Add(sesgo))."""
    nodes = list(model.graph.node)
    users = {}
    for n in nodes:
        for x in n.input:
            users.setdefault(x, []).append(n)

    def next_op(out, op):
        return next((u for u in users.get(out, []) if u.op_type == op), None)

    def const_input(n):
        return next((consts[x] for x in n.input if x in consts), None)

    linears, norms = [], []
    for n in nodes:
        if n.op_type == "Gemm" and n.input[1] in consts:
            w = consts[n.input[1]]
            if not next((a.i for a in n.attribute if a.name == "transB"), 0):
                w = w.T
            b = consts.get(n.input[2]) if len(n.input) > 2 else None
            linears.append((w, b))
        elif n.op_type == "MatMul" and n.input[1] in consts:
            add = next_op(n.output[0], "Add")
            linears.append((consts[n.input[1]].T, const_input(add) if add is not None else None))
        elif n.op_type == "LayerNormalization":
            norms.append((consts.get(n.input[1]), consts.get(n.input[2]) if len(n.input) > 2 else None))
        elif n.op_type == "Div":
            mul = next_op(n.output[0], "Mul")
            add = next_op(mul.output[0], "Add") if mul is not None else None
            if mul is not None and add is not None:
                norms.append((const_input(mul), const_input(add)))
    if len(linears) != 3 or len(norms) != 2:
        return None
    (w1, b1), (w2, b2), (w3, b3) = linears
    (g1, e1), (g2, e2) = norms
    return dict(zip(ORDER, [w1, b1, g1, e1, w2, b2, g2, e2, w3, b3]))


def check_shapes(p):
    if any(p[k] is None for k in ORDER):
        raise ConversionError("al modelo le falta algún peso (¿no es el modelo 'dnn' de openWakeWord?)")
    p = {k: np.ascontiguousarray(np.asarray(v, dtype=np.float32).reshape(v.shape)) for k, v in p.items()}
    for k in ["fc1.b", "ln1.w", "ln1.b", "fc2.b", "ln2.w", "ln2.b", "fc3.b"]:
        p[k] = p[k].reshape(-1)
    if p["fc1.w"].ndim != 2 or p["fc1.w"].shape[1] != IN_DIM:
        raise ConversionError(f"la primera capa recibe {p['fc1.w'].shape}; Sokari le pasa {FEAT_FRAMES}x{EMB_DIM} "
                              f"rasgos ({IN_DIM}). ¿La frase dura más de 2 segundos?")
    h = p["fc1.w"].shape[0]
    if not 1 <= h <= 256:
        raise ConversionError(f"capa oculta de {h} neuronas; Sokari acepta de 1 a 256")
    want = {"fc1.b": (h,), "ln1.w": (h,), "ln1.b": (h,), "fc2.w": (h, h), "fc2.b": (h,), "ln2.w": (h,),
            "ln2.b": (h,), "fc3.w": (1, h), "fc3.b": (1,)}
    for k, shape in want.items():
        if k == "fc3.w":
            p[k] = p[k].reshape(1, -1)
        if p[k].shape != shape:
            raise ConversionError(f"{k} tiene forma {p[k].shape}, se esperaba {shape}")
    return p


def forward(p, feats):
    """La misma cuenta que wakeword.c: fc1 -> LayerNorm -> ReLU -> fc2 ->
    LayerNorm -> ReLU -> fc3 -> sigmoide."""
    def ln_relu(x, g, b):
        m = x.mean(axis=1, keepdims=True)
        v = ((x - m) ** 2).mean(axis=1, keepdims=True)
        return np.maximum((x - m) / np.sqrt(v + 1e-5) * g + b, 0)

    x = feats.reshape(len(feats), -1).astype(np.float64)
    x = ln_relu(x @ p["fc1.w"].T + p["fc1.b"], p["ln1.w"], p["ln1.b"])
    x = ln_relu(x @ p["fc2.w"].T + p["fc2.b"], p["ln2.w"], p["ln2.b"])
    z = x @ p["fc3.w"].T + p["fc3.b"]
    return (1 / (1 + np.exp(-z))).reshape(-1)


def pack(p):
    out = [b"JWW1", struct.pack("<I", len(ORDER))]
    for k in ORDER:
        name = ("kw." + k).encode()
        a = np.ascontiguousarray(p[k], dtype="<f4")
        out += [struct.pack("<I", len(name)), name, struct.pack("<I", a.ndim),
                struct.pack(f"<{a.ndim}I", *a.shape), a.tobytes()]
    return b"".join(out)


def unpack(data):
    if data[:4] != b"JWW1":
        raise ConversionError("el archivo no empieza con JWW1")
    (n,), off, p = struct.unpack("<I", data[4:8]), 8, {}
    for _ in range(n):
        (ln,), off = struct.unpack("<I", data[off:off + 4]), off + 4
        name, off = data[off:off + ln].decode(), off + ln
        (nd,), off = struct.unpack("<I", data[off:off + 4]), off + 4
        dims, off = struct.unpack(f"<{nd}I", data[off:off + 4 * nd]), off + 4 * nd
        count = int(np.prod(dims))
        p[name[3:]] = np.frombuffer(data[off:off + 4 * count], dtype="<f4").reshape(dims)
        off += 4 * count
    if off != len(data):
        raise ConversionError("sobran bytes al final del archivo")
    return p


def convert(onnx_path):
    import onnx
    import onnxruntime as ort

    model = onnx.load(onnx_path)
    consts = load_constants(model)
    p = by_name(consts) or by_graph(model, consts)
    if p is None:
        raise ConversionError("no encontré las 3 capas y las 2 LayerNorm del modelo 'dnn' de openWakeWord")
    p = check_shapes(p)

    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    inp = sess.get_inputs()[0]
    rng = np.random.default_rng(0)
    # Rasgos parecidos a los de verdad y también extremos.
    feats = np.concatenate([rng.normal(0, 3, (200, FEAT_FRAMES, EMB_DIM)),
                            rng.normal(0, 30, (40, FEAT_FRAMES, EMB_DIM)),
                            np.zeros((1, FEAT_FRAMES, EMB_DIM))]).astype(np.float32)
    ref = np.concatenate([np.asarray(sess.run(None, {inp.name: f[None]})[0]).reshape(-1) for f in feats])
    blob = pack(p)
    mine = forward(unpack(blob), feats)
    diff = float(np.max(np.abs(mine - ref)))
    if not np.isfinite(diff) or diff > 1e-4:
        raise ConversionError(f"la conversión no da lo mismo que el modelo original (diferencia {diff:.2e})")
    return blob, p["fc1.w"].shape[0], diff


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    try:
        blob, hidden, diff = convert(argv[1])
    except ConversionError as e:
        print(f"ERROR: {e}")
        return 1
    with open(argv[2], "wb") as f:
        f.write(blob)
    print(f"Listo: {argv[2]} ({len(blob)} bytes, capa oculta de {hidden} neuronas)")
    print(f"Revisado contra onnxruntime: diferencia máxima {diff:.1e}")
    print(f"SHA-256: {hashlib.sha256(blob).hexdigest()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
