# Sokari

Asistente de voz personal para Windows. Le dices **"Hey Sokari"** y te contesta con voz. Puede abrir apps, buscar en internet, controlar tu música y tus ventanas, manejar archivos, recordarte cosas y hablar con tus otras PCs.

Es **un solo `Sokari.exe`** de unos 3 MB, escrito en C. No necesita Python ni instaladores, y no usa archivos `.bat` ni DLLs extra.

## Instalación

1. Baja `Sokari.exe` de la [última versión](https://github.com/podselxd/sokari/releases/latest) y déjalo en la carpeta que quieras.
2. Ábrelo. La primera vez te pide tu API key de Groq, que es gratis y no pide tarjeta: sácala en [console.groq.com/keys](https://console.groq.com/keys). Tu nombre y lo demás son opcionales.
3. Listo. Aparece la esfera y un ícono en la bandeja, junto al reloj.

Cuando lo abres a mano después de la primera vez, sale la **ventana de Inicio** (abajo). Si lo pusiste a iniciar con Windows, al prender la PC arranca directo, sin esa ventana.

Se actualiza solo. Revisa GitHub al arrancar y cada 6 horas, y solo instala la versión nueva cuando no le estás hablando. Antes de reemplazarse comprueba la huella SHA-256 del archivo.

### Si tenías la versión anterior

Baja `Sokari.exe` y ábrelo; la versión anterior no se actualiza sola a esta.
- Si la anterior está abierta, Sokari te pregunta si la cierra. Nunca corren las dos a la vez.
- La primera vez se trae tu configuración y tu memoria a sus carpetas nuevas. No borra nada: las carpetas anteriores se quedan como respaldo, y puedes borrarlas cuando veas que todo está bien.
- Si tenías el inicio con Windows prendido, ahora arranca Sokari.

## Uso

- Di **"Hey Sokari"** y habla. Puedes decirlo todo de corrido ("Hey Sokari, abre Spotify") o hacer una pausa y esperar el tono. Deja de escuchar cuando te callas.
- **Ctrl+Alt+J** sirve para hablarle sin decir "Hey Sokari".
- **"Hey Sokari" todavía se está entrenando** (ver [Entrenar "Hey Sokari"](#entrenar-hey-sokari)). Mientras un exe no traiga el modelo, se le habla solo con **Ctrl+Alt+J** y así lo dicen sus mensajes.
- Si le hablas mientras está hablando, se calla y te escucha.
- Para cerrar la conversación dile "adiós" o "eso es todo". Si configuraste una palabra de apagado y la dices, Sokari se cierra al instante.

Menú del ícono de la bandeja (clic derecho): **Hablar con Sokari**, **Ocultar/Mostrar la esfera**, **Modo de pantalla**, **Silenciar micrófono**, **Ventana de inicio…**, **⚙ Configuración…**, **Abrir carpeta de Sokari** y **Salir**.

### Ventana de Inicio

Sale al abrir `Sokari.exe` a mano, o desde **Ventana de inicio…** en la bandeja. Tiene:

- **Iniciar Sokari** (o **Mostrar la esfera**, si ya está corriendo). Si la cierras sin iniciar, Sokari se cierra.
- **Modo de pantalla** y **Salida de audio** (bocinas o audífonos). Los cambios se aplican al momento.
- **Configuración**, **Silenciar micrófono**, **Probar audio**, **Buscar actualizaciones**, **Abrir carpeta de datos** y **Salir**.

"Probar audio" hace sonar el tono de Sokari por la salida elegida y, si Sokari ya está iniciado, también su voz.

### Configuración

| Sección | Qué tiene |
|---|---|
| Inicio | Iniciar, modo de pantalla, salida de audio y botones rápidos (ver arriba) |
| Cuenta | API key de Groq, tu nombre, contraseña de tu perfil y palabra de apagado |
| Pantalla | Modo de pantalla, resolución de la esfera, estilo (halo de puntos o líneas) y subtítulos |
| Voz y audio | Volumen, voz de Windows (Raúl de México por defecto), micrófono, salida de audio y sensibilidad de "Hey Sokari" |
| General | Iniciar con Windows, conectar tus PCs con Tailscale, sonido de activación, Obsidian y buscar actualizaciones |

Modos de pantalla:

- **Pantalla completa:** siempre encima de todo. Se aparta sola cuando Sokari abre algo.
- **Pantalla completa sin bordes:** ocupa la pantalla, pero tus ventanas pueden ir encima. Pasa al frente cuando le hablas.
- **Esfera flotante:** una esfera transparente que puedes arrastrar a donde quieras.
- **Ventana:** una ventana normal que puedes mover, agrandar o minimizar. **F11** (o doble clic) la pone en pantalla completa y **F11** o **Esc** la regresan. La X la oculta, pero Sokari sigue escuchando.
- **Minimizado:** igual que Ventana, pero arranca minimizada en la barra de tareas y no se asoma cuando le hablas.

F11 solo funciona en Ventana y Minimizado, cuando la ventana tiene el foco. En los otros modos la esfera nunca toma el teclado, para que las teclas que manda Sokari lleguen a tu app. En esos modos cambias de modo desde la bandeja o la ventana de Inicio.

La salida de audio elegida vale para la voz de Sokari y su tono. Si pusiste un sonido de activación propio (MP3 o WAV), ese sale por la salida predeterminada de Windows.

## Qué puede hacer

- Platicar y responder preguntas. Usa los modelos gratis de Groq y rota entre GPT-OSS 120B, Qwen 3 y GPT-OSS 20B para no quedarse sin cupo.
- Abrir apps (incluidas las del menú Inicio, como Discord, Steam o Spotify), carpetas, archivos y páginas. No abre programas ni scripts sueltos (.exe, .bat, accesos directos).
- Buscar en internet y leer páginas completas.
- Controlar el volumen (también a un nivel exacto) y la música: pausa, siguiente y anterior.
- Mostrar el escritorio, cambiar de ventana, minimizar todo, bloquear la PC, poner un video en pantalla completa y cerrar la pestaña.
- Ver qué ventanas tienes abiertas y traer una al frente.
- Escribir texto donde está el cursor. Presiona Enter por su cuenta solo en cosas de bajo riesgo, como una búsqueda; si el texto le llega a otra persona, te pregunta antes (lo decide el modelo, con la regla de confirmación de abajo como respaldo). Nunca escribe en terminales.
- Leer y copiar al portapapeles.
- Listar, leer, buscar y mover archivos, sin sobrescribir nada. Si borra algo, siempre va a la Papelera. No toca rutas de red ni las carpetas donde Sokari guarda su configuración y su memoria.
- Ver CPU, RAM, disco y batería.
- Hacer cálculos exactos con su propia calculadora, sin acceso a nada más.
- Poner recordatorios con hora (te avisa solo cuando llega el momento) o para la próxima vez que le hables.
- Recordar datos para siempre, con perfiles por persona que puedes proteger con contraseña, y exportarlos a Obsidian.
- Crear comandos propios que junten varias acciones ("crea un comando que abra X y ponga música").
- Mandarle órdenes a tus otras PCs por Tailscale ("dile a mi laptop que…"). Solo registra direcciones de Tailscale.

## Privacidad y seguridad

- La detección de "Hey Sokari" corre en tu PC y no sale nada hasta que la oye. Después, tu voz va a Groq para pasarla a texto y el texto va al modelo de Groq.
- La voz de Sokari se genera en tu PC con las voces de Windows. Las búsquedas van a DuckDuckGo, o a Bing si DuckDuckGo falla.
- Lo que le pidas leer (un archivo, el portapapeles o el título de una ventana) viaja a Groq como parte de la conversación. Tenlo en cuenta si es algo delicado.
- Sokari no puede ejecutar comandos libres ni hacer clic en cualquier parte: solo tiene un set cerrado de acciones. No abre programas ni scripts sueltos y nunca escribe en terminales.
- Una página, un archivo, el portapapeles o el título de una pestaña pueden traer instrucciones escondidas para el modelo. Por eso, mientras algo así siga en la conversación, Sokari te pide un "sí" de voz antes de enviar texto, abrir un archivo, mover o borrar, crear o ejecutar comandos propios y usar la red entre tus PCs. La pregunta la arma Sokari, no el modelo, así que escuchas lo que va a hacer de verdad.
- No lee páginas de tu red local (router, otras PCs, localhost), ni siquiera si una página pública redirige ahí.
- La palabra de apagado se revisa en tu PC. Nunca se le manda al modelo ni se guarda en la memoria, y las herramientas de archivos no pueden leer la carpeta donde está guardada.
- El servidor para tus otras PCs escucha solo en tu IP de Tailscale, nunca en internet, y pide un secreto que se genera solo. Sokari solo manda ese secreto a direcciones de Tailscale.

## Dónde guarda las cosas

- `%LOCALAPPDATA%\Sokari\`: configuración (`config.env`), registro (`sokari.log`), sonidos y dispositivos.
- `Escritorio\Sokari\`: tu memoria (datos, conversación reciente, perfiles, recordatorios y comandos propios).
- Las carpetas de la versión anterior, si la tenías: el respaldo de lo de antes. Sokari ya no las usa y, como tienen copia de tu API key y tu memoria, sus herramientas de archivos no las pueden leer.

Para usar sonidos propios, elige tu sonido de activación en Configuración → General. También puedes poner un `busqueda.mp3` en `%LOCALAPPDATA%\Sokari\sounds\`, que suena mientras busca.

## Compilar desde el código

Necesitas Windows, [Git Bash](https://git-scm.com) y MinGW-w64 de WinLibs:

```bash
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Luego, desde Git Bash, en la carpeta del repo:

```bash
mingw32-make
```

Eso genera `dist/Sokari.exe`. Si ese Sokari está abierto, Windows no deja reemplazarlo; en ese caso compila en otro lado:

```bash
mingw32-make OUT=build/Sokari.exe
```

`mingw32-make tests` compila las pruebas en `build/tests/`.

En cada push, GitHub Actions compila en un Windows real (una advertencia del compilador cuenta como error) y corre las pruebas, menos `test_groq`, que necesita una API key. El `Sokari.exe` de cada corrida queda en la pestaña **Actions** para probar una rama sin compilarla.

### Publicar una versión

1. Sube la versión en `src/config.h` (`SOKARI_VERSION` y `SOKARI_VERSION_W`), en `res/sokari.rc` (las cuatro) y en `res/sokari.manifest`. `sh tests/check_version.sh` revisa que coincidan.
2. Con eso ya en `master`: `git tag v2.2.0 && git push origin v2.2.0`.
3. Actions compila, prueba, revisa que el tag coincida con el código y deja un **borrador** de release con `Sokari.exe`.
4. Revisa el borrador y publícalo. Hasta que lo publiques, nadie se actualiza.

No crees el release a mano desde GitHub: te saltas las pruebas, y si el tag no coincide con la versión del código, el exe se vuelve a descargar cada 6 horas.

Lo que va dentro del exe está en `res/`:

- `tools.json`: las herramientas que Sokari puede usar.
- `system_prompt.txt`: sus instrucciones.
- `wakeword.bin`: la parte común del detector de la palabra (convierte el audio en rasgos; es igual para cualquier palabra).
- `hey_sokari.jww`: el clasificador de "Hey Sokari". Solo entra al exe si el archivo existe.
- El ícono.

Si editas `tools.json` o `system_prompt.txt`, el siguiente `mingw32-make` los mete al exe.

### Cómo está organizado

| Archivo | Qué hace |
|---|---|
| `main.c` | Arranque, una sola instancia y actualización |
| `voice.c` | Ciclo de voz: escuchar, grabar, hablar e interrupciones |
| `wakeword.c`, `nn_*.c` | Detector de "Hey Sokari" (red neuronal con AVX2 si tu CPU lo tiene) |
| `compat_jarvis.c` | Paso desde la versión anterior (carpetas, configuración, inicio con Windows). Temporal |
| `groq.c`, `http.c` | Groq (Whisper y chat) con rotación de modelos y control de cupo |
| `agent.c`, `tools*.c`, `calc.c` | Conversación y herramientas |
| `tts.c`, `audio.c`, `sounds.c` | Voces de Windows, micrófono, bocinas y tonos |
| `sphere.c`, `ui_main.c` | Esfera animada, modos de pantalla y subtítulos |
| `ui_settings.c`, `tray.c` | Ventana de Inicio y de configuración, e ícono de la bandeja |
| `memory.c`, `config.c`, `mesh.c`, `update.c` | Memoria, configuración, otras PCs y actualizaciones |

## Desde el celular

La app de Android de `movil/` te deja hablarle a tu Sokari de la PC desde el celular, en casa o fuera, por Tailscale. Instalación y límites en [`movil/README.md`](movil/README.md).

## Entrenar "Hey Sokari"

El clasificador de "Hey Sokari" se entrena una vez con voces sintéticas (muchas voces distintas, para que responda a cualquiera) y queda como `res/hey_sokari.jww`. Los pasos y el cuaderno de Google Colab están en `herramientas/`.

## Créditos

- [cJSON](https://github.com/DaveGamble/cJSON) (licencia MIT), en `src/third_party/`.
- El detector de la palabra es un puerto a C de [openWakeWord](https://github.com/dscripka/openWakeWord) (código Apache 2.0).
- Sus modelos (la parte común, `wakeword.bin`) y el clasificador de "Hey Sokari" que se entrena encima (`hey_sokari.jww`) están bajo [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/): uso no comercial y con crédito, y quien los modifique tiene que compartirlos igual. Por eso Sokari es gratis y no comercial. Detalles en `herramientas/LEEME.md`.
