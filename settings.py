"""Ventana de configuración que se puede abrir en cualquier momento (desde
el ícono de la bandeja), a diferencia de instalador.py que solo aparece una
vez, al arrancar sin API key. Reutiliza sus mismos helpers de .env para no
duplicar el formato ni el código.

Corre en su propio hilo con su propio root de CustomTkinter — mismo patrón
que ui.py y tray.py: nunca toca widgets de otro hilo, solo llama a
on_saved(values) al guardar, y quien la abrió decide qué aplicar en vivo
(jarvis.py) y qué persistir (.env, ya lo hace esta ventana)."""

import threading

import customtkinter as ctk

from instalador import _read_env, _write_env

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

DISPLAY_MODE_LABELS = {
    "fullscreen": "Pantalla completa",
    "windowed_borderless": "Ventana sin bordes",
    "fullscreen_borderless": "Pantalla completa sin bordes",
}
DISPLAY_MODE_BY_LABEL = {v: k for k, v in DISPLAY_MODE_LABELS.items()}

_open_lock = threading.Lock()
_is_open = False


def open_settings_window(current_display_mode: str, current_volume: int, on_saved) -> None:
    """No-op si ya hay una ventana de configuración abierta — evita dos
    instancias pisándose el .env al guardar."""
    global _is_open
    with _open_lock:
        if _is_open:
            return
        _is_open = True

    def _run() -> None:
        global _is_open
        try:
            window = SettingsWindow(current_display_mode, current_volume, on_saved)
            window.mainloop()
        finally:
            with _open_lock:
                _is_open = False

    threading.Thread(target=_run, daemon=True).start()


class SettingsWindow(ctk.CTk):
    def __init__(self, current_display_mode: str, current_volume: int, on_saved) -> None:
        super().__init__()
        self._on_saved = on_saved
        self.title("Jarvis - Configuración")
        self.geometry("460x680")
        self.resizable(False, False)

        existing = _read_env()

        ctk.CTkLabel(self, text="Configuración", font=ctk.CTkFont(size=28, weight="bold")).pack(pady=(30, 20))

        form = ctk.CTkFrame(self, fg_color="transparent")
        form.pack(fill="both", expand=True, padx=36)

        ctk.CTkLabel(form, text="API key de Groq", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        self.api_entry = ctk.CTkEntry(form, placeholder_text="gsk_...", height=40, show="•")
        self.api_entry.pack(fill="x", pady=(4, 16))
        if existing.get("GROQ_API_KEY"):
            self.api_entry.insert(0, existing["GROQ_API_KEY"])

        ctk.CTkLabel(form, text="Tu nombre", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        self.name_entry = ctk.CTkEntry(form, height=40)
        self.name_entry.pack(fill="x", pady=(4, 16))
        if existing.get("JARVIS_USER_NAME"):
            self.name_entry.insert(0, existing["JARVIS_USER_NAME"])

        ctk.CTkLabel(form, text="Palabra de apagado", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        self.stop_entry = ctk.CTkEntry(form, placeholder_text="La IA nunca la conoce", height=40)
        self.stop_entry.pack(fill="x", pady=(4, 20))
        if existing.get("JARVIS_STOP_WORD"):
            self.stop_entry.insert(0, existing["JARVIS_STOP_WORD"])

        ctk.CTkLabel(form, text="Pantalla", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        self.display_var = ctk.StringVar(
            value=DISPLAY_MODE_LABELS.get(current_display_mode, DISPLAY_MODE_LABELS["fullscreen_borderless"])
        )
        ctk.CTkSegmentedButton(
            form,
            values=list(DISPLAY_MODE_LABELS.values()),
            variable=self.display_var,
        ).pack(fill="x", pady=(6, 20))

        ctk.CTkLabel(form, text="Volumen", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        vol_row = ctk.CTkFrame(form, fg_color="transparent")
        vol_row.pack(fill="x", pady=(6, 20))
        self.volume_var = ctk.IntVar(value=current_volume)
        self.volume_label = ctk.CTkLabel(vol_row, text=f"{current_volume}%", width=42)
        self.volume_label.pack(side="right")
        ctk.CTkSlider(
            vol_row, from_=0, to=100, variable=self.volume_var, command=self._on_volume_change
        ).pack(side="left", fill="x", expand=True, padx=(0, 10))

        self.status_label = ctk.CTkLabel(self, text="", text_color="#ff6b6b", font=ctk.CTkFont(size=12))
        self.status_label.pack(pady=(0, 6))

        ctk.CTkButton(
            self,
            text="Guardar",
            height=46,
            font=ctk.CTkFont(size=15, weight="bold"),
            corner_radius=10,
            command=self._on_save,
        ).pack(fill="x", padx=36, pady=(4, 30))

    def _on_volume_change(self, value) -> None:
        self.volume_label.configure(text=f"{int(float(value))}%")

    def _on_save(self) -> None:
        api_key = self.api_entry.get().strip()
        if not api_key:
            self.status_label.configure(text="Necesitas una API key de Groq para continuar.")
            return

        display_mode = DISPLAY_MODE_BY_LABEL.get(self.display_var.get(), "fullscreen_borderless")
        volume = int(self.volume_var.get())

        values = _read_env()
        values["GROQ_API_KEY"] = api_key
        values["JARVIS_USER_NAME"] = self.name_entry.get().strip()
        values["JARVIS_STOP_WORD"] = self.stop_entry.get().strip()
        values["JARVIS_DISPLAY_MODE"] = display_mode
        values["JARVIS_VOLUME"] = str(volume)
        _write_env(values)

        self._on_saved(
            {
                "GROQ_API_KEY": api_key,
                "JARVIS_USER_NAME": values["JARVIS_USER_NAME"],
                "JARVIS_STOP_WORD": values["JARVIS_STOP_WORD"],
                "display_mode": display_mode,
                "volume": volume,
            }
        )
        self.destroy()
