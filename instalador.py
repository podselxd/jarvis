"""Formulario de configuración inicial de Jarvis, con CustomTkinter (tkinter
puro se ve anticuado — esto da un look plano/moderno tipo Material sin mucho
esfuerzo extra). Se muestra solo si falta la API key; jarvis.py lo llama antes
de arrancar en vez de fallar con un mensaje de consola."""

import hashlib
import json
import os
import subprocess
import sys
import webbrowser
from tkinter import messagebox

import customtkinter as ctk

if getattr(sys, "frozen", False):
    PROJECT_DIR = os.path.dirname(sys.executable)
else:
    PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
ENV_PATH = os.path.join(PROJECT_DIR, ".env")

# Mismo formato/ubicación que perfiles.json en jarvis.py — se escribe acá
# directo en vez de importar jarvis.py (que carga mic/wake-word/etc de más).
MEMORY_DIR = os.path.join(os.environ.get("OneDrive") or os.path.expanduser("~"), "Desktop", "Jarvis")
PROFILES_FILE = os.path.join(MEMORY_DIR, "perfiles.json")

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")


def _read_env() -> dict:
    values = {}
    if os.path.exists(ENV_PATH):
        with open(ENV_PATH, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, _, v = line.partition("=")
                    values[k.strip()] = v.strip()
    return values


def _write_env(values: dict) -> None:
    with open(ENV_PATH, "w", encoding="utf-8") as f:
        for k, v in values.items():
            if v:
                f.write(f"{k}={v}\n")


def _tailscale_installed() -> bool:
    try:
        subprocess.run(["tailscale", "version"], capture_output=True, timeout=5)
        return True
    except (OSError, subprocess.SubprocessError):
        return False


def _obsidian_installed() -> bool:
    candidato = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "obsidian", "Obsidian.exe")
    return os.path.exists(candidato)


def _hash_password(password: str) -> str:
    return hashlib.sha256(password.strip().encode("utf-8")).hexdigest()


def _load_profiles() -> dict:
    if not os.path.exists(PROFILES_FILE):
        return {}
    try:
        with open(PROFILES_FILE, "r", encoding="utf-8") as f:
            return json.load(f)
    except json.JSONDecodeError:
        return {}


def _save_profiles(profiles: dict) -> None:
    os.makedirs(MEMORY_DIR, exist_ok=True)
    with open(PROFILES_FILE, "w", encoding="utf-8") as f:
        json.dump(profiles, f, ensure_ascii=False, indent=2)


def _set_profile_password(nombre: str, password: str) -> None:
    """Mismo perfil al que jarvis.py identificarse(nombre) entra al arrancar
    (o 'default' si no diste nombre) — así la contraseña puesta acá protege
    el perfil correcto, no uno que nunca se usa."""
    key = (nombre or "default").strip().lower()
    profiles = _load_profiles()
    profile = profiles.get(key, {"nombre": (nombre or "default").strip(), "apodos": [key]})
    profile["password"] = _hash_password(password) if password else None
    profiles[key] = profile
    _save_profiles(profiles)


def _reset_all_profile_passwords() -> int:
    """Recuperación local: quita TODAS las contraseñas de perfil. A propósito
    solo se puede hacer acá (físicamente en la máquina), nunca por voz —
    si no, cualquiera podría pedirle a Jarvis que se la reinicie."""
    profiles = _load_profiles()
    quitadas = sum(1 for p in profiles.values() if p.get("password"))
    for profile in profiles.values():
        profile["password"] = None
    _save_profiles(profiles)
    return quitadas


