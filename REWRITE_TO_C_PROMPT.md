# Prompt: reescribir Jarvis de Python a C — un solo .exe nativo

Pega esto como primer mensaje en una sesión nueva (de Claude Code o cualquier
otro asistente de código) para arrancar la reescritura. Está escrito para
poder copiarse tal cual.

---

## Tarea

Reescribe por completo el asistente de voz "Jarvis" (hoy en Python, repo
`podselxd/jarvis` en GitHub) en C, para producir **un único `.exe` nativo,
estático, sin Python, sin PyInstaller, sin ningún `.bat` ni archivo suelto
alrededor**. Todo lo que hoy son módulos separados (`jarvis.py`, `ui.py`,
`sphere.py`, `instalador.py`, `settings.py`, `tray.py`, `update.py`,
`setup.bat`, `build_exe.bat`) se convierte en un solo binario compilado.

### Por qué se pide esto

La versión en Python funciona, pero tiene problemas de fondo que vienen del
stack, no de un bug puntual: PyInstaller empaqueta un runtime de Python
completo (el `.exe` pesa ~100MB y arrastra dependencias que ni se usan
directamente — scipy, sklearn, hooks de tensorflow — porque algo en la
cadena de imports las jala), un bug real de DPI-awareness rompió el
centrado de la ventana con el escalado de pantalla de Windows, el render de
la esfera fue lento (~20fps) hasta optimizarlo a mano, y en general el
usuario final quiere abrir un `.exe` y que sea SOLO eso — nada de instalar
Python, nada de `pip install`, nada de un `.bat` corriendo `git pull`.

### Antes de arrancar: la parte honesta

Esto no es una tarea chica. Varias piezas de la versión Python no tienen
equivalente directo en C:

- **edge-tts (la voz de Jarvis) no es una API REST simple**: es un protocolo
  por WebSocket contra el servicio interno de síntesis de voz de Microsoft
  Edge, sin librería oficial en C. Hay que reimplementar ese protocolo a
  mano (con un cliente WebSocket como libwebsockets) o, como alternativa más
  realista, usar las voces neuronales de Windows (SAPI / `System.Speech`,
  disponibles nativamente en Windows 11) — más simples de integrar en C
  pero con voces distintas (hay que decidir cuál se prefiere, no asumirlo).
