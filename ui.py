"""Interfaz visual de Jarvis: ventana negra de pantalla completa, sin bordes,
con una esfera animada (ver sphere.py) que cambia entre estado "escuchando"
(IDLE_PARAMS, morada) y "hablando" (SPEAK_PARAMS, rosa/más rápida), con una
transición deslizada rápida entre los parámetros de una y otra — la esfera
nunca deja de girar, solo cambia de color/velocidad. Corre en su propio
hilo con su propio mainloop de tkinter — jarvis.py solo le avisa cambios de
estado via set_speaking().

Se renderiza en vivo a CONTENT_SIZE (no al tamaño de la pantalla completa)
y se centra sobre un lienzo negro del tamaño real de la pantalla recién al
mostrarla, así la ventana sigue siendo pantalla completa sin tener que
renderizar a esa resolución cada frame."""

import queue
import threading
import time

import tkinter as tk
from PIL import Image, ImageTk

from sphere import SphereRenderer, IDLE_PARAMS, SPEAK_PARAMS, lerp_params

CONTENT_SIZE = (600, 600)
FRAME_DELAY_MS = 30  # objetivo; el render de la esfera manda si tarda más (after() no se acumula, solo se atrasa)
TRANSITION_STEPS = 10
TRANSITION_DELAY_MS = 25  # ~250ms total: la esfera desliza de un set de parámetros al otro sin cortarse


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

        self.renderer = SphereRenderer(size=CONTENT_SIZE[0])
        self.start_time = time.perf_counter()

        self.label = tk.Label(self.root, bg="black", bd=0, highlightthickness=0)
        self.label.pack(fill="both", expand=True)

        self.current_params = dict(IDLE_PARAMS)
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

    def _elapsed(self) -> float:
        return time.perf_counter() - self.start_time

    def _animate(self) -> None:
        if not self.transitioning:
            self._show(self.renderer.render(self._elapsed(), self.current_params))
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
        start_params = dict(self.current_params)
        target_params = SPEAK_PARAMS if self.speaking else IDLE_PARAMS
        self.transitioning = True

        def step(i: int = 0) -> None:
            if i > TRANSITION_STEPS:
                self.current_params = dict(target_params)
                self.transitioning = False
                return
            blended = lerp_params(start_params, target_params, i / TRANSITION_STEPS)
            self._show(self.renderer.render(self._elapsed(), blended))
            self.root.after(TRANSITION_DELAY_MS, lambda: step(i + 1))

        step()