def _setup_autostart() -> None:
    """Crea el acceso directo en la carpeta de inicio de Windows. Si esto
    corre empaquetado (PyInstaller), apunta al propio .exe; si corre como
    script normal, apunta a pythonw.exe + jarvis.py (sin ventana de consola)."""
    import win32com.client

    startup_dir = os.path.join(
        os.environ["APPDATA"], "Microsoft", "Windows", "Start Menu", "Programs", "Startup"
    )
    shortcut_path = os.path.join(startup_dir, "Jarvis.lnk")

    shell = win32com.client.Dispatch("WScript.Shell")
    shortcut = shell.CreateShortCut(shortcut_path)
    if getattr(sys, "frozen", False):
        shortcut.TargetPath = sys.executable
        shortcut.Arguments = ""
    else:
        import shutil

        pythonw = shutil.which("pythonw") or sys.executable
        shortcut.TargetPath = pythonw
        shortcut.Arguments = f'"{os.path.join(PROJECT_DIR, "jarvis.py")}"'
    shortcut.WorkingDirectory = PROJECT_DIR
    shortcut.save()


class InstalladorWindow(ctk.CTk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Jarvis - Configuración")
        self.geometry("460x800")
        self.resizable(False, False)
        self.saved = False

        existing = _read_env()

        ctk.CTkLabel(self, text="Jarvis", font=ctk.CTkFont(size=34, weight="bold")).pack(pady=(36, 2))
        ctk.CTkLabel(
            self, text="Configuración inicial", font=ctk.CTkFont(size=14), text_color="gray60"
        ).pack(pady=(0, 26))

        form = ctk.CTkFrame(self, fg_color="transparent")
        form.pack(fill="both", expand=True, padx=36)

        ctk.CTkLabel(form, text="API key de Groq", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        self.api_entry = ctk.CTkEntry(form, placeholder_text="gsk_...", height=40, show="•")
        self.api_entry.pack(fill="x", pady=(4, 2))
        if existing.get("GROQ_API_KEY"):
            self.api_entry.insert(0, existing["GROQ_API_KEY"])
        link = ctk.CTkLabel(
            form,
            text="Conseguir una gratis en console.groq.com  →",
            text_color="#4a9eff",
            cursor="hand2",
            font=ctk.CTkFont(size=12),
        )
        link.pack(anchor="w", pady=(2, 20))
        link.bind("<Button-1>", lambda _e: webbrowser.open("https://console.groq.com"))

        ctk.CTkLabel(form, text="Tu nombre (opcional)", anchor="w", font=ctk.CTkFont(size=13)).pack(fill="x")
        self.name_entry = ctk.CTkEntry(
            form, placeholder_text="Para que te reconozca desde el arranque", height=40
        )
        self.name_entry.pack(fill="x", pady=(4, 20))
        if existing.get("JARVIS_USER_NAME"):
            self.name_entry.insert(0, existing["JARVIS_USER_NAME"])

        ctk.CTkLabel(
            form, text="Palabra de apagado (opcional)", anchor="w", font=ctk.CTkFont(size=13)
        ).pack(fill="x")
        self.stop_entry = ctk.CTkEntry(form, placeholder_text="La IA nunca la conoce", height=40)
        self.stop_entry.pack(fill="x", pady=(4, 20))
        if existing.get("JARVIS_STOP_WORD"):
            self.stop_entry.insert(0, existing["JARVIS_STOP_WORD"])

        ctk.CTkLabel(
            form, text="Contraseña de tu perfil (opcional)", anchor="w", font=ctk.CTkFont(size=13)
        ).pack(fill="x")
        self.profile_pw_entry = ctk.CTkEntry(
            form, placeholder_text="Para que nadie más entre a tus datos guardados", height=40, show="•"
        )
        self.profile_pw_entry.pack(fill="x", pady=(4, 2))
        reset_link = ctk.CTkLabel(
            form,
            text="¿Olvidaste una contraseña de perfil? Restablecer todas  →",
            text_color="#4a9eff",
            cursor="hand2",
            font=ctk.CTkFont(size=12),
        )
        reset_link.pack(anchor="w", pady=(2, 20))
        reset_link.bind("<Button-1>", lambda _e: self._on_reset_passwords())

        self.autostart_var = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(form, text="Iniciar con Windows", variable=self.autostart_var).pack(
            anchor="w", pady=7
        )

        self.tailscale_var = ctk.BooleanVar(value=False)
        ts_text = "Conectar con mis otros dispositivos (Tailscale)"
        ts_already = _tailscale_installed()
        if ts_already:
            ts_text += "  ✓ instalado"
        ts_check = ctk.CTkCheckBox(form, text=ts_text, variable=self.tailscale_var)
        ts_check.pack(anchor="w", pady=7)
        if ts_already:
            ts_check.configure(state="disabled")

        self.obsidian_var = ctk.BooleanVar(value=False)
        obs_text = "Instalar Obsidian (para exportar tus datos como notas)"
        obs_already = _obsidian_installed()
        if obs_already:
            obs_text += "  ✓ instalado"
        obs_check = ctk.CTkCheckBox(form, text=obs_text, variable=self.obsidian_var)
        obs_check.pack(anchor="w", pady=7)
        if obs_already:
            obs_check.configure(state="disabled")

        self.status_label = ctk.CTkLabel(self, text="", text_color="#ff6b6b", font=ctk.CTkFont(size=12))
        self.status_label.pack(pady=(6, 0))

        ctk.CTkButton(
            self,
            text="Guardar y continuar",
            height=46,
            font=ctk.CTkFont(size=15, weight="bold"),
            corner_radius=10,
            command=self._on_save,
        ).pack(fill="x", padx=36, pady=(16, 32))

    def _on_save(self) -> None:
        api_key = self.api_entry.get().strip()
        if not api_key:
            self.status_label.configure(text="Necesitas una API key de Groq para continuar.")
            return

        values = _read_env()
        values["GROQ_API_KEY"] = api_key
        if self.name_entry.get().strip():
            values["JARVIS_USER_NAME"] = self.name_entry.get().strip()
        if self.stop_entry.get().strip():
            values["JARVIS_STOP_WORD"] = self.stop_entry.get().strip()
        _write_env(values)

        if self.autostart_var.get():
            try:
                _setup_autostart()
            except Exception as exc:
                print(f"No pude configurar el autoarranque: {exc}")

        if self.tailscale_var.get() and not _tailscale_installed():
            try:
                subprocess.Popen(
                    [
                        "winget", "install", "--id", "Tailscale.Tailscale",
                        "--accept-package-agreements", "--accept-source-agreements",
                    ]
                )
            except OSError:
                pass

        if self.obsidian_var.get() and not _obsidian_installed():
            try:
                subprocess.Popen(
                    [
                        "winget", "install", "--id", "Obsidian.Obsidian",
                        "--accept-package-agreements", "--accept-source-agreements",
                    ]
                )
            except OSError:
                pass

        if self.profile_pw_entry.get().strip():
            try:
                _set_profile_password(self.name_entry.get().strip(), self.profile_pw_entry.get().strip())
            except Exception as exc:
                print(f"No pude guardar la contraseña de perfil: {exc}")

        self.saved = True
        self.destroy()

    def _on_reset_passwords(self) -> None:
        if not messagebox.askyesno(
            "Restablecer contraseñas",
            "Esto quita la contraseña de TODOS los perfiles guardados en esta PC "
            "(el tuyo y el de cualquier otra persona que haya protegido el suyo). "
            "Solo se puede hacer desde acá, nunca por voz. ¿Continuar?",
        ):
            return
        quitadas = _reset_all_profile_passwords()
        messagebox.showinfo(
            "Listo",
            f"Se quitaron {quitadas} contraseña(s)." if quitadas else "No había ninguna contraseña puesta.",
        )


def run_setup_if_needed() -> bool:
    """True si ya hay (o quedó) una API key configurada y se puede seguir;
    False si el usuario cerró la ventana sin guardar."""
    if _read_env().get("GROQ_API_KEY"):
        return True
    window = InstalladorWindow()
    window.mainloop()
    return window.saved


if __name__ == "__main__":
    run_setup_if_needed()