- **openWakeWord** usa ONNX Runtime, que sí tiene [API oficial en C](https://onnxruntime.ai/docs/api/c/),
  pero el pipeline de features (melspectrograma + embeddings antes del
  modelo de wake word) hoy lo arma la librería de Python — en C hay que
  reproducir ese preprocesamiento a mano con la misma matemática exacta, o
  el modelo no va a detectar nada.
- Todo lo que hoy es "un tool de 5 líneas en Python que llama a una librería"
  (búsqueda web con `ddgs`, parseo de HTML con BeautifulSoup, recorte de
  imágenes con Pillow) se vuelve bastante más código en C.

Esto es realista como proyecto de varias semanas, no de una tarde. Si en
algún punto el costo no se siente justificado frente a simplemente arreglar
bugs puntuales de la versión Python (que ya está funcionando), vale la pena
decirlo y frenar ahí en vez de dejar una reescritura a medias.

## Requisito no negociable: un solo ejecutable

- Todo el código compila a **un `.exe` estático** (o con el mínimo de DLLs
  del sistema operativo — nada de tener que instalar un runtime aparte).
- Recursos (ícono, lo que haga falta) van **embebidos como recursos de
  Windows** (`.rc` + `windres`/`rc.exe`), no como archivos sueltos junto al
  exe.
- Los modelos ONNX de wake word (melspectrograma, embeddings, el modelo en
  sí) son binarios grandes — está bien que se bajen una sola vez a
  `%LOCALAPPDATA%\Jarvis\models\` en el primer arranque (como hace hoy la
  versión Python), no hace falta embeberlos en el exe.
- Config y memoria (`.env`-equivalente, historial, perfiles, recordatorios)
  siguen viviendo AFUERA del exe, en `%LOCALAPPDATA%` / `Desktop\Jarvis`
  como hoy — el exe en sí no se auto-modifica salvo para el auto-update.
- Nada de `.bat`. Ni para instalar, ni para compilar (usa CMake o un
  Makefile, pero el usuario final nunca corre nada de eso — solo descarga
  y ejecuta `Jarvis.exe`).
- Auto-actualización: revisa GitHub Releases al arrancar, baja el `.exe`
  nuevo, y se reemplaza a sí mismo — Windows no deja que un proceso se
  sobreescriba en caliente, así que hace falta algún mecanismo de
  relanzamiento (ej. un pequeño helper que se extrae a un temporal,
  reemplaza el exe y lo vuelve a abrir), pero eso es un detalle interno de
  implementación: para el usuario sigue siendo "un solo exe", nunca un
  archivo que tenga que ver o tocar.

## Funcionalidad que hay que preservar exactamente

Fuente de verdad para el comportamiento exacto: el repo actual en
`https://github.com/podselxd/jarvis` (rama `master`). No asumas strings,
prompts o umbrales — cópialos del código real.

### Pipeline de voz
- Activación por palabra "Hey Jarvis" (siempre escuchando, no push-to-talk).
- STT: Groq API, modelo `whisper-large-v3-turbo`.
- Razonamiento/chat: Groq API, modelo `openai/gpt-oss-120b`, con tool
  calling (formato OpenAI-compatible de Groq).
- TTS: voz `es-MX-JorgeNeural` (o el equivalente disponible si se cambia a
  SAPI, ver arriba).
- El audio de Jarvis se puede interrumpir hablando fuerte por encima
  (umbral: energía RMS del frame > `SILENCIO_RMS * 4.5`).
- Calibración de silencio ambiente al arrancar, se recalibra si cambia la
  lógica de cálculo (versión de calibración guardada en el archivo).

### Palabra de apagado local
- Se define una vez, al configurar el perfil.
- Se revisa por coincidencia de texto en el resultado del STT, **en local,
  antes de mandarle nada a Groq** — el modelo de IA nunca la recibe ni la
  conoce. Esto es una propiedad de seguridad intencional, no un detalle de
  implementación: no lo cambies a "que la IA decida apagarse".

### Las ~27 herramientas (tools) que puede usar el agente
`open_app`, `web_search`, `control_media`, `control_desktop`,
`create_macro`, `run_macro`, `guardar_dato`, `recordar`, `identificarse`,
`proteger_perfil`, `exportar_a_obsidian`, `crear_recordatorio`,
`focus_window`, `list_windows`, `type_text`, `list_files`, `read_file`,
`buscar_archivo`, `leer_portapapeles`, `copiar_portapapeles`,
`info_sistema`, `leer_pagina`, `mover_archivo`, `borrar_archivo`,
`calcular`, `registrar_dispositivo`, `gestionar_dispositivo`.

Cada una tiene límites de seguridad deliberados que hay que mantener en la
reescritura, no relajar "porque ahora es más fácil en C":
- **Nada de shell libre ni `system()` con strings armados por concatenación**
  — cada acción es un enum fijo de operaciones seguras, no comandos
  arbitrarios. Al pasar a C esto importa el doble: usar `CreateProcess` con
  un array de argumentos, nunca construir un string de shell y pasarlo a
  `system()`.
- `type_text` escribe texto pero **nunca presiona Enter** salvo que la
  propia lógica del agente lo marque explícitamente como seguro (búsquedas,
  navegación) vía un flag — nunca por defecto.
- `borrar_archivo` **siempre** manda a la Papelera de Reciclaje
  (`IFileOperation` con `FOF_ALLOWUNDO` es el equivalente nativo de
  `send2trash`), nunca borra en definitiva.
- `calcular` es un sandbox: nada de `eval`/interpretar código arbitrario.
  Hoy es un intérprete de AST hecho a mano con tope de exponente (1000) para
  evitar un DoS tipo `9**9**9`. En C, un parser de expresiones matemáticas
  desde cero, con el mismo tope.
- `buscar_archivo` tiene un tope de archivos escaneados (20000) para que una
  carpeta enorme sin resultados no cuelgue el proceso.
- Un error dentro de CUALQUIER tool nunca debe poder tirar el proceso
  entero — en Python hoy es un `try/except` alrededor del dispatch; en C,
  asegurar que ningún tool pueda corromper memoria o crashear el proceso
  (esto es más peligroso en C que en Python — ver la sección de seguridad
  más abajo).

### Memoria
- Contexto rodante de 12 horas que se manda automático al modelo (no todo
  el historial de siempre, para no gastar contexto/tokens de más).
- Historial permanente en disco, buscable bajo pedido explícito del
  usuario ("busca en lo que hablamos de...").
- Perfiles por persona (`perfiles.json` o equivalente), con contraseña
  opcional por perfil (hash, no texto plano) — el reseteo de contraseñas
  solo se puede hacer localmente desde la configuración, nunca por voz.

### Multi-dispositivo (Tailscale)
- Servidor HTTP que **solo escucha en la IP de Tailscale del equipo**,
  nunca en `0.0.0.0` ni en la red pública.
- Autenticado con un secreto random generado una sola vez
  (`JARVIS_MESH_SECRET`, 32 bytes aleatorios), nunca tipeado por el
  usuario.
- Acceso a estado compartido protegido correctamente para concurrencia (en
  Python es un `threading.Lock`; en C, un mutex real) — un comando por voz
  y un comando por la malla no pueden pisarse.
- La palabra de apagado tiene que funcionar igual si llega por la malla
  (desde otro hilo/conexión) que si llega por voz local.

### Esfera animada (la interfaz visual)

Es un render procedural de una esfera "wireframe" — el algoritmo exacto
(ya portado una vez de canvas/JS a Python, en `sphere.py` del repo) es:

- Malla de meridianos: `MERIDIAN_COUNT=140` líneas, `POINTS_PER_MERIDIAN=60`
  puntos cada una, coordenadas esféricas base `(sin θ cos φ, cos θ, sin θ
  sin φ)`.
- Ondulación (`ripple`) que deforma el radio por punto:
  `wave = ripple * (sin(4φ + 0.6t)·sin(2θ + 0.3t) + 0.5·sin(11φ − 0.9t)·sin(5θ + 0.5t))`.
- Rotación continua en Y a `rotationSpeed` rad/s + un "wobble" adicional
  `sin(0.12t)·0.22` en otro eje.
- Proyección en perspectiva simple, color por "intensity bucket" (12 pasos)
  mezclando `colorLow` → `colorHigh` según fresnel + bulge + profundidad,
  con blending aditivo (más brillante donde se cruzan más líneas) y un
  glow/blur suave encima.
- Dos estados: **idle** (`colorLow=#4550e6, colorHigh=#ff2bd1,
  rotationSpeed=0.15, rippleAmount=0.24, glowBlur=8`) y **hablando**
  (`colorLow=#5b3df0, colorHigh=#ff47e0, rotationSpeed=0.32,
  rippleAmount=0.4, glowBlur=11`), con una transición que desliza los
  parámetros de uno a otro (no un corte).
- Corre en tiempo real (no es un GIF pre-grabado — la rotación nunca
  cierra en un loop corto exacto). En C, esto es el lugar perfecto para
  usar de verdad la GPU: Direct2D o Direct3D en vez de rasterizar a mano
  con CPU como hace la versión Python — sería más rápido y más simple que
  optimizar dibujo de líneas por CPU.

### Interfaz nueva a agregar (no existía en Python hasta esta reescritura)

- **Ícono en la bandeja del sistema** (`Shell_NotifyIcon`, API nativa de
  Win32) con menú: Mostrar/ocultar la esfera, Configuración, Salir. Esta es
  la forma "normal" de apagar Jarvis — no depender de cerrar una consola.
- **Ventana de configuración** (nativa — un diálogo de Win32 normal, o una
  librería liviana tipo Dear ImGui si se prefiere algo más moderno) con:
  - Modo de pantalla, como opción excluyente: **Pantalla completa** (API
    nativa de fullscreen de Windows) / **Ventana sin bordes** (ventana
    chica, centrada, sin marco, no tapa el resto de la pantalla) /
    **Pantalla completa sin bordes** (cubre toda la pantalla, sin marco —
    el modo por defecto de hoy).
  - Volumen (0-100%) — en C, controlar el volumen de la sesión de audio de
    este proceso específicamente vía WASAPI/`ISimpleAudioVolume`, no un
    hack de MCI como hace la versión Python hoy (que ni siquiera está
    garantizado que funcione en todos los sistemas).
  - API key de Groq.
  - Nombre de usuario registrado.
  - Palabra de apagado.
- Todos los cambios de esta ventana aplican en caliente cuando tiene
  sentido (modo de pantalla, volumen) y persisten en el archivo de config
  para el próximo arranque.

## Seguridad: cosas que Python te regalaba gratis y en C no

Esto es lo más importante de toda esta reescritura y lo más fácil de
pasar por alto. Python, por ser un lenguaje memory-safe, evita de raíz
toda una clase de bugs que en C hay que prevenir a mano:

- **Nunca** usar `strcpy`, `sprintf`, `gets` ni construir buffers de
  tamaño fijo para datos que vienen de la red (respuestas de Groq), del
  micrófono, o de archivos del usuario — usar siempre variantes con
  límite de tamaño (`strncpy_s`, `snprintf`) y validar longitudes antes de
  copiar.
- Cualquier dato que llegue de una respuesta HTTP (JSON de Groq, HTML de
  una búsqueda) es **no confiable** — parsear con una librería robusta
  (cJSON, no un parser hecho a mano), nunca asumir un tamaño o formato.
- Los argumentos que arman los tools (rutas de archivo, nombres de
  aplicación) vienen del modelo de IA, que a su vez los arma a partir de
  lo que el usuario dijo por voz — tratarlos igual de no confiables que
  input de red: nada de pasarlos directo a `CreateProcess`/`ShellExecute`
  sin validar, nada de construir paths con concatenación de strings sin
  chequear `..`/rutas fuera de lo esperado.
- Nada de comandos de shell armados con f-strings/concatenación — si hace
  falta ejecutar algo, usar la API de Win32 directamente (`CreateProcess`
  con array de argumentos) en vez de `system("cmd /c " + texto)`.

## Stack sugerido (punto de partida, no una decisión cerrada)

- HTTP/TLS: libcurl.
- JSON: cJSON.
- Inferencia ONNX (wake word): ONNX Runtime, API oficial en C.
- Audio: WASAPI (captura y reproducción) — mejor que MCI en todo sentido,
  incluyendo el control de volumen por proceso.
- Render: Direct2D (o Direct3D si se quiere más control) para la esfera.
- UI nativa: Win32 puro (`Shell_NotifyIcon` para la bandeja, diálogos
  nativos o Dear ImGui para configuración).
- Build: CMake, apuntando a MSVC (o MinGW-w64 si se prefiere no depender
  de Visual Studio) — un solo `.exe` de salida, linkeo estático de todo lo
  que se pueda.

## Qué NO hacer

- No relajar ningún límite de seguridad "porque en C es más difícil
  implementarlo igual" — si algo es más difícil de portar de forma segura,
  se discute explícitamente, no se omite en silencio.
- No inventar funcionalidad nueva más allá de lo que está en este
  documento — el objetivo es portar lo que ya existe y funciona, más la
  bandeja/configuración descritas arriba, no rediseñar el producto.
- No asumir strings/prompts/umbrales de memoria: copiarlos del repo real.
