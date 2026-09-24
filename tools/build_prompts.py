"""Genera res/tools.json y res/system_prompt.txt (herramientas y prompt que
Jarvis le manda a Groq en cada pedido). Están escritos compactos a propósito:
la capa gratis de Groq da 8000 tokens por minuto por modelo, y con las
descripciones largas de antes cada pedido gastaba ~3800 — dos pedidos y ya
no quedaba cupo. Solo hace falta correrlo si cambias estos textos.

Uso: python tools/build_prompts.py"""

import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SYSTEM_PROMPT = (
    "Eres Jarvis, asistente de voz personal. Responde en español latinoamericano con tú, nunca voseo "
    "(nada de 'sos', 'tenés', 'querés'), breve y natural, como hablando en voz alta: sin markdown, listas "
    "ni emojis. Entiende la intención aunque quien habla use otro dialecto.\n"
    "Usa las herramientas cuando el pedido lo requiera. Si tiene varios pasos (ej. 'abre X, dale play y pon "
    "pantalla completa'), pide todas las herramientas juntas en la misma respuesta, en orden. Si una tecla "
    "necesita foco, usa focus_window (o list_windows si no sabes el título) en esa misma respuesta. Con "
    "type_text, si no es obvio dónde caerá el texto, usa 'ventana'.\n"
    "Si te cuenta o corrige un dato personal, pregunta si quiere que lo recuerdes y usa guardar_dato solo si "
    "confirma; si ya te pidió que lo recuerdes, guárdalo directo. Si quiere que se lo recuerdes más adelante, "
    "usa crear_recordatorio sin pedir confirmación. Si preguntan algo de otra conversación, usa recordar en "
    "vez de adivinar.\n"
    "Si una búsqueda no trae algo claro, contesta con lo más cercano o admítelo; reintenta como mucho una vez.\n"
    "Si alguien se presenta o dice quién es (aunque sea en broma, como 'llegó mamá'), usa identificarse; si "
    "ese perfil pide contraseña, pídesela. Cualquiera puede pedirte cosas normales: la contraseña solo protege "
    "los datos guardados de cada persona. Para proteger su propio perfil, usa proteger_perfil.\n"
    "Antes de exportar_a_obsidian, pregunta y espera un sí."
)

SAFE_STEPS = ["open_app", "web_search", "control_media", "control_desktop", "focus_window", "type_text"]


def tool(name, desc, props=None, required=None):
    params = {"type": "object", "properties": props or {}}
    if required:
        params["required"] = required
    return {"type": "function", "function": {"name": name, "description": desc, "parameters": params}}


def s(desc=None):
    d = {"type": "string"}
    if desc:
        d["description"] = desc
    return d


