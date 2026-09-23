"""Ícono en la bandeja del sistema (junto al reloj de Windows): clic derecho
da un menú para mostrar/ocultar la esfera, abrir Configuración, o salir —
así apagar Jarvis no depende de encontrar la consola o matarlo desde el
Administrador de tareas.

Corre en su propio hilo con su propio loop de eventos (pystray.Icon.run() es
bloqueante), igual que ui.py corre su propio mainloop de tkinter — jarvis.py
solo le pasa funciones callback, nunca toca sus widgets desde otro hilo."""

import threading

import pystray
from PIL import Image


class JarvisTray:
    def __init__(self, icon_image: Image.Image, on_toggle_visibility, on_open_settings, on_quit) -> None:
        self._on_toggle_visibility = on_toggle_visibility
        self._on_open_settings = on_open_settings
        self._on_quit = on_quit
        self._icon = pystray.Icon(
            "Jarvis",
            icon_image,
            "Jarvis",
            menu=pystray.Menu(
                pystray.MenuItem("Mostrar/ocultar esfera", self._toggle_visibility),
                pystray.MenuItem("Configuración...", self._open_settings),
                pystray.MenuItem("Salir", self._quit),
            ),
        )
        self._thread = threading.Thread(target=self._icon.run, daemon=True)
        self._thread.start()

    # pystray llama a los callbacks del menú desde su propio hilo — cada
    # callback ya recibido acá es rápido y solo dispara la función real
    # (que a su vez, si toca tkinter, encola el pedido en el hilo dueño en
    # vez de tocarlo directo, igual que set_speaking en ui.py).
    def _toggle_visibility(self, icon, item) -> None:
        self._on_toggle_visibility()

    def _open_settings(self, icon, item) -> None:
        self._on_open_settings()

    def _quit(self, icon, item) -> None:
        self._on_quit()
        icon.stop()

    def stop(self) -> None:
        self._icon.stop()
