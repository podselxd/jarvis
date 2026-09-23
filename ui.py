"""Interfaz visual de Jarvis: ventana negra de pantalla completa, sin bordes,
con la animación real (GIF) que cambia entre estado "escuchando" (idle_purple)
y "hablando" (speak_pink), con una transición difuminada (cross-fade) rápida
entre las dos. Corre en su propio hilo con su propio mainloop de tkinter —
jarvis.py solo le avisa cambios de estado via set_speaking().

Los frames se cargan a CONTENT_SIZE (no al tamaño de la pantalla completa):
con 200+ frames por animación, guardarlos ya escalados a 1920x1080 usaría
más de 1GB de RAM. Se centran sobre un lienzo negro del tamaño de la
pantalla recién al mostrarlos, así la ventana sigue siendo pantalla
completa sin gastar memoria de más."""

import os
import queue
import sys
import threading

import tkinter as tk
from PIL import Image, ImageTk, ImageOps, ImageSequence

# Los gifs van empaquetados DENTRO del .exe (de solo lectura) — PyInstaller los
# extrae a sys._MEIPASS en tiempo de ejecucion. Como script normal, junto a
# este archivo, como siempre.
_BASE_DIR = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(_BASE_DIR, "assets")
IDLE_IMAGE = os.path.join(ASSETS_DIR, "idle_purple.gif")
SPEAK_IMAGE = os.path.join(ASSETS_DIR, "speak_pink.gif")

CONTENT_SIZE = (600, 600)  # tamano al que se cargan los frames, no el de la pantalla
FRAME_DELAY_MS = 40  # coincide con el gif mas lento (25fps); tkinter no puede variar por frame facil
TRANSITION_STEPS = 8
TRANSITION_DELAY_MS = 18  # ~150ms total: rapida pero se nota


def _load_frames(path: str, size: tuple[int, int]) -> list[Image.Image]:
    """Cada frame se ajusta a `size` manteniendo su proporción real (nunca
    estirado) y se centra sobre un lienzo negro de exactamente `size` —
    fijo, no solo "contenido" — para que los frames de idle y de speak
    midan siempre igual entre sí (Image.blend exige mismo tamaño para
    hacer la transición entre las dos animaciones). El centrado sobre el
    fondo de pantalla completa es un paso aparte, en _compose."""
    img = Image.open(path)
    frames = []
    for frame in ImageSequence.Iterator(img):
        fitted = ImageOps.contain(frame.convert("RGB"), size, Image.LANCZOS)
        canvas = Image.new("RGB", size, (0, 0, 0))
        offset = ((size[0] - fitted.width) // 2, (size[1] - fitted.height) // 2)
        canvas.paste(fitted, offset)
        frames.append(canvas)
    return frames or [Image.new("RGB", size, (0, 0, 0))]


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

        self.screen_size = (self.root.winfo_screenwidth(), self.root.winfo_screenheight())
        self.root.geometry(f"{self.screen_size[0]}x{self.screen_size[1]}+0+0")
        self.root.overrideredirect(True)  # sin borde ni barra de titulo
        self.root.attributes("-topmost", False)  # no tapa otras ventanas cuando trabajas en otra cosa
        self.root.bind("<Escape>", lambda _e: self.root.destroy())  # forma de cerrarla sin boton de cerrar

        self.idle_frames = _load_frames(IDLE_IMAGE, CONTENT_SIZE)
        self.speak_frames = _load_frames(SPEAK_IMAGE, CONTENT_SIZE)

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

    def _compose(self, content: Image.Image) -> Image.Image:
        """Centra el frame (a CONTENT_SIZE) sobre un lienzo negro del tamaño
        real de la pantalla."""
        canvas = Image.new("RGB", self.screen_size, (0, 0, 0))
        offset = (
            (self.screen_size[0] - content.width) // 2,
            (self.screen_size[1] - content.height) // 2,
        )
        canvas.paste(content, offset)
        return canvas

    def _show(self, content: Image.Image) -> None:
        photo = ImageTk.PhotoImage(self._compose(content))
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