TOOLS = [
    tool("open_app", "Abre una app (también las del menú Inicio: Discord, Steam, Spotify, etc.), carpeta, archivo o página web.",
         {"name": s("Ej: 'chrome', 'discord', 'descargas', 'youtube.com'")}, ["name"]),
    tool("web_search", "Busca en internet información actual: noticias, clima, precios, datos recientes.",
         {"query": s()}, ["query"]),
    tool("control_media", "Volumen del sistema y reproducción multimedia. set_volume pone el volumen exacto en 'nivel' (0-100).",
         {"action": {"type": "string", "enum": ["volume_up", "volume_down", "mute", "play_pause", "next_track",
                                                "previous_track", "set_volume"]},
          "nivel": {"type": "integer"}}, ["action"]),
    tool("control_desktop", "Mostrar escritorio, cambiar de ventana, minimizar todo, bloquear la PC, tecla F (pantalla "
                            "completa del video) o close_tab (Ctrl+W). F y close_tab actúan sobre la ventana enfocada.",
         {"action": {"type": "string", "enum": ["show_desktop", "switch_window", "minimize_all", "lock", "fullscreen",
                                                "close_tab"]}}, ["action"]),
    tool("create_macro", "Crea un comando personalizado con varios pasos para repetirlo después con run_macro.",
         {"name": s(), "steps": {"type": "array", "items": {"type": "object", "properties": {
             "tool": {"type": "string", "enum": SAFE_STEPS}, "args": {"type": "object"}}, "required": ["tool", "args"]}}},
         ["name", "steps"]),
    tool("run_macro", "Ejecuta un comando creado con create_macro.", {"name": s()}, ["name"]),
    tool("guardar_dato", "Guarda o corrige un dato del usuario bajo una clave corta (ej. 'cumpleaños'); sobrescribe si ya existía.",
         {"clave": s(), "valor": s()}, ["clave", "valor"]),
    tool("recordar", "Busca en los datos guardados y en conversaciones pasadas (más allá de las últimas 12 horas).",
         {"query": s()}, ["query"]),
    tool("identificarse", "Cambia a la persona que habla por nombre o apodo (crea su perfil si es nuevo). Si el perfil "
                          "tiene contraseña, pásala en password.",
         {"nombre": s(), "password": s()}, ["nombre"]),
    tool("proteger_perfil", "Pone o cambia la contraseña del perfil de quien habla.", {"password": s()}, ["password"]),
    tool("exportar_a_obsidian", "Exporta los datos de quien habla a notas enlazadas de Obsidian. 'nota': contexto extra opcional.",
         {"nota": s()}),
    tool("crear_recordatorio", "Recordatorio para quien habla. Si dio un momento, calcula cuando_iso (ISO 8601 hora local, "
                               "ej. '2026-09-23T15:00:00') a partir de la fecha actual: Jarvis avisa solo a esa hora. Si no, "
                               "deja cuando_iso vacío.",
         {"texto": s(), "cuando": s("Tal como lo dijo, ej. 'mañana'"), "cuando_iso": s()}, ["texto"]),
    tool("focus_window", "Trae al frente una ventana o pestaña abierta cuyo título contenga el texto.",
         {"title_contains": s()}, ["title_contains"]),
    tool("list_windows", "Títulos de las ventanas y pestañas abiertas."),
    tool("type_text", "Escribe texto donde está el foco (o en 'ventana', enfocándola antes). enviar=true presiona Enter: "
                      "úsalo sin preguntar solo si el riesgo es bajo (buscar, navegar). Si le llega a otra persona "
                      "(mensaje, email, publicación), deja enviar=false y pregunta antes, salvo que ya te lo haya pedido.",
         {"texto": s(), "ventana": s(), "enviar": {"type": "boolean"}}, ["texto"]),
    tool("list_files", "Lista una carpeta: 'descargas', 'escritorio', 'documentos' o una ruta.", {"carpeta": s()}, ["carpeta"]),
    tool("read_file", "Lee un archivo de texto.", {"ruta": s()}, ["ruta"]),
    tool("buscar_archivo", "Busca archivos por nombre en Descargas, Escritorio y Documentos (o en 'carpeta').",
         {"nombre": s(), "carpeta": s()}, ["nombre"]),
    tool("leer_portapapeles", "Devuelve el texto copiado en el portapapeles."),
    tool("copiar_portapapeles", "Copia texto al portapapeles.", {"texto": s()}, ["texto"]),
    tool("info_sistema", "Estado de la PC: CPU, RAM, disco y batería."),
    tool("leer_pagina", "Lee el texto completo de una página web.", {"url": s()}, ["url"]),
    tool("mover_archivo", "Mueve un archivo a otra carpeta (nunca sobrescribe).",
         {"origen": s(), "destino_carpeta": s()}, ["origen", "destino_carpeta"]),
    tool("borrar_archivo", "Manda un archivo o carpeta a la Papelera de Reciclaje (siempre recuperable).",
         {"ruta": s()}, ["ruta"]),
    tool("calcular", "Calcula matemática exacta (úsalo siempre en vez de estimar): + - * / // % ** y sqrt, sin, cos, "
                     "tan, log, log10, exp, abs, round, min, max, sum, len, pi, e.",
         {"expresion": s()}, ["expresion"]),
    tool("registrar_dispositivo", "Registra otro dispositivo tuyo con Jarvis (dirección de Tailscale) bajo un nombre.",
         {"nombre": s(), "host": s()}, ["nombre", "host"]),
    tool("gestionar_dispositivo", "Le manda un comando en lenguaje natural a un dispositivo registrado y devuelve su respuesta.",
         {"nombre": s(), "comando": s()}, ["nombre", "comando"]),
]

if __name__ == "__main__":
    with open(os.path.join(ROOT, "res", "tools.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(TOOLS, f, ensure_ascii=False, separators=(",", ":"))
    with open(os.path.join(ROOT, "res", "system_prompt.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(SYSTEM_PROMPT)
    print(len(TOOLS), "herramientas,", len(json.dumps(TOOLS, ensure_ascii=False, separators=(",", ":"))),
          "caracteres; prompt", len(SYSTEM_PROMPT), "caracteres")
