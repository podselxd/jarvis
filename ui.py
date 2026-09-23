"""Interfaz visual de Jarvis: ventana negra de pantalla completa, sin bordes, con
una imagen que cambia entre estado "escuchando" (idle_purple) y "hablando"
(speak_pink), con una transición difuminada (cross-fade) rápida entre las dos.
Corre en su propio hilo con su propio mainloop de tkinter — jarvis.py solo le
avisa cambios de estado via set_speaking().

Si algún día se reemplazan los assets por versiones animadas de verdad (varios
frames), esto ya los recorre solo — no hace falta tocar código."""

import os
import queue
import threading

import tkinter as tk
from PIL import Image, ImageTk, ImageOps, ImageSequence

ASSETS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
IDLE_IMAGE = os.path.join(ASSETS_DIR, "idle_purple.webp")
SPEAK_IMAGE = os.path.join(ASSETS_DIR, "speak_pink.webp")

FRAME_DELAY_MS = 40  # ~25fps para animaciones multi-frame
TRANSITION_STEPS = 8
TRANSITION_DELAY_MS = 18  # ~150ms total: rapida pero se nota


def _load_frames(path: str, size: tuple[int, int]) -> list[Image.Image]:
    """Achica/agranda cada frame manteniendo su proporción real (ImageOps.contain
    en vez de un resize a lo bruto) y lo centra sobre un lienzo negro del tamaño
    de la pantalla — así nunca queda estirado, con barras negras arriba/abajo o
    a los costados si la proporción no coincide con la de la pantalla."""
    img = Image.open(path)
    frames = []
    for frame in ImageSequence.Iterator(img):
        frame = frame.convert("RGBA")
        fitted = ImageOps.contain(frame, size, Image.LANCZOS)
        canvas = Image.new("RGBA", size, (0, 0, 0, 255))
        offset = ((size[0] - fitted.width) // 2, (size[1] - fitted.height) // 2)
        canvas.paste(fitted, offset, fitted)
        frames.append(canvas)
    return frames or [Image.new("RGBA", size, (0, 0, 0, 255))]


class JarvisUI:
    def __init__(self) -> None:
        self._queue: queue.Queue[bool] = queue.Queue()
        self._ready = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=10)

    def set_speaking(self, speaking: bool) -> None:
        self._queue.put(speaking)

    # --- todo lo de abajo corre en el hilo de la UI, nunca desde afuera ---

    def _run(self) -> None:
        self.root = tk.Tk()
        self.root.title("Jarvis")
        self.root.configure(bg="black")

        size = (self.root.winfo_screenwidth(), self.root.winfo_screenheight())
        self.root.geometry(f"{size[0]}x{size[1]}+0+0")
        self.root.overrideredirect(True)  # sin borde ni barra de titulo
        self.root.attributes("-topmost", False)  # no tapa otras ventanas cuando trabajas en otra cosa
        self.root.bind("<Escape>", lambda _e: self.root.destroy())  # forma de cerrarla sin boton de cerrar

        self.idle_frames = _load_frames(IDLE_IMAGE, size)
        self.speak_frames = _load_frames(SPEAK_IMAGE, size)

        self.label = tk.Label(self.root, bg="black", bd=0, highlightthickness=0)
        self.label.pack(fill="both", expand=True)

        self.current_frames = self.idle_frames
        self.frame_idx = 0
        self.speaking = False
        self.transitioning = False

        self._ready.set()
        self._animate()
        self._poll_queue()
        self.root.mainloop()

    def _show(self, frame: Image.Image) -> None:
        photo = ImageTk.PhotoImage(frame)
        self.label.configure(image=photo)
        self.label.image = photo  # referencia, si no tkinter la recolecta y queda en negro

    def _animate(self) -> None:
        if not self.transitioning:
            frame = self.current_frames[self.frame_idx % len(self.current_frames)]
            self._show(frame)
            self.frame_idx += 1
        self.root.after(FRAME_DELAY_MS, self._animate)

    def _poll_queue(self) -> None:
        try:
            while True:
                speaking = self._queue.get_nowait()
                if speaking != self.speaking:
                    self.speaking = speaking
                    self._start_transition()
        except queue.Empty:
            pass
        self.root.after(50, self._poll_queue)

    def _start_transition(self) -> None:
        start_frame = self.current_frames[self.frame_idx % len(self.current_frames)]
        target_frames = self.speak_frames if self.speaking else self.idle_frames
        end_frame = target_frames[0]
        self.transitioning = True

        def step(i: int = 0) -> None:
            if i > TRANSITION_STEPS:
                self.current_frames = target_frames
                self.frame_idx = 0
                self.transitioning = False
                return
            self._show(Image.blend(start_frame, end_frame, i / TRANSITION_STEPS))
            self.root.after(TRANSITION_DELAY_MS, lambda: step(i + 1))

        step()
