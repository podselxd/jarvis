# Jarvis

Asistente de voz personal para Windows. Le dices **"Hey Jarvis"** y te contesta con voz. Puede abrir apps, buscar en internet, controlar tu música y tus ventanas, manejar archivos, recordarte cosas y hablar con tus otras PCs.

Es **un solo `Jarvis.exe`** de unos 4 MB, escrito en C. No necesita Python ni instaladores, y no usa archivos `.bat` ni DLLs extra.

## Instalación

1. Baja `Jarvis.exe` de la [última versión](https://github.com/podselxd/jarvis/releases/latest) y déjalo en la carpeta que quieras.
2. Ábrelo. La primera vez te pide tu API key de Groq, que es gratis y no pide tarjeta: sácala en [console.groq.com/keys](https://console.groq.com/keys). Tu nombre y lo demás son opcionales.
3. Listo. Aparece la esfera y un ícono en la bandeja, junto al reloj.

Se actualiza solo. Revisa GitHub al arrancar y cada 6 horas, y solo instala la versión nueva cuando no le estás hablando. Antes de reemplazarse comprueba la huella SHA-256 del archivo.

Si vienes de la versión en Python, se actualiza sola a esta. Si la bajas a mano, ponla en la misma carpeta que el `Jarvis.exe` viejo. La primera vez que la abras se trae tu `.env`, tus sonidos, tus dispositivos y el inicio con Windows. Tu memoria se queda donde estaba.

## Uso

- Di **"Hey Jarvis"**, espera el tono y habla. Deja de escuchar cuando te callas.
- **Ctrl+Alt+J** sirve para hablarle sin decir "Hey Jarvis".
- Si le hablas mientras está hablando, se calla y te escucha.
- Para cerrar la conversación dile "adiós" o "eso es todo". Si configuraste una palabra de apagado y la dices, Jarvis se cierra al instante.

Menú del ícono de la bandeja (clic derecho): **Hablar con Jarvis**, **Ocultar/Mostrar la esfera**, **Silenciar micrófono**, **⚙ Configuración…**, **Abrir carpeta de Jarvis** y **Salir**.

### Configuración

| Sección | Qué tiene |
|---|---|
| Cuenta | API key de Groq, tu nombre, contraseña de tu perfil y palabra de apagado |
| Pantalla | Modo de pantalla, resolución de la esfera, estilo (halo de puntos o líneas) y subtítulos |
| Voz y audio | Volumen, voz de Windows (Raúl de México por defecto), micrófono y sensibilidad de "Hey Jarvis" |
| General | Iniciar con Windows, conectar tus PCs con Tailscale, sonido de activación, Obsidian y buscar actualizaciones |

Modos de pantalla:

- **Pantalla completa:** siempre encima de todo. Se aparta sola cuando Jarvis abre algo.
- **Pantalla completa sin bordes:** ocupa la pantalla, pero tus ventanas pueden ir encima. Pasa al frente cuando le hablas.
- **Ventana sin bordes:** una esfera flotante y transparente que puedes arrastrar a donde quieras.

## Qué puede hacer

- Platicar y responder preguntas. Usa los modelos gratis de Groq y rota entre GPT-OSS 120B, Qwen 3 y GPT-OSS 20B para no quedarse sin cupo.
- Abrir apps (incluidas las del menú Inicio, como Discord, Steam o Spotify), carpetas, archivos y páginas. No abre programas ni scripts sueltos (.exe, .bat, accesos directos).
- Buscar en internet y leer páginas completas.
- Controlar el volumen (también a un nivel exacto) y la música: pausa, siguiente y anterior.
- Mostrar el escritorio, cambiar de ventana, minimizar todo, bloquear la PC, poner un video en pantalla completa y cerrar la pestaña.
- Ver qué ventanas tienes abiertas y traer una al frente.
- Escribir texto donde está el cursor. Presiona Enter por su cuenta solo en cosas de bajo riesgo, como una búsqueda; si el texto le llega a otra persona, te pregunta antes (lo decide el modelo, con la regla de confirmación de abajo como respaldo). Nunca escribe en terminales.
- Leer y copiar al portapapeles.
- Listar, leer, buscar y mover archivos, sin sobrescribir nada. Si borra algo, siempre va a la Papelera. No toca rutas de red ni las carpetas donde Jarvis guarda su configuración y su memoria.
- Ver CPU, RAM, disco y batería.
- Hacer cálculos exactos con su propia calculadora, sin acceso a nada más.
- Poner recordatorios con hora (te avisa solo cuando llega el momento) o para la próxima vez que le hables.
- Recordar datos para siempre, con perfiles por persona que puedes proteger con contraseña, y exportarlos a Obsidian.
- Crear comandos propios que junten varias acciones ("crea un comando que abra X y ponga música").
- Mandarle órdenes a tus otras PCs por Tailscale ("dile a mi laptop que…"). Solo registra direcciones de Tailscale.

## Privacidad y seguridad

- La detección de "Hey Jarvis" corre en tu PC y no sale nada hasta que la oye. Después, tu voz va a Groq para pasarla a texto y el texto va al modelo de Groq.
- La voz de Jarvis se genera en tu PC con las voces de Windows. Las búsquedas van a DuckDuckGo, o a Bing si DuckDuckGo falla.
- Lo que le pidas leer (un archivo, el portapapeles o el título de una ventana) viaja a Groq como parte de la conversación. Tenlo en cuenta si es algo delicado.
- Jarvis no puede ejecutar comandos libres ni hacer clic en cualquier parte: solo tiene un set cerrado de acciones. No abre programas ni scripts sueltos y nunca escribe en terminales.
- Una página, un archivo, el portapapeles o el título de una pestaña pueden traer instrucciones escondidas para el modelo. Por eso, mientras algo así siga en la conversación, Jarvis te pide un "sí" de voz antes de enviar texto, abrir un archivo, mover o borrar, crear o ejecutar comandos propios y usar la red entre tus PCs. La pregunta la arma Jarvis, no el modelo, así que escuchas lo que va a hacer de verdad.
- No lee páginas de tu red local (router, otras PCs, localhost), ni siquiera si una página pública redirige ahí.
- La palabra de apagado se revisa en tu PC. Nunca se le manda al modelo ni se guarda en la memoria, y las herramientas de archivos no pueden leer la carpeta donde está guardada.
- El servidor para tus otras PCs escucha solo en tu IP de Tailscale, nunca en internet, y pide un secreto que se genera solo. Jarvis solo manda ese secreto a direcciones de Tailscale.

## Dónde guarda las cosas

- `%LOCALAPPDATA%\Jarvis\`: configuración (`config.env`), registro (`jarvis.log`), sonidos y dispositivos.
- `Escritorio\Jarvis\`: tu memoria (datos, conversación reciente, perfiles, recordatorios y comandos propios).

Para usar sonidos propios, elige tu sonido de activación en Configuración → General. También puedes poner un `busqueda.mp3` en `%LOCALAPPDATA%\Jarvis\sounds\`, que suena mientras busca.

## Compilar desde el código

Necesitas Windows, [Git Bash](https://git-scm.com) y MinGW-w64 de WinLibs:

```bash
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Luego, desde Git Bash, en la carpeta del repo:

```bash
mingw32-make
```

Eso genera `dist/Jarvis.exe`. Si ese Jarvis está abierto, Windows no deja reemplazarlo; en ese caso compila en otro lado:

```bash
mingw32-make OUT=build/Jarvis.exe
```

`mingw32-make tests` compila las pruebas en `build/tests/`.

Lo que va dentro del exe está en `res/`:

- `tools.json`: las herramientas que Jarvis puede usar.
- `system_prompt.txt`: sus instrucciones.
- `wakeword.bin`: el modelo de "Hey Jarvis".
- El ícono.

Si editas `tools.json` o `system_prompt.txt`, el siguiente `mingw32-make` los mete al exe.

### Cómo está organizado

| Archivo | Qué hace |
|---|---|
| `main.c` | Arranque, una sola instancia y actualización |
| `voice.c` | Ciclo de voz: escuchar, grabar, hablar e interrupciones |
| `wakeword.c`, `nn_*.c` | Detector de "Hey Jarvis" (red neuronal con AVX2 si tu CPU lo tiene) |
| `groq.c`, `http.c` | Groq (Whisper y chat) con rotación de modelos y control de cupo |
| `agent.c`, `tools*.c`, `calc.c` | Conversación y herramientas |
| `tts.c`, `audio.c`, `sounds.c` | Voces de Windows, micrófono, bocinas y tonos |
| `sphere.c`, `ui_main.c` | Esfera animada, modos de pantalla y subtítulos |
| `ui_settings.c`, `tray.c` | Ventana de configuración e ícono de la bandeja |
| `memory.c`, `config.c`, `mesh.c`, `update.c` | Memoria, configuración, otras PCs y actualizaciones |

## Créditos

- [cJSON](https://github.com/DaveGamble/cJSON) (licencia MIT), en `src/third_party/`.
- El modelo de "Hey Jarvis" es de [openWakeWord](https://github.com/dscripka/openWakeWord) y usa la licencia [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/): Jarvis es para uso personal, no comercial.
