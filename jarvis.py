"""Jarvis: asistente de voz activado por "Hey Jarvis". Groq (oido+cerebro) + edge-tts (voz)."""

import ast
import asyncio
import ctypes
import hashlib
import io
import json
import math
import operator
import os
import random
import secrets
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import wave
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer

import edge_tts
import numpy as np
import psutil
import requests
import send2trash
import sounddevice as sd
import keyboard
import win32api
import win32clipboard
import win32con
import win32gui
import win32process
from bs4 import BeautifulSoup
from ddgs import DDGS
from openwakeword.model import Model as WakeWordModel
from openwakeword.utils import download_models as download_wakeword_models
from playsound import playsound

from ui import JarvisUI

# Empaquetado (PyInstaller), __file__ apunta a una carpeta temporal que se
# borra al cerrar — .env/memoria/sonidos tienen que vivir junto al .exe real,
# no ahí. Como script normal, junto a este archivo, como siempre.
if getattr(sys, "frozen", False):
    PROJECT_DIR = os.path.dirname(sys.executable)
else:
    PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))

UI = None  # se crea recién en main(): importar jarvis.py para pruebas no debe abrir una ventana


def _load_dotenv() -> None:
    env_path = os.path.join(PROJECT_DIR, ".env")
    if not os.path.exists(env_path):
        return
    with open(env_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, value = line.partition("=")
            os.environ.setdefault(key.strip(), value.strip())


_load_dotenv()

GROQ_API_KEY = os.environ.get("GROQ_API_KEY")
STOP_WORD = os.environ.get("JARVIS_STOP_WORD", "").strip()  # nunca se manda a Groq: se revisa localmente antes
USER_NAME = os.environ.get("JARVIS_USER_NAME", "").strip()
MESH_SECRET = os.environ.get("JARVIS_MESH_SECRET", "").strip()
MESH_PORT = 8765
DEVICES_FILE = os.path.join(PROJECT_DIR, "dispositivos.json")
SHUTDOWN_EVENT = threading.Event()
COMMAND_LOCK = threading.Lock()
GROQ_BASE = "https://api.groq.com/openai/v1"
CHAT_MODEL = "openai/gpt-oss-120b"
STT_MODEL = "whisper-large-v3-turbo"
SESSION = requests.Session()  # reutiliza la conexion TCP/TLS entre pedidos: menos latencia por request
TTS_VOICE = "es-MX-JorgeNeural"

SAMPLE_RATE = 16000
FRAME_SAMPLES = 1280  # 80ms @ 16kHz, tamaño de frame que espera openWakeWord
WAKE_WORD_NAME = "hey_jarvis"
WAKE_THRESHOLD = 0.4  # mas bajo = tolera mas variacion de acento/pronunciacion, a costa de algun falso positivo
SILENCE_RMS = 300
SILENCE_FRAMES_TO_STOP = 22  # ~1.75s de silencio: tolera pausas naturales en oraciones largas
MAX_COMMAND_SECONDS = 20
LISTEN_TIMEOUT_SECONDS = 5  # cuanto espera a que empieces a hablar antes de volver a dormir
MAX_HISTORY_MESSAGES = 24
MEMORY_WINDOW_SECONDS = 12 * 3600  # 12 horas
REMINDER_CHECK_FRAMES = int(20 / (FRAME_SAMPLES / SAMPLE_RATE))  # revisa recordatorios vencidos ~cada 20s
INTERRUPT_ENERGY_MULTIPLIER = 4.5  # que tan mas fuerte que el silencio de fondo cuenta como "me estan interrumpiendo" (2.5 se disparaba con solo moverse)

SOUNDS_DIR = os.path.join(PROJECT_DIR, "sounds")
SOUND_ACTIVATION = os.path.join(SOUNDS_DIR, "activacion.mp3")
SOUND_SEARCH = os.path.join(SOUNDS_DIR, "busqueda.mp3")
ACTIVATION_MAX_MS = 900  # no dejar que un sonido de activacion largo retrase el inicio de la grabacion

MEMORY_DIR = os.path.join(os.environ.get("OneDrive") or os.path.expanduser("~"), "Desktop", "Jarvis")
MEMORY_FILE = os.path.join(MEMORY_DIR, "memoria.jsonl")
FACTS_FILE = os.path.join(MEMORY_DIR, "hechos.json")
PROFILES_FILE = os.path.join(MEMORY_DIR, "perfiles.json")
COMMANDS_FILE = os.path.join(PROJECT_DIR, "commands.json")
REMINDERS_FILE = os.path.join(MEMORY_DIR, "recordatorios.json")

DEFAULT_PROFILE = "default"
CURRENT_SPEAKER = DEFAULT_PROFILE

ACTIVE_STREAM = None  # seteado en main(); permite que speak() se corte si alguien habla encima

OBSIDIAN_CONFIG = os.path.join(os.environ.get("APPDATA", ""), "obsidian", "obsidian.json")

SYSTEM_PROMPT = (
    "Eres Jarvis, un asistente de voz personal. Responde siempre en "
    "español neutro/latinoamericano usando tú, nunca vos ni voseo "
    "argentino (nunca 'sos', 'tenés', 'querés', 'necesitás'; sí 'eres', "
    "'tienes', 'quieres', 'necesitas') — de forma breve y natural, como "
    "si hablaras en voz alta. No uses markdown, listas ni emojis; habla "
    "en oraciones simples. Usa las herramientas disponibles cuando el "
    "pedido lo requiera (abrir algo, buscar en internet, controlar "
    "volumen, música o el escritorio). Si el usuario te cuenta un dato "
    "personal para recordar (fechas, nombres, preferencias) o corrige "
    "uno que ya le habías guardado, NO lo guardes directo: primero "
    "pregunta si quiere que lo recuerdes, y usa guardar_dato solo si "
    "confirma que sí en su próxima respuesta. Excepción: si ya te pidió "
    "explícitamente que lo recuerdes o que no se te olvide — de "
    "cualquier forma en que lo diga, en cualquier variante de español o "
    "país — eso YA es la confirmación, guárdalo directo sin volver a "
    "preguntar. Si además pide que se lo recuerdes más adelante, no solo "
    "que lo sepas ahora (algo para el futuro, con o sin fecha), usa "
    "crear_recordatorio en vez de guardar_dato; tampoco necesita "
    "confirmación extra. Entiende la intención más allá de la frase "
    "exacta o el dialecto de quien habla (eso no cambia cómo respondés "
    "vos, que siempre es con tú). Si te preguntan algo que quizás te "
    "dijeron en otra conversación, usa recordar en vez de adivinar. "
    "Si buscas algo y los resultados no traen una respuesta clara, no "
    "sigas reformulando la búsqueda una y otra vez — contesta con lo más "
    "cercano que encontraste o admite que no hay una respuesta clara; "
    "como mucho, reintenta una vez con otra frase y no más. "
    "Si un pedido tiene varios pasos encadenados (ej: 'abre X, dale play, y "
    "pon pantalla completa'), identifica cada acción por separado y pídelas "
    "TODAS juntas en la misma respuesta (varias tool calls a la vez) en el "
    "orden que corresponda, en vez de una por una en turnos separados — "
    "cada turno extra tarda más en contestar. Si necesitas enfocar una "
    "ventana antes de mandarle una tecla, primero usa focus_window (o "
    "list_windows si no sabes el título exacto) y en la misma respuesta "
    "pide ya la acción de control_desktop/control_media que sigue. Con "
    "type_text, si no es obvio dónde debería caer el texto (por ejemplo si "
    "el pedido no fue continuar escribiendo algo que ya estaba en curso), "
    "usa el parámetro 'ventana' en vez de escribir donde sea que esté el "
    "foco en ese momento. "
    "Si alguien se presenta o anuncia quién es — formal ('me llamo Ana') "
    "o informal/en broma ('papá volvió', 'llegó mamá', 'soy yo de nuevo') "
    "— llama a identificarse con ese nombre o apodo para saber con quién "
    "hablas; así cada persona tiene sus propios datos guardados. Si ese "
    "perfil pide contraseña, pídesela a quien habla antes de reintentar. "
    "Cualquiera puede pedirte lo que sea normalmente (abrir cosas, "
    "buscar, etc.) sin restricción — la contraseña es solo para entrar a "
    "los datos personales guardados de OTRA persona. Si alguien quiere "
    "proteger su propio perfil con contraseña, usa proteger_perfil. "
    "Antes de exportar datos a Obsidian con exportar_a_obsidian, "
    "pregunta primero si quiere que lo hagas y espera su confirmación — "
    "no exportes sin que te diga que sí."
)

def _known_folder(name: str) -> str:
    """Carpetas como Escritorio/Documentos pueden estar redirigidas a OneDrive
    (Known Folder Move) mientras que otras, como Descargas, no — en vez de asumir,
    comprueba cuál existe de verdad en esta máquina."""
    onedrive = os.environ.get("OneDrive")
    if onedrive:
        candidate = os.path.join(onedrive, name)
        if os.path.isdir(candidate):
            return candidate
    return os.path.join(os.path.expanduser("~"), name)


APP_ALIASES = {
    "chrome": "chrome",
    "navegador": "chrome",
    "google": "chrome",
    "bloc de notas": "notepad",
    "notas": "notepad",
    "calculadora": "calc",
    "explorador": "explorer",
    "archivos": "explorer",
    "word": "winword",
    "excel": "excel",
    "spotify": "spotify",
    "descargas": _known_folder("Downloads"),
    "escritorio": _known_folder("Desktop"),
    "documentos": _known_folder("Documents"),
}

MEDIA_KEYS = {
    "volume_up": "volume up",
    "volume_down": "volume down",
    "mute": "volume mute",
    "play_pause": "play/pause media",
    "next_track": "next track",
    "previous_track": "previous track",
}

DESKTOP_ACTIONS = {
    "show_desktop": "windows+d",
    "switch_window": "alt+tab",
    "minimize_all": "windows+m",
    "lock": "windows+l",
    "fullscreen": "f",  # atajo estandar de YouTube/Netflix/Twitch para pantalla completa del reproductor
    "close_tab": "ctrl+w",  # cierra la pestaña/ventana activa en casi cualquier app moderna
}

SEARCH_BACKENDS = ["bing", "brave", "yahoo"]
SAFE_STEP_TOOLS = {
    "open_app", "web_search", "control_media", "control_desktop",
    "focus_window", "type_text",
}

TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "open_app",
            "description": "Abre una aplicación, carpeta o archivo en la PC del usuario.",
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "Nombre de la app, carpeta o archivo, ej: 'chrome', 'calculadora', 'descargas'.",
                    }
                },
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "web_search",
            "description": "Busca en internet información actual: noticias, clima, precios, resultados, datos recientes.",
            "parameters": {
                "type": "object",
                "properties": {"query": {"type": "string", "description": "Qué buscar."}},
                "required": ["query"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "control_media",
            "description": "Controla el volumen del sistema o la reproducción multimedia.",
            "parameters": {
                "type": "object",
                "properties": {
                    "action": {"type": "string", "enum": list(MEDIA_KEYS.keys()), "description": "Acción a realizar."}
                },
                "required": ["action"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "control_desktop",
            "description": (
                "Controla el escritorio de Windows: mostrar escritorio, cambiar de "
                "ventana, minimizar todo, bloquear la PC, alternar pantalla completa "
                "(tecla F) o cerrar la pestaña/ventana activa (close_tab, Ctrl+W). "
                "Tanto F como close_tab actúan sobre lo que esté enfocado en ese "
                "momento — si no es obvio qué está enfocado, usa focus_window primero "
                "en la misma respuesta."
            ),
            "parameters": {
                "type": "object",
                "properties": {"action": {"type": "string", "enum": list(DESKTOP_ACTIONS.keys())}},
                "required": ["action"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "create_macro",
            "description": (
                "Crea un comando personalizado que agrupa una o más acciones "
                "(abrir apps, buscar, controlar media o escritorio) bajo un "
                "nombre, para repetirlas después con run_macro."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "name": {"type": "string", "description": "Nombre del comando, ej: 'modo trabajo'."},
                    "steps": {
                        "type": "array",
                        "description": "Pasos a ejecutar en orden.",
                        "items": {
                            "type": "object",
                            "properties": {
                                "tool": {"type": "string", "enum": list(SAFE_STEP_TOOLS)},
                                "args": {"type": "object"},
                            },
                            "required": ["tool", "args"],
                        },
                    },
                },
                "required": ["name", "steps"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "run_macro",
            "description": "Ejecuta un comando personalizado creado antes con create_macro.",
            "parameters": {
                "type": "object",
                "properties": {"name": {"type": "string", "description": "Nombre del comando a ejecutar."}},
                "required": ["name"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "guardar_dato",
            "description": (
                "Guarda o actualiza un dato puntual del usuario bajo una clave corta "
                "(ej: 'cumpleaños', 'nombre del perro'). Si la clave ya existía la "
                "sobreescribe — usalo también cuando el usuario corrija algo que ya "
                "te había dicho antes."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "clave": {"type": "string", "description": "Nombre corto del dato."},
                    "valor": {"type": "string", "description": "El valor a guardar."},
                },
                "required": ["clave", "valor"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "recordar",
            "description": (
                "Busca en la memoria del usuario (datos guardados y conversaciones "
                "pasadas, más allá de las últimas 12 horas) algo que haya mencionado antes."
            ),
            "parameters": {
                "type": "object",
                "properties": {"query": {"type": "string", "description": "Qué buscar."}},
                "required": ["query"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "identificarse",
            "description": (
                "Reconoce quién está hablando ahora, por nombre o apodo (formal o "
                "informal/en broma). Si el nombre o apodo ya es conocido, cambia a "
                "esa persona; si es nuevo, crea su perfil sin contraseña. Si el "
                "perfil al que se quiere cambiar tiene contraseña puesta, hay que "
                "pasarla en password (pedísela al usuario si no la dio) — sin la "
                "contraseña correcta no se cambia de perfil. A partir de que se "
                "identifica alguien, guardar_dato y recordar operan sobre sus datos."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "nombre": {"type": "string", "description": "Nombre o apodo con el que se identificó, ej: 'Ana', 'papá'."},
                    "password": {"type": "string", "description": "Contraseña, solo si el perfil la pide y el usuario la dio."},
                },
                "required": ["nombre"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "proteger_perfil",
            "description": (
                "Pone o cambia la contraseña del perfil de quien está hablando "
                "ahora mismo, para que otras personas necesiten decirla antes de "
                "poder cambiarse a ese perfil y ver sus datos personales."
            ),
            "parameters": {
                "type": "object",
                "properties": {"password": {"type": "string", "description": "La contraseña nueva a poner."}},
                "required": ["password"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "exportar_a_obsidian",
            "description": (
                "Exporta los datos guardados de quien está hablando ahora a notas de "
                "Obsidian: una carpeta por persona, con cada dato como su propia nota "
                "enlazada (para verlos como red en el Graph View). Si un dato se "
                "beneficia de más contexto (un tema, un interés — no algo puntual que "
                "el usuario ya te dio directo como una fecha), buscá en internet "
                "primero con web_search y pasá lo que encontraste en 'nota'."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "nota": {
                        "type": "string",
                        "description": "Contexto extra opcional para la nota de la persona (ej. de una búsqueda web).",
                    }
                },
                "required": [],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "crear_recordatorio",
            "description": (
                "Guarda un recordatorio para quien está hablando ahora. Usalo "
                "cuando pida explícitamente que le recuerdes algo (\"recordame "
                "que...\", \"no te olvides de avisarme...\") — esto no necesita "
                "confirmación extra, pedirlo YA es la confirmación. Si dio un "
                "momento concreto (\"en 10 minutos\", \"mañana a las 3\", \"el "
                "viernes a la tarde\"), calculalo a partir de la fecha y hora "
                "actual (te la doy en el contexto) y pasalo en cuando_iso como "
                "fecha y hora ISO 8601 (ej. '2026-09-23T15:00:00') — así Jarvis "
                "avisa solo apenas llegue el momento, sin que nadie pregunte. Si "
                "no dio un momento concreto, dejá cuando_iso vacío; igual se lo "
                "menciona la próxima vez que esa persona se identifique."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "texto": {"type": "string", "description": "Qué hay que recordarle."},
                    "cuando": {"type": "string", "description": "Cuándo, tal como lo dijo (ej: 'mañana', 'el viernes'). Opcional, solo para mostrarlo."},
                    "cuando_iso": {"type": "string", "description": "Fecha y hora exacta calculada, formato ISO 8601. Opcional."},
                },
                "required": ["texto"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "focus_window",
            "description": (
                "Busca entre las ventanas/pestañas ya abiertas una cuyo título "
                "contenga el texto dado (ej: 'Netflix', 'YouTube', el nombre de una "
                "app) y la trae al frente. Úsalo para 've a mi pestaña de X' o antes "
                "de mandar una tecla con control_desktop que necesite foco (como F "
                "para pantalla completa)."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "title_contains": {
                        "type": "string",
                        "description": "Texto que debería estar en el título de la ventana/pestaña.",
                    }
                },
                "required": ["title_contains"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "list_windows",
            "description": (
                "Devuelve los títulos de todas las ventanas y pestañas abiertas "
                "ahora mismo. Úsalo para saber qué hay abierto (ej. 'qué tengo "
                "abierto'), o antes de focus_window cuando no estás seguro del "
                "nombre exacto de la ventana que buscas."
            ),
            "parameters": {"type": "object", "properties": {}},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "type_text",
            "description": (
                "Escribe el texto dado como si lo tipearas, en lo que esté "
                "enfocado ahora mismo (o en 'ventana' si se da, enfocándola "
                "primero). Si no sabés dónde va a caer el texto, especifica "
                "'ventana' en vez de escribir a ciegas donde sea que esté el "
                "foco. Con enviar=true además presiona Enter. Usa enviar=true "
                "sin preguntar cuando el riesgo es obviamente bajo (buscar algo "
                "en Google/YouTube/un buscador, navegar). Para cualquier cosa "
                "que le llegue a OTRA PERSONA (mensaje, chat, email, comentario, "
                "publicación) o que sea difícil de deshacer una vez mandada, deja "
                "enviar=false y preguntale primero si quiere que lo mandes — "
                "salvo que ya te haya dicho explícitamente que lo envíes en el "
                "mismo pedido, ahí no hace falta preguntar de nuevo."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "texto": {"type": "string", "description": "El texto exacto a escribir."},
                    "ventana": {
                        "type": "string",
                        "description": "Opcional: título (o parte) de la ventana donde escribir.",
                    },
                    "enviar": {
                        "type": "boolean",
                        "description": "Si además hay que presionar Enter para enviarlo. Default false.",
                    },
                },
                "required": ["texto"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "list_files",
            "description": (
                "Lista los archivos y carpetas dentro de una carpeta. Acepta "
                "'descargas', 'escritorio' o 'documentos', o una ruta completa."
            ),
            "parameters": {
                "type": "object",
                "properties": {"carpeta": {"type": "string", "description": "Alias o ruta de la carpeta."}},
                "required": ["carpeta"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "read_file",
            "description": (
                "Lee y devuelve el contenido de texto de un archivo (se corta si es "
                "muy largo). Solo funciona bien con archivos de texto, no binarios."
            ),
            "parameters": {
                "type": "object",
                "properties": {"ruta": {"type": "string", "description": "Ruta completa del archivo."}},
                "required": ["ruta"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "buscar_archivo",
            "description": (
                "Busca archivos cuyo nombre contenga el texto dado, dentro de "
                "Descargas/Escritorio/Documentos (o una carpeta puntual si se da)."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "nombre": {"type": "string", "description": "Texto que debería estar en el nombre del archivo."},
                    "carpeta": {"type": "string", "description": "Opcional: dónde buscar ('descargas', 'escritorio', 'documentos', o una ruta)."},
                },
                "required": ["nombre"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "leer_portapapeles",
            "description": "Devuelve el texto que el usuario tiene copiado en el portapapeles ahora mismo.",
            "parameters": {"type": "object", "properties": {}},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "copiar_portapapeles",
            "description": "Copia el texto dado al portapapeles, para que el usuario lo pueda pegar donde quiera.",
            "parameters": {
                "type": "object",
                "properties": {"texto": {"type": "string", "description": "El texto a copiar."}},
                "required": ["texto"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "info_sistema",
            "description": (
                "Devuelve el estado actual de la PC: uso de CPU, RAM, espacio en "
                "disco y batería. Úsalo para 'cuánta batería me queda', 'por qué "
                "está lenta la PC', 'cuánto espacio tengo', etc."
            ),
            "parameters": {"type": "object", "properties": {}},
        },
    },
    {
        "type": "function",
        "function": {
            "name": "leer_pagina",
            "description": (
                "Entra a una URL y devuelve el texto real de la página completa "
                "(no solo el fragmento que trae web_search). Úsalo cuando pidan "
                "leer un artículo entero, o si dan un link directo."
            ),
            "parameters": {
                "type": "object",
                "properties": {"url": {"type": "string", "description": "La URL a leer."}},
                "required": ["url"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "mover_archivo",
            "description": (
                "Mueve un archivo a otra carpeta (para organizar). Nunca sobreescribe "
                "si ya existe un archivo con ese nombre en el destino."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "origen": {"type": "string", "description": "Ruta del archivo a mover."},
                    "destino_carpeta": {
                        "type": "string",
                        "description": "Carpeta destino: 'descargas', 'escritorio', 'documentos', o una ruta.",
                    },
                },
                "required": ["origen", "destino_carpeta"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "borrar_archivo",
            "description": (
                "Manda un archivo o carpeta a la Papelera de Reciclaje. NUNCA borra "
                "para siempre — siempre queda recuperable desde la papelera."
            ),
            "parameters": {
                "type": "object",
                "properties": {"ruta": {"type": "string", "description": "Ruta del archivo o carpeta a borrar."}},
                "required": ["ruta"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "calcular",
            "description": (
                "Calcula una expresión matemática de verdad (no la estimes de "
                "memoria, usa esta herramienta): números, + - * / // % **, y "
                "funciones como sqrt, sin, cos, log, abs, round, min, max, sum."
            ),
            "parameters": {
                "type": "object",
                "properties": {"expresion": {"type": "string", "description": "La expresión a calcular, ej: 'sqrt(144) + 3*7'."}},
                "required": ["expresion"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "registrar_dispositivo",
            "description": (
                "Registra otro dispositivo tuyo (con su propio Jarvis corriendo) "
                "bajo un nombre, para poder mandarle comandos después con "
                "gestionar_dispositivo. Se usa una sola vez por dispositivo."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "nombre": {"type": "string", "description": "Nombre para ese dispositivo, ej: 'escritorio'."},
                    "host": {"type": "string", "description": "Su dirección/nombre de Tailscale."},
                },
                "required": ["nombre", "host"],
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "gestionar_dispositivo",
            "description": (
                "Le manda un comando a otro dispositivo tuyo ya registrado (vía "
                "Tailscale) y devuelve lo que contestó. Ej: 'decile a mi "
                "escritorio que baje el volumen'."
            ),
            "parameters": {
                "type": "object",
                "properties": {
                    "nombre": {"type": "string", "description": "Nombre del dispositivo registrado."},
                    "comando": {"type": "string", "description": "Qué pedirle, en lenguaje natural."},
                },
                "required": ["nombre", "comando"],
            },
        },
    },
]


# --- Herramientas: apps, media, escritorio, búsqueda -----------------------


def open_app(name: str) -> str:
    target = APP_ALIASES.get(name.strip().lower(), name)
    try:
        os.startfile(target)
        return f"Abrí {name}."
    except OSError as exc:
        return f"No pude abrir {name}: {exc}"


def web_search(query: str) -> str:
    loop_sound_start(SOUND_SEARCH, "jarvis_busqueda", start_range=(0, 50))
    try:
        results, last_error = [], None
        for backend in SEARCH_BACKENDS:
            try:
                results = DDGS().text(query, max_results=4, backend=backend)
                if results:
                    break
            except Exception as exc:
                last_error = exc
    finally:
        loop_sound_stop("jarvis_busqueda")
    if not results:
        return f"No pude buscar: {last_error}" if last_error else "No encontré resultados."
    return "\n".join(
        f"{r.get('title', '')} ({r.get('href', '')}): {r.get('body', '')}" for r in results
    )


MAX_PAGE_READ_CHARS = 6000


def leer_pagina(url: str) -> str:
    """Lee el contenido real de una página (no solo el fragmento que trae una
    búsqueda) — para 'leeme el artículo completo' o cuando el usuario da un link."""
    url = url.strip()
    if not url:
        return "No me diste ninguna URL."
    if not url.startswith(("http://", "https://")):
        url = "https://" + url
    try:
        resp = SESSION.get(url, timeout=15, headers={"User-Agent": "Mozilla/5.0"})
        resp.raise_for_status()
    except requests.RequestException as exc:
        return f"No pude abrir esa página: {exc}"

    soup = BeautifulSoup(resp.text, "html.parser")
    for tag in soup(["script", "style", "nav", "footer", "header", "aside", "noscript"]):
        tag.decompose()
    texto = " ".join(soup.get_text(separator=" ").split())
    if not texto:
        return "Entré a la página pero no encontré texto legible ahí."
    if len(texto) > MAX_PAGE_READ_CHARS:
        texto = texto[:MAX_PAGE_READ_CHARS] + " [...se cortó acá, la página sigue...]"
    return texto


def _send_key_action(action: str, keymap: dict, label: str) -> str:
    key = keymap.get(action)
    if not key:
        return f"Acción de {label} no reconocida."
    keyboard.send(key)
    return "Listo."


def control_media(action: str) -> str:
    return _send_key_action(action, MEDIA_KEYS, "media")


def control_desktop(action: str) -> str:
    return _send_key_action(action, DESKTOP_ACTIONS, "escritorio")


def _force_foreground(hwnd) -> bool:
    """SetForegroundWindow solo, desde un proceso en segundo plano, Windows lo
    bloquea en silencio (no tira error, simplemente no cambia el foco) — es una
    proteccion anti-robo-de-foco del sistema. El workaround estandar: "pedir
    prestado" el input state del hilo que sí tiene foco ahora mismo, así Windows
    nos considera parte de esa interacción y permite el cambio. A veces igual
    falla (con o sin excepción) por motivos fuera de nuestro control — nunca
    debe tirar abajo el proceso entero por eso."""
    target_thread, _ = win32process.GetWindowThreadProcessId(hwnd)
    current_thread = win32api.GetCurrentThreadId()
    attached = False
    try:
        if target_thread != current_thread:
            attached = win32process.AttachThreadInput(current_thread, target_thread, True)
        win32gui.BringWindowToTop(hwnd)
        win32gui.SetForegroundWindow(hwnd)
        return True
    except Exception as exc:
        print(f"No pude forzar el foco de la ventana: {exc}")
        return False
    finally:
        if attached:
            win32process.AttachThreadInput(current_thread, target_thread, False)


def focus_window(title_contains: str) -> str:
    needle = title_contains.strip().lower()
    if not needle:
        return "No dijiste qué ventana buscar."

    found = {}

    def _on_window(hwnd, _):
        if not win32gui.IsWindowVisible(hwnd):
            return True
        title = win32gui.GetWindowText(hwnd)
        if title and needle in title.lower():
            found["hwnd"], found["title"] = hwnd, title
            return False  # ya encontramos, no hace falta seguir enumerando
        return True

    try:
        win32gui.EnumWindows(_on_window, None)
    except Exception:
        pass  # EnumWindows corta la enumeracion lanzando cuando el callback devuelve False

    if "hwnd" not in found:
        return f"No encontré ninguna ventana con '{title_contains}' abierta."
    win32gui.ShowWindow(found["hwnd"], win32con.SW_RESTORE)
    if _force_foreground(found["hwnd"]):
        return f"Enfoqué: {found['title']}"
    return f"Encontré {found['title']} pero Windows no me dejó traerla al frente."


def list_windows() -> str:
    titles = []

    def _on_window(hwnd, _):
        if win32gui.IsWindowVisible(hwnd):
            title = win32gui.GetWindowText(hwnd).strip()
            if title:
                titles.append(title)
        return True

    win32gui.EnumWindows(_on_window, None)
    return "\n".join(titles) if titles else "No encontré ninguna ventana abierta."


def type_text(texto: str, ventana: str = "", enviar: bool = False) -> str:
    """Escribe texto en lo que esté enfocado. enviar=False (default) no manda
    Enter — el usuario decide si lo manda. La decisión de cuándo es seguro
    poner enviar=True (buscar algo) vs. cuándo hay que preguntar primero
    (mandarle algo a otra persona) la hace el modelo vía el system prompt,
    no esta función — acá solo se ejecuta lo que ya se decidió."""
    texto = texto or ""
    if not texto.strip():
        return "No me dijiste qué escribir."
    if ventana.strip():
        resultado_foco = focus_window(ventana)
        if not resultado_foco.startswith("Enfoqué"):
            return resultado_foco  # no encontrada, o no se pudo enfocar: no escribo a ciegas
    keyboard.write(texto)
    if enviar:
        keyboard.send("enter")
        return "Listo, lo escribí y lo envié."
    return "Listo, lo escribí — no lo envié, decidís vos si mandarlo."


# --- Leer la PC: archivos, portapapeles, estado del sistema -----------------

SEARCHABLE_ROOTS = {
    "descargas": APP_ALIASES["descargas"],
    "escritorio": APP_ALIASES["escritorio"],
    "documentos": APP_ALIASES["documentos"],
}
MAX_FILE_READ_CHARS = 6000
MAX_LIST_ENTRIES = 60
MAX_SEARCH_HITS = 20
MAX_FILES_SCANNED = 20000  # tope para que una carpeta enorme sin resultados no tarde para siempre
SKIP_DIR_NAMES = {".git", "node_modules", "__pycache__", "$RECYCLE.BIN", "AppData"}


def _resolve_path(ruta: str) -> str:
    """Acepta tanto un alias conocido (descargas/escritorio/documentos) como una
    ruta completa, para no obligar a decir la ruta exacta cada vez."""
    return SEARCHABLE_ROOTS.get(ruta.strip().lower(), os.path.expandvars(ruta.strip()))


def list_files(carpeta: str) -> str:
    path = _resolve_path(carpeta)
    if not os.path.isdir(path):
        return f"No encontré la carpeta '{carpeta}'."
    try:
        entries = sorted(os.listdir(path))
    except OSError as exc:
        return f"No pude leer '{carpeta}': {exc}"
    if not entries:
        return f"'{carpeta}' está vacía."
    truncado = len(entries) > MAX_LIST_ENTRIES
    lineas = []
    for name in entries[:MAX_LIST_ENTRIES]:
        tag = "carpeta" if os.path.isdir(os.path.join(path, name)) else "archivo"
        lineas.append(f"[{tag}] {name}")
    if truncado:
        lineas.append(f"... y {len(entries) - MAX_LIST_ENTRIES} más")
    return "\n".join(lineas)


def read_file(ruta: str) -> str:
    path = _resolve_path(ruta)
    if not os.path.isfile(path):
        return f"No encontré el archivo '{ruta}'."
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            content = f.read(MAX_FILE_READ_CHARS + 1)
    except OSError as exc:
        return f"No pude leer '{ruta}': {exc}"
    if not content.strip():
        return f"'{ruta}' está vacío o no es texto legible (puede ser binario)."
    if len(content) > MAX_FILE_READ_CHARS:
        content = content[:MAX_FILE_READ_CHARS] + "\n[...se cortó acá, el archivo sigue...]"
    return content


def buscar_archivo(nombre: str, carpeta: str = "") -> str:
    nombre = nombre.strip().lower()
    if not nombre:
        return "No dijiste qué archivo buscar."
    if carpeta.strip().lower() in SEARCHABLE_ROOTS:
        roots = [SEARCHABLE_ROOTS[carpeta.strip().lower()]]
    elif carpeta.strip():
        roots = [_resolve_path(carpeta)]
    else:
        roots = list(SEARCHABLE_ROOTS.values())

    hits = []
    scanned = 0
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIR_NAMES]
            for fname in filenames:
                scanned += 1
                if nombre in fname.lower():
                    hits.append(os.path.join(dirpath, fname))
                    if len(hits) >= MAX_SEARCH_HITS:
                        break
                if scanned >= MAX_FILES_SCANNED:
                    break
            if len(hits) >= MAX_SEARCH_HITS or scanned >= MAX_FILES_SCANNED:
                break
        if len(hits) >= MAX_SEARCH_HITS or scanned >= MAX_FILES_SCANNED:
            break

    if not hits:
        return f"No encontré ningún archivo con '{nombre}' en {', '.join(roots) if carpeta.strip() else 'Descargas, Escritorio o Documentos'}."
    return "\n".join(hits)


def mover_archivo(origen: str, destino_carpeta: str) -> str:
    origen_path = _resolve_path(origen)
    if not os.path.isfile(origen_path):
        return f"No encontré el archivo '{origen}'."
    destino_dir = _resolve_path(destino_carpeta)
    if not os.path.isdir(destino_dir):
        return f"No encontré la carpeta destino '{destino_carpeta}'."
    destino_path = os.path.join(destino_dir, os.path.basename(origen_path))
    if os.path.exists(destino_path):
        return f"Ya hay un archivo llamado '{os.path.basename(origen_path)}' en '{destino_carpeta}', no lo piso."
    try:
        shutil.move(origen_path, destino_path)
    except OSError as exc:
        return f"No pude mover el archivo: {exc}"
    return f"Listo, moví {os.path.basename(origen_path)} a {destino_carpeta}."


def borrar_archivo(ruta: str) -> str:
    """Nunca borra de verdad: manda a la Papelera de Reciclaje, siempre recuperable
    con un par de clicks. A propósito no hay ninguna forma de hacer un borrado
    permanente directo desde acá."""
    path = _resolve_path(ruta)
    if not os.path.isfile(path) and not os.path.isdir(path):
        return f"No encontré '{ruta}'."
    try:
        send2trash.send2trash(path)
    except Exception as exc:
        return f"No pude mandarlo a la papelera: {exc}"
    return f"Listo, mandé '{os.path.basename(path)}' a la papelera de reciclaje — se puede recuperar desde ahí."


def leer_portapapeles() -> str:
    try:
        win32clipboard.OpenClipboard()
        try:
            data = win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
        finally:
            win32clipboard.CloseClipboard()
        return data.strip() if data and data.strip() else "El portapapeles está vacío."
    except Exception:
        return "No hay texto en el portapapeles (puede tener una imagen u otra cosa)."


def copiar_portapapeles(texto: str) -> str:
    if not texto.strip():
        return "No me dijiste qué copiar."
    win32clipboard.OpenClipboard()
    try:
        win32clipboard.EmptyClipboard()
        win32clipboard.SetClipboardText(texto, win32clipboard.CF_UNICODETEXT)
    finally:
        win32clipboard.CloseClipboard()
    return "Listo, lo copié."


def info_sistema() -> str:
    partes = []
    try:
        partes.append(f"CPU al {psutil.cpu_percent(interval=0.3):.0f}%")
    except Exception:
        pass
    try:
        mem = psutil.virtual_memory()
        partes.append(f"RAM al {mem.percent:.0f}% ({mem.used // 2**30}GB de {mem.total // 2**30}GB)")
    except Exception:
        pass
    try:
        disk = psutil.disk_usage(os.path.expanduser("~"))
        partes.append(f"disco al {disk.percent:.0f}% usado ({disk.free // 2**30}GB libres)")
    except Exception:
        pass
    try:
        bateria = psutil.sensors_battery()
        if bateria:
            estado = "cargando" if bateria.power_plugged else "sin cargador"
            partes.append(f"batería al {bateria.percent:.0f}% ({estado})")
    except Exception:
        pass
    return "; ".join(partes) if partes else "No pude leer el estado del sistema."


# --- Calculadora en sandbox: sin eval/exec, solo un AST restringido --------

_MAX_EXPONENT = 1000  # 9**9**9 ya tiene un exponente de ~387 millones: sin este tope, cuelga el proceso entero


def _safe_pow(base, exp):
    if abs(exp) > _MAX_EXPONENT:
        raise ValueError(f"el exponente es demasiado grande (máximo {_MAX_EXPONENT})")
    return operator.pow(base, exp)


_SAFE_BINOPS = {
    ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: operator.mul,
    ast.Div: operator.truediv, ast.FloorDiv: operator.floordiv, ast.Mod: operator.mod,
    ast.Pow: _safe_pow,
}
_SAFE_UNARYOPS = {ast.USub: operator.neg, ast.UAdd: operator.pos}
_SAFE_FUNCS = {
    "sqrt": math.sqrt, "sin": math.sin, "cos": math.cos, "tan": math.tan,
    "log": math.log, "log10": math.log10, "exp": math.exp, "abs": abs,
    "round": round, "min": min, "max": max, "pow": pow, "sum": sum, "len": len,
}
_SAFE_NAMES = {"pi": math.pi, "e": math.e}


def _eval_node(node):
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return node.value
    if isinstance(node, ast.BinOp) and type(node.op) in _SAFE_BINOPS:
        return _SAFE_BINOPS[type(node.op)](_eval_node(node.left), _eval_node(node.right))
    if isinstance(node, ast.UnaryOp) and type(node.op) in _SAFE_UNARYOPS:
        return _SAFE_UNARYOPS[type(node.op)](_eval_node(node.operand))
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in _SAFE_FUNCS:
        return _SAFE_FUNCS[node.func.id](*[_eval_node(a) for a in node.args])
    if isinstance(node, ast.Name) and node.id in _SAFE_NAMES:
        return _SAFE_NAMES[node.id]
    if isinstance(node, ast.List):
        return [_eval_node(e) for e in node.elts]
    raise ValueError("esa expresión tiene algo que no sé calcular")


def calcular(expresion: str) -> str:
    """Evalúa matemática de verdad en vez de que el modelo la 'estime' de memoria.
    No usa eval/exec: interpreta el árbol de sintaxis a mano contra una lista
    blanca de operaciones — no hay forma de que esto toque archivos, red, ni
    nada fuera de aritmética y funciones de math."""
    expresion = expresion.strip()
    if not expresion:
        return "No me diste ninguna expresión para calcular."
    try:
        arbol = ast.parse(expresion, mode="eval")
        resultado = _eval_node(arbol.body)
    except Exception as exc:
        return f"No pude calcular '{expresion}': {exc}"
    return f"El resultado es {resultado}."


# --- Comandos personalizados (macros) ---------------------------------------


def _read_json_dict(path: str) -> dict:
    if not os.path.exists(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except json.JSONDecodeError:
        return {}  # archivo corrupto (ej. cierre forzado a mitad de escritura)


def _write_json_dict(path: str, data: dict) -> None:
    try:
        dirname = os.path.dirname(path)
        if dirname:
            os.makedirs(dirname, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    except OSError as exc:
        print(f"No pude guardar {path}: {exc}")


def load_macros() -> dict:
    return _read_json_dict(COMMANDS_FILE)


def save_macros(macros: dict) -> None:
    _write_json_dict(COMMANDS_FILE, macros)


def create_macro(name: str, steps: list) -> str:
    clean_steps = [
        {"tool": step.get("tool"), "args": step.get("args", {})}
        for step in steps
        if step.get("tool") in SAFE_STEP_TOOLS
    ]
    if not clean_steps:
        return "No pude crear el comando: ningún paso era válido."
    macros = load_macros()
    macros[name.strip().lower()] = clean_steps
    save_macros(macros)
    return f"Comando '{name}' creado con {len(clean_steps)} paso(s)."


def run_macro(name: str) -> str:
    steps = load_macros().get(name.strip().lower())
    if not steps:
        return f"No existe el comando '{name}'."
    results = []
    for step in steps:
        tool = step.get("tool")
        if tool not in SAFE_STEP_TOOLS:
            continue
        results.append(run_tool(tool, json.dumps(step.get("args", {}))))
    return " ".join(results)


# --- Perfiles: quién habla ahora --------------------------------------------


def load_profiles() -> dict:
    return _read_json_dict(PROFILES_FILE)


def save_profiles(profiles: dict) -> None:
    _write_json_dict(PROFILES_FILE, profiles)


def _hash_password(password: str) -> str:
    return hashlib.sha256(password.strip().encode("utf-8")).hexdigest()


def load_reminders() -> dict:
    return _read_json_dict(REMINDERS_FILE)


def save_reminders(data: dict) -> None:
    _write_json_dict(REMINDERS_FILE, data)


def crear_recordatorio(texto: str, cuando: str = "", cuando_iso: str = "") -> str:
    texto = (texto or "").strip()
    if not texto:
        return "No entendí qué quieres que recuerde."
    cuando_iso = (cuando_iso or "").strip()
    if cuando_iso:
        try:
            datetime.fromisoformat(cuando_iso)
        except ValueError:
            cuando_iso = ""  # formato invalido: lo tratamos como recordatorio sin hora fija
    data = load_reminders()
    items = data.get(CURRENT_SPEAKER, [])
    items.append({
        "texto": texto,
        "cuando": (cuando or "").strip(),
        "cuando_iso": cuando_iso,
        "creado": time.time(),
        "avisado": False,
    })
    data[CURRENT_SPEAKER] = items
    save_reminders(data)
    if cuando_iso:
        return f"Listo, te aviso el {cuando_iso.replace('T', ' a las ')}."
    cuando_txt = f" para {cuando.strip()}" if cuando and cuando.strip() else ""
    return f"Listo, te lo voy a recordar{cuando_txt}."


def _avisar_recordatorios_pendientes() -> str:
    """Junta los recordatorios sin avisar de CURRENT_SPEAKER y los marca como avisados."""
    data = load_reminders()
    items = data.get(CURRENT_SPEAKER, [])
    pendientes = [it for it in items if not it.get("avisado")]
    if not pendientes:
        return ""
    for it in pendientes:
        it["avisado"] = True
    data[CURRENT_SPEAKER] = items
    save_reminders(data)
    return "; ".join(
        it["texto"] + (f" ({it['cuando']})" if it.get("cuando") else "") for it in pendientes
    )


def revisar_recordatorios_vencidos() -> list[tuple[str, dict]]:
    """Recorre TODOS los perfiles (no solo CURRENT_SPEAKER) buscando recordatorios
    con cuando_iso ya vencido y sin avisar — para el aviso proactivo, que puede
    dispararse sin que nadie se haya identificado. Marca como avisados los que
    encuentra, así no se repiten cuando esa persona se identifique después."""
    data = load_reminders()
    ahora = datetime.now()
    vencidos = []
    cambio = False
    for perfil, items in data.items():
        for it in items:
            if it.get("avisado"):
                continue
            due = it.get("cuando_iso")
            if not due:
                continue
            try:
                due_dt = datetime.fromisoformat(due)
            except ValueError:
                continue
            if due_dt <= ahora:
                vencidos.append((perfil, it))
                it["avisado"] = True
                cambio = True
    if cambio:
        save_reminders(data)
    return vencidos


def identificarse(nombre: str, password: str = "") -> str:
    global CURRENT_SPEAKER
    alias = nombre.strip().lower()
    if not alias:
        return "No entendí el nombre."

    profiles = load_profiles()
    for canonical, data in profiles.items():
        aliases = [a.lower() for a in data.get("apodos", [])]
        if alias == canonical or alias in aliases:
            display_name = data.get("nombre", canonical)
            if canonical == CURRENT_SPEAKER:
                return f"Ya estás como {display_name}."
            stored_hash = data.get("password")
            if stored_hash and _hash_password(password or "") != stored_hash:
                return f"{display_name} tiene sus datos protegidos. Dime la contraseña para entrar."
            CURRENT_SPEAKER = canonical
            pendientes = _avisar_recordatorios_pendientes()
            aviso = f" Ah, y me pediste que te recuerde: {pendientes}." if pendientes else ""
            return f"Hola de nuevo, {display_name}.{aviso}"

    profiles[alias] = {"nombre": nombre.strip(), "apodos": [alias], "password": None}
    save_profiles(profiles)
    CURRENT_SPEAKER = alias
    return (
        f"Un gusto, {nombre.strip()}. Te voy a reconocer la próxima vez que uses ese "
        "nombre o apodo. Ojo: como no reconozco voces de verdad, si alguien más usa "
        "el mismo nombre va a entrar a tu perfil — si te importa que sea solo tuyo, "
        "decime una contraseña para protegerlo."
    )


def proteger_perfil(password: str) -> str:
    password = (password or "").strip()
    if not password:
        return "Dime qué contraseña quieres poner."
    profiles = load_profiles()
    profile = profiles.get(CURRENT_SPEAKER, {"nombre": CURRENT_SPEAKER, "apodos": [CURRENT_SPEAKER]})
    profile["password"] = _hash_password(password)
    profiles[CURRENT_SPEAKER] = profile
    save_profiles(profiles)
    return "Listo, tu perfil ahora pide contraseña para que otros entren."


# --- Obsidian: exportar la memoria como una red de notas enlazadas ---------


def find_obsidian_vault() -> str | None:
    """Lee la config de Obsidian para encontrar el vault, sin pedirle la ruta al
    usuario. Prefiere el vault marcado como abierto; si no existe en disco
    (se movió o se borró), prueba con el resto de los vaults conocidos."""
    if not os.path.exists(OBSIDIAN_CONFIG):
        return None
    try:
        with open(OBSIDIAN_CONFIG, "r", encoding="utf-8") as f:
            config = json.load(f)
    except (json.JSONDecodeError, OSError):
        return None
    vaults = list(config.get("vaults", {}).values())
    vaults.sort(key=lambda v: not v.get("open", False))
    for vault in vaults:
        path = vault.get("path")
        if path and os.path.isdir(path):
            return path
    return None


def _obsidian_slug(text: str) -> str:
    """Nombre de archivo/carpeta válido en Windows a partir de un título."""
    cleaned = "".join(c if c not in '<>:"/\\|?*' else " " for c in text).strip()
    return cleaned or "Sin titulo"


def exportar_a_obsidian(nota: str = "") -> str:
    vault = find_obsidian_vault()
    if not vault:
        return "No encontré ningún vault de Obsidian en esta máquina."

    profiles = load_profiles()
    display_name = profiles.get(CURRENT_SPEAKER, {}).get("nombre", CURRENT_SPEAKER)
    person_slug = _obsidian_slug(display_name)

    person_dir = os.path.join(vault, "Jarvis", person_slug)
    data_dir = os.path.join(person_dir, "Datos")

    facts = load_facts()
    if not facts:
        return f"{display_name} todavía no tiene datos guardados para exportar."

    try:
        os.makedirs(data_dir, exist_ok=True)
        links = []
        for clave, valor in facts.items():
            titulo = clave.strip().capitalize()
            fact_slug = _obsidian_slug(titulo)
            with open(os.path.join(data_dir, f"{fact_slug}.md"), "w", encoding="utf-8") as f:
                f.write(f"# {titulo}\n\n- [[{person_slug}]]: {valor}\n")
            links.append(f"- [[{fact_slug}]]: {valor}")

        contenido = f"# {display_name}\n\n" + "\n".join(links)
        if nota.strip():
            contenido += f"\n\n{nota.strip()}\n"
        with open(os.path.join(person_dir, f"{person_slug}.md"), "w", encoding="utf-8") as f:
            f.write(contenido)
    except OSError as exc:
        return f"No pude escribir en el vault: {exc}"

    return f"Listo, exporté {len(facts)} dato(s) de {display_name} a Obsidian."


# --- Memoria: datos puntuales + notas permanentes + ventana de 12h ---------


def load_facts() -> dict:
    """Devuelve solo los datos del perfil que está hablando ahora (CURRENT_SPEAKER)."""
    return _read_json_dict(FACTS_FILE).get(CURRENT_SPEAKER, {})


def guardar_dato(clave: str, valor: str) -> str:
    all_facts = _read_json_dict(FACTS_FILE)
    profile_facts = all_facts.get(CURRENT_SPEAKER, {})
    clave_norm = clave.strip().lower()
    accion = "actualicé" if clave_norm in profile_facts else "guardé"
    profile_facts[clave_norm] = valor
    all_facts[CURRENT_SPEAKER] = profile_facts
    _write_json_dict(FACTS_FILE, all_facts)
    return f"Listo, {accion} '{clave}': {valor}."


def recordar(query: str) -> str:
    q = query.lower()
    facts = load_facts()
    fact_hits = [f"{k}: {v}" for k, v in facts.items() if k in q or q in k]
    if fact_hits:
        return "\n".join(fact_hits)

    if not os.path.exists(MEMORY_FILE):
        return "No encontré nada guardado sobre eso."
    terms = [t for t in q.split() if len(t) > 2]
    hits = []
    with open(MEMORY_FILE, "r", encoding="utf-8") as f:
        for line in f:
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue
            content = entry.get("content", "")
            if terms and any(t in content.lower() for t in terms):
                when = datetime.fromtimestamp(entry["ts"]).strftime("%d/%m %H:%M")
                hits.append(f"[{when}] {entry.get('role')}: {content}")
    if not hits:
        return "No encontré nada sobre eso en la memoria."
    return "\n".join(hits[-5:])


def persist_message(role: str, content: str) -> None:
    os.makedirs(MEMORY_DIR, exist_ok=True)
    with open(MEMORY_FILE, "a", encoding="utf-8") as f:
        f.write(json.dumps({"ts": time.time(), "role": role, "content": content}, ensure_ascii=False) + "\n")


def trim_history(history: list) -> None:
    """Mantiene acotado el historial para no comerse el límite de tokens/día gratis.

    Corta solo justo antes de un mensaje "user": un mensaje assistant con tool_calls
    tiene que quedar siempre junto a sus resultados "tool", cortar en el medio deja
    una secuencia inválida que Groq rechaza en todos los turnos siguientes."""
    if len(history) <= MAX_HISTORY_MESSAGES + 1:
        return
    cut = len(history) - MAX_HISTORY_MESSAGES
    while cut < len(history) and history[cut].get("role") != "user":
        cut += 1
    del history[1:cut]


def load_recent_history() -> list:
    history = [{"role": "system", "content": SYSTEM_PROMPT}]
    if not os.path.exists(MEMORY_FILE):
        return history
    cutoff = time.time() - MEMORY_WINDOW_SECONDS
    with open(MEMORY_FILE, "r", encoding="utf-8") as f:
        for line in f:
            try:
                entry = json.loads(line)
            except json.JSONDecodeError:
                continue
            if entry.get("ts", 0) >= cutoff and entry.get("role") in ("user", "assistant"):
                history.append({"role": entry["role"], "content": entry["content"]})
    trim_history(history)
    return history


def _dispatch_tool(name: str, args: dict) -> str:
    if name == "open_app":
        return open_app(args.get("name", ""))
    if name == "web_search":
        return web_search(args.get("query", ""))
    if name == "control_media":
        return control_media(args.get("action", ""))
    if name == "control_desktop":
        return control_desktop(args.get("action", ""))
    if name == "create_macro":
        return create_macro(args.get("name", ""), args.get("steps", []))
    if name == "run_macro":
        return run_macro(args.get("name", ""))
    if name == "guardar_dato":
        return guardar_dato(args.get("clave", ""), args.get("valor", ""))
    if name == "recordar":
        return recordar(args.get("query", ""))
    if name == "identificarse":
        return identificarse(args.get("nombre", ""), args.get("password", ""))
    if name == "proteger_perfil":
        return proteger_perfil(args.get("password", ""))
    if name == "exportar_a_obsidian":
        return exportar_a_obsidian(args.get("nota", ""))
    if name == "crear_recordatorio":
        return crear_recordatorio(args.get("texto", ""), args.get("cuando", ""), args.get("cuando_iso", ""))
    if name == "focus_window":
        return focus_window(args.get("title_contains", ""))
    if name == "list_windows":
        return list_windows()
    if name == "type_text":
        return type_text(args.get("texto", ""), args.get("ventana", ""), bool(args.get("enviar", False)))
    if name == "list_files":
        return list_files(args.get("carpeta", ""))
    if name == "read_file":
        return read_file(args.get("ruta", ""))
    if name == "buscar_archivo":
        return buscar_archivo(args.get("nombre", ""), args.get("carpeta", ""))
    if name == "leer_portapapeles":
        return leer_portapapeles()
    if name == "copiar_portapapeles":
        return copiar_portapapeles(args.get("texto", ""))
    if name == "info_sistema":
        return info_sistema()
    if name == "leer_pagina":
        return leer_pagina(args.get("url", ""))
    if name == "mover_archivo":
        return mover_archivo(args.get("origen", ""), args.get("destino_carpeta", ""))
    if name == "borrar_archivo":
        return borrar_archivo(args.get("ruta", ""))
    if name == "calcular":
        return calcular(args.get("expresion", ""))
    if name == "registrar_dispositivo":
        return registrar_dispositivo(args.get("nombre", ""), args.get("host", ""))
    if name == "gestionar_dispositivo":
        return gestionar_dispositivo(args.get("nombre", ""), args.get("comando", ""))
    return f"Herramienta desconocida: {name}"


def run_tool(name: str, arguments_json: str) -> str:
    try:
        args = json.loads(arguments_json or "{}")
    except json.JSONDecodeError:
        args = {}
    if not isinstance(args, dict):
        return "Argumentos de la herramienta con formato inesperado."
    try:
        return _dispatch_tool(name, args)
    except Exception as exc:
        # una herramienta individual rota (ej. una API de Windows que falla) no
        # puede tirar abajo todo el proceso de Jarvis — se reporta y se sigue.
        print(f"[herramienta] {name} crasheó: {exc}")
        return f"Hubo un error ejecutando {name}, no pude completarlo."


# --- Voz: síntesis + reproducción -------------------------------------------


async def _synthesize(text: str, path: str) -> None:
    await edge_tts.Communicate(text, TTS_VOICE).save(path)


def play_sound(path: str) -> None:
    if path and os.path.exists(path):
        try:
            playsound(path)
        except Exception as exc:
            print(f"No pude reproducir {path}: {exc}")


def _mci(command: str, warn: bool = True) -> bool:
    error = ctypes.windll.winmm.mciSendStringW(command, None, 0, None)
    if error and warn:
        print(f"[mci] '{command}' -> error {error}")
    return error == 0


def loop_sound_start(path: str, alias: str, start_range: tuple[float, float] | None = None) -> None:
    """Arranca un sonido en loop en segundo plano (no bloquea). No-op si el archivo no existe.

    start_range=(min_s, max_s): arranca desde un segundo aleatorio en ese rango
    (variedad entre búsquedas); si se llega al final, el loop retoma desde el inicio.
    """
    if not path or not os.path.exists(path):
        return
    _mci(f'close {alias}', warn=False)  # por si quedó una instancia previa sin cerrar; normal que "falle"
    _mci(f'open "{path}" alias {alias}')
    if start_range and start_range[1] > start_range[0]:
        start_ms = int(random.uniform(*start_range) * 1000)
        _mci(f'play {alias} from {start_ms} repeat')
    else:
        _mci(f'play {alias} repeat')


def loop_sound_stop(alias: str) -> None:
    _mci(f'stop {alias}', warn=False)
    _mci(f'close {alias}', warn=False)


def play_sound_capped(path: str, max_ms: int) -> None:
    """Como play_sound, pero nunca bloquea más de max_ms — para que un sonido de
    activación largo no retrase el arranque de la grabación del comando."""
    if not path or not os.path.exists(path):
        return
    alias = "jarvis_activacion"
    if not _mci(f'open "{path}" alias {alias}'):
        return  # sin poder abrirlo por MCI no hay forma de acotar la duracion; se salta el sonido
    _mci(f"play {alias}")
    time.sleep(max_ms / 1000)
    _mci(f"stop {alias}", warn=False)
    _mci(f"close {alias}", warn=False)


def _mci_status(alias: str) -> str:
    buf = ctypes.create_unicode_buffer(128)
    ctypes.windll.winmm.mciSendStringW(f"status {alias} mode", buf, 128, None)
    return buf.value.strip().lower()


def _play_interruptible(path: str) -> None:
    """Reproduce el audio, pero corta apenas alguien empieza a hablar fuerte por el
    mic — así se puede interrumpir a Jarvis en vez de esperar a que termine."""
    alias = "jarvis_voz"
    if ACTIVE_STREAM is None or not _mci(f'open "{path}" alias {alias}'):
        playsound(path)  # respaldo: sin stream activo o si MCI falla, reproducción normal
        return
    try:
        _mci(f"play {alias}")
        while _mci_status(alias) == "playing":
            chunk, _ = ACTIVE_STREAM.read(FRAME_SAMPLES)
            if _frame_energy(chunk.flatten()) > SILENCE_RMS * INTERRUPT_ENERGY_MULTIPLIER:
                break  # alguien empezó a hablar fuerte: Jarvis se calla
    finally:
        _mci(f"stop {alias}", warn=False)
        _mci(f"close {alias}", warn=False)


def speak(text: str) -> None:
    print(f"Jarvis: {text}")
    if UI:
        UI.set_speaking(True)
    fd, path = tempfile.mkstemp(suffix=".mp3")
    os.close(fd)
    try:
        asyncio.run(_synthesize(text, path))
        _play_interruptible(path)
    except Exception as exc:
        print(f"No pude hablar: {exc}")
    finally:
        try:
            os.remove(path)
        except OSError:
            pass  # el reproductor puede seguir con el handle abierto un instante
        if UI:
            UI.set_speaking(False)


# --- Audio: wake word + grabación con detección de silencio -----------------

CALIBRATION_FILE = os.path.join(MEMORY_DIR, "calibracion.json")
CALIBRATION_SECONDS = 2.0
MIN_CALIBRATION_SECONDS = 0.4
CALIBRATION_SHRINK_PER_RUN = 0.15  # cuanto se acorta la calibración por cada corrida previa
CALIBRATION_VERSION = 2  # subir esto invalida calibraciones viejas guardadas (ver _load_calibration)


def _load_calibration() -> tuple[int | None, int]:
    if not os.path.exists(CALIBRATION_FILE):
        return None, 0
    try:
        with open(CALIBRATION_FILE, "r", encoding="utf-8") as f:
            saved = json.load(f)
        if saved.get("version") != CALIBRATION_VERSION:
            return None, 0  # calibracion de una version vieja de la formula: no la arrastramos
        threshold = saved.get("threshold")
        runs = int(saved.get("runs", 0))
        if not isinstance(threshold, (int, float)) or runs < 0:
            return None, 0
        return int(threshold), runs
    except (json.JSONDecodeError, OSError, TypeError, ValueError):
        return None, 0  # calibración corrupta/vieja, arrancamos de cero sin crashear


def _save_calibration(threshold: int, runs: int) -> None:
    try:
        os.makedirs(MEMORY_DIR, exist_ok=True)
        with open(CALIBRATION_FILE, "w", encoding="utf-8") as f:
            json.dump({"version": CALIBRATION_VERSION, "threshold": threshold, "runs": runs}, f)
    except OSError as exc:
        print(f"No pude guardar la calibración: {exc}")


def calibrate_silence_threshold(stream: sd.InputStream) -> int:
    """Mide el ruido de fondo real al arrancar (en vez de asumir un valor fijo a ciegas).

    Se llama justo después de abrir el stream y antes de anunciar que Jarvis está
    listo, así que el usuario todavía no pudo haber dicho nada — ese instante ya es
    silencio de fondo real por sí solo. Usa el percentil 10 en vez del promedio para
    que un ruido puntual (tos, portazo) durante la calibración no arruine el número.

    Cuantas más veces corrió antes, más corta la calibración (confía más en el
    historial acumulado) y el umbral final pesa más el historial que la muestra
    fresca — así arranca cada vez más rápido sin perder precisión.
    """
    global SILENCE_RMS
    previous_threshold, runs = _load_calibration()

    duration = max(MIN_CALIBRATION_SECONDS, CALIBRATION_SECONDS - CALIBRATION_SHRINK_PER_RUN * runs)
    frames_needed = max(1, int(duration * SAMPLE_RATE / FRAME_SAMPLES))
    energies = []
    for _ in range(frames_needed):
        chunk, _ = stream.read(FRAME_SAMPLES)
        energies.append(_frame_energy(chunk.flatten()))
    noise_floor = float(np.percentile(energies, 10))
    fresh = max(int(noise_floor * 2.2), 90)  # antes x4/mínimo 150: muy lento para reconocer que empezaste a hablar

    if previous_threshold:
        SILENCE_RMS = round((fresh + previous_threshold * runs) / (runs + 1))
    else:
        SILENCE_RMS = fresh

    _save_calibration(SILENCE_RMS, runs + 1)
    return SILENCE_RMS


def to_wav_bytes(audio: np.ndarray) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(SAMPLE_RATE)
        wav_file.writeframes(audio.astype(np.int16).tobytes())
    return buf.getvalue()


def _frame_energy(chunk: np.ndarray) -> float:
    # int32 antes de abs: abs(-32768) desborda un int16 (el rango es -32768..32767)
    # y da la vuelta a -32768, clasificando el audio mas fuerte como silencio.
    return float(np.abs(chunk.astype(np.int32)).mean())


def record_command(stream: sd.InputStream) -> np.ndarray:
    """Graba un comando. Si nadie empieza a hablar en LISTEN_TIMEOUT_SECONDS, se rinde
    (audio vacío) — así se usa tanto para el primer comando tras el wake word como
    para los turnos de una conversación continua sin repetir "Hey Jarvis". Solo se
    acumula audio desde que arranca el habla: si nunca arranca, no hay nada que
    devolver y los llamadores lo detectan como fin de conversación."""
    frames = []
    silence_run = 0
    waited = 0
    heard_speech = False
    max_frames = int(MAX_COMMAND_SECONDS * SAMPLE_RATE / FRAME_SAMPLES)
    wait_frames = int(LISTEN_TIMEOUT_SECONDS * SAMPLE_RATE / FRAME_SAMPLES)
    for _ in range(max_frames):
        chunk, _ = stream.read(FRAME_SAMPLES)
        chunk = chunk.flatten()
        energy = _frame_energy(chunk)
        if energy > SILENCE_RMS:
            heard_speech = True
            silence_run = 0
            frames.append(chunk)
        elif heard_speech:
            silence_run += 1
            frames.append(chunk)
            if silence_run >= SILENCE_FRAMES_TO_STOP:
                break
        else:
            waited += 1
            if waited >= wait_frames:
                break  # nadie empezó a hablar, se acabó la espera
    if not frames:
        return np.zeros((0,), dtype=np.int16)
    return np.concatenate(frames, axis=0)


def transcribe(audio: np.ndarray) -> str:
    response = SESSION.post(
        f"{GROQ_BASE}/audio/transcriptions",
        headers={"Authorization": f"Bearer {GROQ_API_KEY}"},
        files={"file": ("audio.wav", to_wav_bytes(audio), "audio/wav")},
        data={"model": STT_MODEL, "language": "es"},
        timeout=60,
    )
    response.raise_for_status()
    return response.json().get("text", "").strip()


def ask_groq(history: list) -> str:
    # se inyecta fresca en cada pedido (no se guarda en history) para que crear_recordatorio
    # pueda calcular fechas relativas ("en 10 minutos", "mañana") sin que quede vieja en
    # una sesión que puede correr corriendo por días.
    fecha_actual = {"role": "system", "content": f"Fecha y hora actual: {datetime.now().isoformat(timespec='seconds')}"}
    for _ in range(6):
        response = SESSION.post(
            f"{GROQ_BASE}/chat/completions",
            headers={"Authorization": f"Bearer {GROQ_API_KEY}"},
            json={"model": CHAT_MODEL, "messages": [history[0], fecha_actual] + history[1:], "tools": TOOLS},
            timeout=60,
        )
        response.raise_for_status()
        choices = response.json().get("choices") or []
        if not choices:
            return "Groq no mandó respuesta, probá de nuevo."
        message = choices[0]["message"]
        tool_calls = message.get("tool_calls")
        if not tool_calls:
            return message.get("content") or ""

        history.append(message)
        for call in tool_calls:
            result = run_tool(call["function"]["name"], call["function"]["arguments"])
            print(f"[herramienta] {call['function']['name']}({call['function']['arguments']}) -> {result}")
            history.append({"role": "tool", "tool_call_id": call["id"], "content": result})

    return "Me hice bolas con eso, probá de nuevo."


FAREWELL_PHRASES = [
    "adios", "adiós", "hasta luego", "hasta la proxima", "hasta la próxima",
    "nos vemos", "me despido", "chao", "chau", "bye",
    "eso es todo", "eso seria todo", "eso sería todo", "nada mas", "nada más",
    "ya esta", "ya está", "ya no necesito nada", "gracias eso es todo",
]


def _is_farewell(text: str) -> bool:
    normalized = text.strip().lower()
    return any(phrase in normalized for phrase in FAREWELL_PHRASES)


def _is_stop_word(text: str) -> bool:
    """Se compara contra el texto crudo transcripto, ANTES de que nada se le mande
    al modelo — el modelo nunca ve STOP_WORD (no está en el system prompt ni en
    ningún request a Groq), así que no hay forma de que lo razone, lo repita, o
    lo ignore. Es un freno que depende únicamente de este chequeo local."""
    return bool(STOP_WORD) and STOP_WORD.lower() in text.lower()


def procesar_texto(text: str, history: list) -> tuple[str | None, bool]:
    """Corre el pipeline de seguridad + modelo sobre un texto ya transcripto (voz)
    o recibido de otro dispositivo vía gestionar_dispositivo — mismo camino para
    los dos, así ningún filtro de seguridad se salta por venir de la red.
    Devuelve (respuesta_para_decir_o_None, seguir_conversación)."""
    if _is_stop_word(text):
        print("Palabra de apagado detectada. Cerrando Jarvis.")
        SHUTDOWN_EVENT.set()  # el loop principal (en su propio hilo) hace el cierre limpio
        return "Jarvis desactivado.", False

    if _is_farewell(text):
        persist_message("user", text)
        return "Hasta luego.", False

    turn_start = len(history)
    history.append({"role": "user", "content": text})
    persist_message("user", text)
    try:
        reply = ask_groq(history)
    except requests.HTTPError as exc:
        status = exc.response.status_code if exc.response is not None else None
        print(f"Error hablando con Groq: {exc}")
        del history[turn_start:]  # descarta todo el turno, no solo el último mensaje
        if status == 429:
            return "Me quedé sin cupo de peticiones por ahora, dame un momento.", True
        return "No puedo conectar con Groq ahora mismo.", True
    except requests.RequestException as exc:
        print(f"Error hablando con Groq: {exc}")
        del history[turn_start:]
        return "No puedo conectar con Groq ahora mismo.", True

    history.append({"role": "assistant", "content": reply})
    persist_message("assistant", reply)
    trim_history(history)
    return reply, True


def handle_command(audio: np.ndarray, history: list) -> bool:
    """Procesa un comando de voz. Devuelve False si hay que volver a modo
    wake-word-only (despedida o apagado), True si la conversación sigue abierta."""
    if audio.size < SAMPLE_RATE * 0.3:
        return True
    try:
        text = transcribe(audio)
    except requests.RequestException as exc:
        print(f"Error transcribiendo: {exc}")
        speak("No pude transcribir el audio.")
        return True
    if not text:
        return True
    print(f"Vos: {text}")

    reply, seguir = procesar_texto(text, history)
    if reply:
        speak(reply)
    return seguir


PID_FILE = os.path.join(PROJECT_DIR, "jarvis.pid")
WAKEWORD_MODELS_DIR = os.path.join(os.environ.get("LOCALAPPDATA", PROJECT_DIR), "Jarvis", "models")


def _load_wake_word_model() -> WakeWordModel:
    """Descarga los modelos a una carpeta propia y persistente en vez de la
    carpeta del paquete de openwakeword: empaquetado como .exe, esa carpeta
    vive en un directorio temporal que se borra al cerrar, así que cada
    arranque tendría que volver a bajar todo. Además pasa rutas explícitas
    (wakeword + melspectrogram + embedding) en vez de solo el nombre corto,
    porque openwakeword por defecto busca esos dos últimos siempre en la
    carpeta del paquete sin importar de dónde venga el modelo de wake word.

    Si una descarga queda corrupta (pasó una vez en pruebas: mismo tamaño,
    contenido distinto — parece un corte de red a mitad de escritura),
    reintenta una vez borrando todo y volviendo a bajar antes de rendirse."""
    paths = {
        "wake": os.path.join(WAKEWORD_MODELS_DIR, "hey_jarvis_v0.1.onnx"),
        "mel": os.path.join(WAKEWORD_MODELS_DIR, "melspectrogram.onnx"),
        "emb": os.path.join(WAKEWORD_MODELS_DIR, "embedding_model.onnx"),
    }
    for intento in range(2):
        download_wakeword_models([WAKE_WORD_NAME], target_directory=WAKEWORD_MODELS_DIR)
        try:
            return WakeWordModel(
                wakeword_models=[paths["wake"]],
                melspec_model_path=paths["mel"],
                embedding_model_path=paths["emb"],
                inference_framework="onnx",
            )
        except Exception as exc:
            print(f"Modelo de wake word con problemas (intento {intento + 1}/2): {exc}")
            shutil.rmtree(WAKEWORD_MODELS_DIR, ignore_errors=True)
    raise SystemExit("No pude preparar el modelo de wake word después de reintentar. Revisa tu conexión.")


def _pid_is_running(pid: int) -> bool:
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if handle:
        ctypes.windll.kernel32.CloseHandle(handle)
        return True
    return False


# --- Auto-actualizacion del .exe (solo empaquetado; el script se actualiza
# solo via setup.bat/git pull, esto es la contraparte para el .exe) --------

JARVIS_VERSION = "1.0.4"  # subir a mano en cada release, junto con el tag de git
GITHUB_REPO = "podselxd/jarvis"


def _schedule_exe_replacement(new_exe_path: str) -> None:
    """Windows no deja que un .exe se reemplace a si mismo mientras esta
    corriendo, asi que arma un .bat descartable que espera a que este
    proceso cierre, pisa el .exe viejo con el nuevo ya descargado, y lo
    vuelve a abrir. El propio jarvis.py dispara su apagado limpio despues
    de llamar a esto (via SHUTDOWN_EVENT), el helper hace el resto."""
    current_exe = sys.executable
    pid = os.getpid()
    helper_path = os.path.join(tempfile.gettempdir(), "jarvis_actualizar.bat")
    script = (
        "@echo off\r\n"
        ":esperar\r\n"
        f'tasklist /FI "PID eq {pid}" 2>nul | find "{pid}" >nul\r\n'
        "if not errorlevel 1 (\r\n"
        "    timeout /t 1 /nobreak >nul\r\n"
        "    goto esperar\r\n"
        ")\r\n"
        f'move /y "{new_exe_path}" "{current_exe}" >nul\r\n'
        f'start "" "{current_exe}"\r\n'
        'del "%~f0"\r\n'
    )
    with open(helper_path, "w", encoding="utf-8") as f:
        f.write(script)
    subprocess.Popen(["cmd", "/c", helper_path], creationflags=subprocess.CREATE_NO_WINDOW, close_fds=True)


def _check_for_exe_update() -> None:
    """Si corre empaquetado y hay una version mas nueva publicada en GitHub
    Releases, la descarga y deja lista para aplicarse al cerrar. No hace
    nada corriendo como script (ahi ya esta el update.py/git pull)."""
    if not getattr(sys, "frozen", False):
        return
    try:
        resp = SESSION.get(f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest", timeout=10)
        resp.raise_for_status()
        data = resp.json()
        latest_tag = data.get("tag_name", "").lstrip("v")
        if not latest_tag or latest_tag == JARVIS_VERSION:
            return
        asset = next((a for a in data.get("assets", []) if a.get("name") == "Jarvis.exe"), None)
        if not asset:
            return

        print(f"Nueva versión disponible: {latest_tag} (actual: {JARVIS_VERSION}). Descargando...")
        new_path = os.path.join(PROJECT_DIR, "Jarvis_nuevo.exe")
        with SESSION.get(asset["browser_download_url"], stream=True, timeout=180) as r:
            r.raise_for_status()
            with open(new_path, "wb") as f:
                for chunk in r.iter_content(chunk_size=1024 * 1024):
                    f.write(chunk)

        _schedule_exe_replacement(new_path)
        print(f"Actualización a {latest_tag} lista, se aplica ahora.")
        speak(f"Encontré una actualización, dame un segundo para instalarla.")
        SHUTDOWN_EVENT.set()
    except Exception as exc:
        print(f"No pude revisar/bajar actualizaciones: {exc}")


def _anunciar_recordatorios_vencidos() -> None:
    """Habla sola, sin que nadie haya dicho 'Hey Jarvis' — se llama periódicamente
    desde el loop principal mientras está en modo wake-word-only (nunca en medio
    de una conversación activa, así no interrumpe lo que se esté hablando)."""
    vencidos = revisar_recordatorios_vencidos()
    if not vencidos:
        return
    perfiles = load_profiles()
    for perfil, recordatorio in vencidos:
        nombre = perfiles.get(perfil, {}).get("nombre") if perfil != DEFAULT_PROFILE else None
        prefijo = f"{nombre}, te" if nombre else "Te"
        speak(f"{prefijo} quería recordar: {recordatorio['texto']}")


# --- Malla: conectar los Jarvis de tus propios dispositivos vía Tailscale ---


def _tailscale_ip() -> str | None:
    try:
        result = subprocess.run(
            ["tailscale", "ip", "-4"], capture_output=True, text=True, timeout=5
        )
        ip = result.stdout.strip()
        return ip if result.returncode == 0 and ip else None
    except (OSError, subprocess.SubprocessError):
        return None


def _ensure_mesh_secret() -> str:
    """El secreto se genera una sola vez (no lo elige el usuario, a diferencia de
    la palabra de apagado: tiene que ser largo y aleatorio, nunca se dice en voz
    alta) y se guarda en .env para copiarlo a mano a los demás dispositivos."""
    global MESH_SECRET
    if MESH_SECRET:
        return MESH_SECRET
    MESH_SECRET = secrets.token_hex(32)
    with open(os.path.join(PROJECT_DIR, ".env"), "a", encoding="utf-8") as f:
        f.write(f"JARVIS_MESH_SECRET={MESH_SECRET}\n")
    print(f"Secreto de malla nuevo (copialo al .env de tus otros dispositivos): {MESH_SECRET}")
    return MESH_SECRET


def load_devices() -> dict:
    return _read_json_dict(DEVICES_FILE)


def registrar_dispositivo(nombre: str, host: str) -> str:
    nombre = nombre.strip().lower()
    host = host.strip()
    if not nombre or not host:
        return "Necesito un nombre y la dirección de Tailscale de ese dispositivo."
    devices = load_devices()
    devices[nombre] = host
    _write_json_dict(DEVICES_FILE, devices)
    return f"Listo, registré '{nombre}' en {host}."


def gestionar_dispositivo(nombre: str, comando: str) -> str:
    """Le manda un comando de texto al Jarvis de OTRO dispositivo tuyo, ya
    registrado, y devuelve lo que ese Jarvis contestó."""
    devices = load_devices()
    host = devices.get(nombre.strip().lower())
    if not host:
        conocidos = ", ".join(devices) or "ninguno todavía"
        return f"No tengo registrado un dispositivo llamado '{nombre}'. Los que conozco: {conocidos}."
    if not comando.strip():
        return "No me dijiste qué comando mandarle."
    try:
        resp = requests.post(
            f"http://{host}:{MESH_PORT}/comando",
            json={"comando": comando},
            headers={"X-Jarvis-Secret": _ensure_mesh_secret()},
            timeout=30,
        )
    except requests.RequestException as exc:
        return f"No pude conectarme con '{nombre}': {exc}"
    if resp.status_code == 401:
        return f"'{nombre}' rechazó el pedido (secreto de malla no coincide)."
    if resp.status_code != 200:
        return f"'{nombre}' respondió con un error ({resp.status_code})."
    return resp.text or "Listo."


class _MeshRequestHandler(BaseHTTPRequestHandler):
    history: list = []  # historial propio de los pedidos que llegan por malla, separado del de voz

    def log_message(self, format, *args):
        pass  # no ensuciar la consola con el log de acceso default de http.server

    def do_POST(self) -> None:
        if self.path != "/comando":
            self.send_response(404)
            self.end_headers()
            return
        if self.headers.get("X-Jarvis-Secret", "") != MESH_SECRET:
            self.send_response(401)
            self.end_headers()
            self.wfile.write(b"secreto invalido")
            return
        try:
            length = int(self.headers.get("Content-Length", 0))
            data = json.loads(self.rfile.read(length) or b"{}")
        except (ValueError, json.JSONDecodeError):
            self.send_response(400)
            self.end_headers()
            return
        comando = str(data.get("comando", "")).strip()
        if not comando:
            self.send_response(400)
            self.end_headers()
            return

        if not COMMAND_LOCK.acquire(timeout=8):
            self._responder(503, "Jarvis está ocupado ahora, probá en un momento.")
            return
        try:
            reply, _ = procesar_texto(comando, _MeshRequestHandler.history)
        finally:
            COMMAND_LOCK.release()
        self._responder(200, reply or "Listo.")

    def _responder(self, code: int, texto: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(texto.encode("utf-8"))


def start_mesh_server() -> None:
    """Arranca el servidor SOLO si Tailscale está corriendo — se bindea
    específicamente a la IP de Tailscale, nunca a 0.0.0.0, así que ni con un
    error de configuración queda alcanzable desde fuera de tu red privada."""
    ip = _tailscale_ip()
    if not ip:
        print("Tailscale no está activo, el servidor de malla no arranca (Jarvis sigue funcionando normal).")
        return
    _ensure_mesh_secret()
    try:
        server = HTTPServer((ip, MESH_PORT), _MeshRequestHandler)
    except OSError as exc:
        print(f"No pude levantar el servidor de malla en {ip}:{MESH_PORT}: {exc}")
        return
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print(f"Servidor de malla escuchando en {ip}:{MESH_PORT} (solo alcanzable por tu Tailscale).")


def main() -> None:
    if sys.stdout is None:
        # pythonw (autostart silencioso) no tiene consola: sys.stdout/stderr son None
        # y cualquier print() crashearía. Los redirigimos a un log en vez de a la nada.
        log = open(os.path.join(PROJECT_DIR, "jarvis.log"), "a", encoding="utf-8", buffering=1)
        sys.stdout = log
        sys.stderr = log
    else:
        # la consola de Windows suele usar cp1252/cp850, que no puede representar
        # cualquier caracter que venga de una búsqueda web (ej. ⓘ) y tira UnicodeEncodeError
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")

    global GROQ_API_KEY, USER_NAME, STOP_WORD
    if not GROQ_API_KEY:
        import instalador

        if not instalador.run_setup_if_needed():
            raise SystemExit("Configuración cancelada, no arranco sin una API key de Groq.")
        _load_dotenv()  # ahora hay .env recien escrito por el formulario: se relee
        GROQ_API_KEY = os.environ.get("GROQ_API_KEY", "").strip()
        USER_NAME = os.environ.get("JARVIS_USER_NAME", "").strip()
        STOP_WORD = os.environ.get("JARVIS_STOP_WORD", "").strip()

    if os.path.exists(PID_FILE):
        try:
            with open(PID_FILE, "r", encoding="utf-8") as f:
                old_pid = int(f.read().strip())
            if _pid_is_running(old_pid):
                raise SystemExit(
                    f"Ya hay un Jarvis corriendo (PID {old_pid}). "
                    "Usa detener.bat si quieres cerrarlo antes de abrir otro."
                )
        except (ValueError, OSError):
            pass  # jarvis.pid vacío/corrupto de una instancia anterior, lo pisamos

    with open(PID_FILE, "w", encoding="utf-8") as f:
        f.write(str(os.getpid()))

    try:
        print(f"Jarvis {JARVIS_VERSION} — revisando actualizaciones...")
        _check_for_exe_update()
        if SHUTDOWN_EVENT.is_set():
            return  # version nueva ya descargada, el .bat ayudante hace el reemplazo al salir

        print("Verificando modelo de wake word (se descarga solo la primera vez)...")
        wake_model = _load_wake_word_model()
        history = load_recent_history()
        if USER_NAME:
            identificarse(USER_NAME)  # perfil por defecto desde el arranque, sin esperar a que se presente por voz

        global UI
        try:
            UI = JarvisUI()
        except Exception as exc:
            print(f"No pude abrir la interfaz visual, sigo sin ventana: {exc}")
            UI = None

        global ACTIVE_STREAM
        stream = sd.InputStream(samplerate=SAMPLE_RATE, channels=1, dtype="int16", blocksize=FRAME_SAMPLES)
        stream.start()
        ACTIVE_STREAM = stream
        print("Calibrando nivel de silencio, no hace falta que digas nada...")
        threshold = calibrate_silence_threshold(stream)
        print(f"Umbral de silencio ajustado a tu ambiente: {threshold}")

        start_mesh_server()

        speak("Jarvis en línea.")
        print('Listo. Di "Hey Jarvis" para activarlo. Para apagarlo: detener.bat')
        try:
            frame_count = 0
            while not SHUTDOWN_EVENT.is_set():
                chunk, _ = stream.read(FRAME_SAMPLES)
                frame_count += 1
                if frame_count % REMINDER_CHECK_FRAMES == 0:
                    _anunciar_recordatorios_vencidos()
                scores = wake_model.predict(chunk.flatten())
                if scores.get(WAKE_WORD_NAME, 0.0) > WAKE_THRESHOLD:
                    wake_model.reset()
                    print("Wake word detectada.")
                    play_sound_capped(SOUND_ACTIVATION, ACTIVATION_MAX_MS)
                    while True:
                        audio = record_command(stream)
                        if audio.size < SAMPLE_RATE * 0.3:
                            break  # nadie siguió hablando, se acabó la conversación
                        if not handle_command(audio, history):
                            break  # se despidió (o pidió apagado), vuelve a modo wake-word-only
                        if SHUTDOWN_EVENT.is_set():
                            break
                    print('Escuchando "Hey Jarvis"...')
        except KeyboardInterrupt:
            print("Cerrando Jarvis.")
        finally:
            stream.stop()
            stream.close()
            ACTIVE_STREAM = None
    finally:
        if os.path.exists(PID_FILE):
            os.remove(PID_FILE)


if __name__ == "__main__":
    main()
