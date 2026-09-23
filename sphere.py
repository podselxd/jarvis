"""Render de la esfera animada que ve el usuario en ui.py: un puerto directo
(mismo álgebra, mismo orden de operaciones) del canvas 2D que Ismael pasó en
Main.dc.html ("AI Sphere Loop") — no es una animación inventada acá, es la
misma que él aprobó, corriendo en vivo con numpy/PIL en vez de en un
navegador, porque no hay forma de empaquetar Chromium dentro del .exe.

Se mantiene en vivo (con tiempo real, como el requestAnimationFrame del
original) en vez de pre-render a GIF: la rotación nunca cierra en un loop
corto exacto (los períodos de rotación/wobble/ripple no comparten un
múltiplo común razonable), así que un GIF se notaría el salto; en vivo no
hay salto porque nunca "vuelve" a un frame anterior.

Los props (colorLow/colorHigh/rotationSpeed/rippleAmount/glowBlur) son
exactamente los que el propio archivo declara como ajustables — IDLE_PARAMS
usa sus valores default tal cual (esa es la animación "de HUD, cuando no
habla"), SPEAK_PARAMS usa otras opciones de la misma lista que el archivo
declara válidas (colorLow/colorHigh) más velocidad/ripple/glow más altos
dentro de sus propios rangos — no son colores ni parámetros inventados."""

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageEnhance

MERIDIAN_COUNT = 140
POINTS_PER_MERIDIAN = 60
BUCKETS = 12

IDLE_PARAMS = {
    "colorLow": "#4550e6",
    "colorHigh": "#ff2bd1",
    "rotationSpeed": 0.15,
    "rippleAmount": 0.24,
    "glowBlur": 8,
}

SPEAK_PARAMS = {
    "colorLow": "#5b3df0",
    "colorHigh": "#ff47e0",
    "rotationSpeed": 0.32,
    "rippleAmount": 0.4,
    "glowBlur": 11,
}


