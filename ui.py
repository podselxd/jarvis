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
WINDOWED_SIZE = (600, 600)  # tamaño de la ventana en modo "ventana sin bordes" (no pantalla completa)
FRAME_DELAY_MS = 30  # objetivo; el render de la esfera manda si tarda más (after() no se acumula, solo se atrasa)
TRANSITION_STEPS = 10
TRANSITION_DELAY_MS = 25  # ~250ms total: la esfera desliza de un set de parámetros al otro sin cortarse

DISPLAY_MODES = ("fullscreen", "windowed_borderless", "fullscreen_borderless")
DEFAULT_DISPLAY_MODE = "fullscreen_borderless"


class JarvisUI:
    def __init__(self) -> None:
        self._speaking_queue: queue.Queue[bool] = queue.Queue()
        self._cmd_queue: queue.Queue[tuple] = queue.Queue()
        self._ready = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()
        self._ready.wait(timeout=10)

    def set_speaking(self, speaking: bool) -> None:
        self._speaking_queue.put(speaking)

    def set_display_mode(self, mode: str) -> None:
        if mode not in DISPLAY_MODES:
            return
        self._cmd_queue.put(("display_mode", mode))

    def set_visible(self, visible: bool) -> None:
        self._cmd_queue.put(("visible", visible))

    def toggle_visible(self) -> None:
        self._cmd_queue.put(("toggle_visible", None))

    # --- todo lo de abajo corre en el hilo de la UI, nunca desde afuera ---

    def _run(self) -> None:
        self.root = tk.Tk()
        self.root.title("Jarvis")
        self.root.configure(bg="black")

        self.screen_size = (self.root.winfo_screenwidth(), self.root.winfo_screenheight())
        self.root.bind("<Escape>", lambda _e: self.root.destroy())  # forma de cerrarla sin boton de cerrar
        self.root.protocol("WM_DELETE_WINDOW", self.root.destroy)

        self.renderer = SphereRenderer(size=CONTENT_SIZE[0])
        self.start_time = time.perf_counter()

        self.label = tk.Label(self.root, bg="black", bd=0, highlightthickness=0)
        self.label.pack(fill="both", expand=True)

        self.current_params = dict(IDLE_PARAMS)
        self.speaking = False
        self.transitioning = False
        self.visible = True
        self.display_mode = None
        self.canvas_size = CONTENT_SIZE
        self._apply_display_mode(DEFAULT_DISPLAY_MODE)

        self._ready.set()
        self._animate()
        self._poll_queue()
        self.root.mainloop()

    def _apply_display_mode(self, mode: str) -> None:
        if mode == self.display_mode:
            return
        self.display_mode = mode
        self.root.attributes("-fullscreen", False)  # se limpia siempre; "fullscreen" la vuelve a prender
        if mode == "fullscreen":
            # Pantalla completa "de verdad" (API nativa de Windows vía tkinter)
            # en vez de overrideredirect + geometría a mano — así el propio SO
            # se encarga del encuadre con cualquier escalado de pantalla.
            self.canvas_size = self.screen_size
            self.root.overrideredirect(False)
            self.root.attributes("-topmost", False)
            self.root.attributes("-fullscreen", True)
        elif mode == "windowed_borderless":
            # Ventana chica sin bordes, centrada — no tapa el resto de la
            # pantalla. Topmost porque si no, al no cubrir todo, se pierde
            # detrás de la primera ventana que se le ponga encima.
            self.canvas_size = WINDOWED_SIZE
            self.root.overrideredirect(True)
            x = (self.screen_size[0] - WINDOWED_SIZE[0]) // 2
            y = (self.screen_size[1] - WINDOWED_SIZE[1]) // 2
            self.root.geometry(f"{WINDOWED_SIZE[0]}x{WINDOWED_SIZE[1]}+{x}+{y}")
            self.root.attributes("-topmost", True)
        else:  # fullscreen_borderless (default de siempre)
            self.canvas_size = self.screen_size
            self.root.overrideredirect(True)
            self.root.geometry(f"{self.screen_size[0]}x{self.screen_size[1]}+0+0")
            self.root.attributes("-topmost", False)

    def _compose(self, content: Image.Image) -> Image.Image:
        """Centra el frame (a CONTENT_SIZE) sobre un lienzo negro del tamaño
        actual de la ventana (pantalla completa, o la ventana chica en modo
        "sin bordes")."""
        if content.size == self.canvas_size:
            return content
        canvas = Image.new("RGB", self.canvas_size, (0, 0, 0))
        offset = (
            (self.canvas_size[0] - content.width) // 2,
            (self.canvas_size[1] - content.height) // 2,
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
        # Oculta (ocultar desde la bandeja) no gasta CPU renderizando frames
        # que nadie ve — importa porque compite por CPU con el reconocimiento
        # de voz en el mismo proceso.
        if self.visible and not self.transitioning:
            self._show(self.renderer.render(self._elapsed(), self.current_params))
        self.root.after(FRAME_DELAY_MS, self._animate)

    def _poll_queue(self) -> None:
        try:
            while True:
                speaking = self._speaking_queue.get_nowait()
                if speaking != self.speaking:
                    self.speaking = speaking
                    self._start_transition()
        except queue.Empty:
            pass
        try:
            while True:
                cmd, value = self._cmd_queue.get_nowait()
                if cmd == "display_mode":
                    self._apply_display_mode(value)
                elif cmd == "visible":
                    self._set_visible(value)
                elif cmd == "toggle_visible":
                    self._set_visible(not self.visible)
        except queue.Empty:
            pass
        self.root.after(50, self._poll_queue)

    def _set_visible(self, visible: bool) -> None:
        self.visible = visible
        if visible:
            self.root.deiconify()
        else:
            self.root.withdraw()

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