def _hex_to_rgb(hex_color: str) -> tuple[int, int, int]:
    h = hex_color.lstrip("#")
    if len(h) == 3:
        h = "".join(c * 2 for c in h)
    v = int(h, 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def _rgb_to_hex(r: int, g: int, b: int) -> str:
    return f"#{r:02x}{g:02x}{b:02x}"


def lerp_params(p0: dict, p1: dict, f: float) -> dict:
    """Interpola parámetros (no frames) entre dos estados — usado para la
    transición idle/hablando: la esfera sigue girando sin cortes mientras
    el color/velocidad/ripple se deslizan de un estado al otro."""
    r0, g0, b0 = _hex_to_rgb(p0["colorLow"])
    r1, g1, b1 = _hex_to_rgb(p1["colorLow"])
    color_low = _rgb_to_hex(
        round(r0 + (r1 - r0) * f), round(g0 + (g1 - g0) * f), round(b0 + (b1 - b0) * f)
    )
    r0, g0, b0 = _hex_to_rgb(p0["colorHigh"])
    r1, g1, b1 = _hex_to_rgb(p1["colorHigh"])
    color_high = _rgb_to_hex(
        round(r0 + (r1 - r0) * f), round(g0 + (g1 - g0) * f), round(b0 + (b1 - b0) * f)
    )
    return {
        "colorLow": color_low,
        "colorHigh": color_high,
        "rotationSpeed": p0["rotationSpeed"] + (p1["rotationSpeed"] - p0["rotationSpeed"]) * f,
        "rippleAmount": p0["rippleAmount"] + (p1["rippleAmount"] - p0["rippleAmount"]) * f,
        "glowBlur": p0["glowBlur"] + (p1["glowBlur"] - p0["glowBlur"]) * f,
    }


def _bucket_colors(color_low: str, color_high: str, buckets: int = BUCKETS) -> list[tuple[int, int, int]]:
    """Igual que applyProps() del original: un degradado de BUCKETS pasos
    entre colorLow y colorHigh, con alfa creciente (f*f*0.85+0.06) — acá se
    devuelve ya premultiplicado por ese alfa porque las capas se sobreponen
    sumando (equivale al globalCompositeOperation='lighter' del canvas)."""
    low = _hex_to_rgb(color_low)
    high = _hex_to_rgb(color_high)
    colors = []
    for i in range(buckets):
        f = i / (buckets - 1)
        alpha = 0.06 + f * f * 0.85
        r = round((low[0] + (high[0] - low[0]) * f) * alpha)
        g = round((low[1] + (high[1] - low[1]) * f) * alpha)
        b = round((low[2] + (high[2] - low[2]) * f) * alpha)
        colors.append((r, g, b))
    return colors


class SphereRenderer:
    def __init__(self, size: int = 600, meridian_count: int = MERIDIAN_COUNT, pts: int = POINTS_PER_MERIDIAN):
        self.size = size
        self.pts = pts
        self._widths = [max(1, round(0.5 + (k / (BUCKETS - 1)) * 1.3)) for k in range(BUCKETS)]

        m = np.arange(meridian_count).reshape(-1, 1)
        j = np.arange(pts + 1).reshape(1, -1)
        self.phi = (m / meridian_count) * 2 * np.pi
        self.theta = (j / pts) * np.pi
        self.bx = np.sin(self.theta) * np.cos(self.phi)
        self.by = np.cos(self.theta) * np.ones_like(self.phi)
        self.bz = np.sin(self.theta) * np.sin(self.phi)

    def render(self, t: float, params: dict) -> Image.Image:
        size = self.size
        cx = cy = size / 2
        R = size * 0.375  # 360/960 del original, mismo tamaño relativo de esfera
        rot_speed = params["rotationSpeed"]
        ripple = params["rippleAmount"] or 0.0001
        glow_blur = params["glowBlur"]

        angle = t * rot_speed
        cosA, sinA = np.cos(angle), np.sin(angle)
        wobble = np.sin(t * 0.12) * 0.22
        cosT, sinT = np.cos(wobble), np.sin(wobble)

        phi, theta = self.phi, self.theta
        wave = ripple * (
            np.sin(phi * 4 + t * 0.6) * np.sin(theta * 2 + t * 0.3)
            + 0.5 * np.sin(phi * 11 - t * 0.9) * np.sin(theta * 5 + t * 0.5)
        )
        rad = 1 + wave
        x, y, z = self.bx * rad, self.by * rad, self.bz * rad

        x1 = x * cosA - z * sinA
        z1 = x * sinA + z * cosA
        y1 = y * cosT - z1 * sinT
        z2 = y * sinT + z1 * cosT

        persp = 760 / (760 - z2 * R * 0.55)
        sx = cx + x1 * R * persp
        sy = cy + y1 * R * persp

        fresnel = 1 - np.abs(z2)
        bulge = np.maximum(0, wave) / ripple
        depth = (z2 + 1) / 2
        intensity = np.clip(fresnel * 0.5 + bulge * 0.75 + depth * 0.15, 0, 1)
        bucket_idx = np.minimum(BUCKETS - 1, np.floor(intensity * BUCKETS)).astype(int)

        # En vez de una línea de 2 puntos por segmento (8400 llamadas a PIL,
        # el cuello de botella real), se juntan corridas consecutivas del
        # mismo bucket dentro de cada meridiano en UNA sola polilínea —
        # mismas líneas dibujadas, bastantes menos llamadas. Todo en listas
        # de Python (no arrays numpy) porque para esta parte — un loop con
        # muchas operaciones chiquitas — es más rápido que llamar a numpy
        # miles de veces con arrays de 60 elementos.
        sx_list = sx.tolist()
        sy_list = sy.tolist()
        bucket_list = bucket_idx[:, 1:].tolist()
        runs_by_bucket: list[list[list[tuple]]] = [[] for _ in range(BUCKETS)]
        for row, b in enumerate(bucket_list):
            xs_row = sx_list[row]
            ys_row = sy_list[row]
            s = 0
            cur = b[0]
            for i in range(1, len(b)):
                if b[i] != cur:
                    runs_by_bucket[cur].append(list(zip(xs_row[s : i + 1], ys_row[s : i + 1])))
                    s = i
                    cur = b[i]
            runs_by_bucket[cur].append(list(zip(xs_row[s:], ys_row[s:])))

        # Se dibuja de menos a más intenso (bucket ascendente): al pintar el
        # más brillante al final imita el "lighter" del canvas en los cruces
        # de líneas, sin pagar el costo de 12 capas separadas de más.
        colors = _bucket_colors(params["colorLow"], params["colorHigh"])
        img = Image.new("RGB", (size, size), (0, 0, 0))
        draw = ImageDraw.Draw(img)
        for bk in range(BUCKETS):
            color = colors[bk]
            width = self._widths[bk]
            for pts_xy in runs_by_bucket[bk]:
                draw.line(pts_xy, fill=color, width=width)

        if glow_blur > 0:
            # Blur barato: reducir y volver a agrandar (bilinear) en vez de
            # un blur gaussiano de verdad, y sumar en PIL nativo (uint8, con
            # saturación) en vez de pasar por numpy float — visualmente casi
            # igual para un resplandor suave, y bastante más rápido.
            small = img.resize((max(1, size // 8), max(1, size // 8)), Image.BILINEAR)
            small = ImageEnhance.Brightness(small).enhance(0.9)
            glow = small.resize((size, size), Image.BILINEAR)
            img = ImageChops.add(img, glow)

        return img
