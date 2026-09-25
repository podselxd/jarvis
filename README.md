# Sokari

Asistente de voz personal para Windows. Le dices **"Hey Sokari"** y te contesta con voz. Puede abrir apps, buscar en internet, controlar tu música y tus ventanas, manejar archivos, recordarte cosas y hablar con tus otras PCs.

Es **un solo `Sokari.exe`** de unos 3 MB, escrito en C. No necesita Python ni instaladores, y no usa archivos `.bat` ni DLLs extra.

## Instalación

1. Baja `Sokari.exe` de la [última versión](https://github.com/podselxd/sokari/releases/latest) y déjalo en la carpeta que quieras.
2. Ábrelo. La primera vez te pide tu API key de Groq, que es gratis y no pide tarjeta: sácala en [console.groq.com/keys](https://console.groq.com/keys). Tu nombre y lo demás son opcionales.
3. Listo. Sokari dice "en línea" y queda un ícono en la bandeja, junto al reloj. La esfera aparece cuando le hablas.

Cuando lo abres a mano después de la primera vez, sale la **ventana de Inicio** (abajo). Si lo pusiste a iniciar con Windows, al prender la PC arranca directo, sin esa ventana.

Se actualiza solo. Revisa GitHub al arrancar y cada 6 horas, y solo instala la versión nueva cuando no le estás hablando. Antes de reemplazarse comprueba la huella SHA-256 del archivo.

## Uso

- Di **"Hey Sokari"** y habla. Puedes decirlo todo de corrido ("Hey Sokari, abre Spotify") o hacer una pausa y esperar el tono. Deja de escuchar cuando te callas.
- **Ctrl+Alt+J** sirve para hablarle sin decir "Hey Sokari".
- **"Hey Sokari" es beta.** Se entrenó solo con voces sintéticas. En pruebas con voces que nunca oyó:
  - se activó por error 0.35 veces por hora de audio real;
  - detectó el 94 % de las voces en inglés y ~50–60 % de una voz mexicana sintética.
  
  Con tu voz todavía no está medido. Si no te oye, usa **Ctrl+Alt+J**; lo que lo arregla es reentrenarlo con grabaciones de tu voz. Subir la sensibilidad ayuda poco. Detalles en [Entrenar "Hey Sokari"](#entrenar-hey-sokari).
- Después de cada respuesta te sigue escuchando unos segundos, sin que repitas "Hey Sokari".
- Si dices **"Hey Sokari"** (o Ctrl+Alt+J) mientras está hablando, se calla y te escucha. Otros ruidos ya no lo interrumpen.
- Para cerrar la conversación dile "adiós", "ya vete" o "eso es todo". También termina cuando Sokari se despide o si dejas de hablarle. Si configuraste una palabra de apagado y la dices, Sokari se cierra al instante.
- Con **Aparecer solo cuando le hablas** (Configuración → Pantalla, prendida de fábrica), la esfera aparece en tu modo de pantalla al hablarle y se esconde al terminar. Al abrir Sokari se ve y se queda hasta tu primera conversación.

Menú del ícono de la bandeja (clic derecho): **Hablar con Sokari**, **Ocultar/Mostrar la esfera**, **Modo de pantalla**, **Silenciar micrófono**, **Acceso completo (menos borrar)**, **Ventana de inicio…**, **⚙ Configuración…**, **Abrir carpeta de Sokari** y **Salir**.

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
| Pantalla | Modo de pantalla, resolución de la esfera, estilo (halo de puntos o líneas), subtítulos y "Aparecer solo cuando le hablas" |
| Voz y audio | Volumen, voz de Windows (Raúl de México por defecto, con botón **Probar**), micrófono, salida de audio y sensibilidad de "Hey Sokari" |
| General | Iniciar con Windows, sonido de activación, Obsidian y **Acceso completo (menos borrar)** |
| Dispositivos | Tailscale, tus otras PCs (Detectar, Probar, Quitar), permiso en el firewall, Revisar la malla y el secreto para cuentas distintas. Ver [Tus otras PCs](#tus-otras-pcs) |

Modos de pantalla:

- **Pantalla completa** (la de fábrica): siempre encima de todo. Se aparta sola cuando Sokari abre algo.
- **Pantalla completa sin bordes:** ocupa la pantalla, pero tus ventanas pueden ir encima. Pasa al frente cuando le hablas.
- **Esfera flotante:** una esfera transparente que puedes arrastrar a donde quieras.
- **Ventana:** una ventana normal que puedes mover, agrandar o minimizar. **F11** (o doble clic) la pone en pantalla completa y **F11** o **Esc** la regresan. La X la oculta, pero Sokari sigue escuchando.
- **Minimizado:** igual que Ventana, pero arranca minimizada en la barra de tareas y no se asoma cuando le hablas.

F11 solo funciona en Ventana y Minimizado, cuando la ventana tiene el foco. En los otros modos la esfera nunca toma el teclado, para que las teclas que manda Sokari lleguen a tu app. En esos modos cambias de modo desde la bandeja o la ventana de Inicio.

La salida de audio elegida vale para la voz de Sokari y su tono. Si pusiste un sonido de activación propio (MP3 o WAV), ese sale por la salida predeterminada de Windows.

### Tus otras PCs

Para decirle desde una PC "dile a mi laptop que abra Spotify":

1. Instala Tailscale en cada PC (Configuración → Dispositivos → **Instalar Tailscale**) y entra **con la misma cuenta** en todas. Con la misma cuenta, tus PCs se reconocen solas: no hace falta copiar el secreto.
2. En cada PC dale **Permitir en el firewall** (pide permiso de administrador y abre el puerto solo para tu red de Tailscale).
3. Dale **Detectar mis PCs**: agrega tus otras PCs con Windows que estén en tu Tailscale. También puedes decirle a Sokari "registra mi laptop en 100.x.y.z".
4. Dale **Probar** a cada una. Te dice qué falta:
   - "no te reconoce": las PCs están en cuentas distintas de Tailscale. Entra con la misma, o dale **Copiar secreto** en una y pégalo en ese campo en la otra.
   - "Sokari no le contesta": ábrelo en esa PC.
   - "no contesta": que esté prendida, con Tailscale conectado y sin otra VPN, y con el paso 2 hecho en **esa** PC.

Si algo no funciona, dale **Revisar la malla**. Revisa paso a paso Tailscale, tu cuenta, si esta PC recibe órdenes, el firewall, qué dispositivos ve tu red y cada PC registrada, y marca con ✗ lo que falla. El reporte se copia solo, para que lo pegues donde pidas ayuda.

Sokari empieza a recibir órdenes solo en cuanto Tailscale se conecta, sin reiniciarlo. La PC que recibe una orden avisa con una notificación, y la respuesta se oye en la PC donde hablaste.

## Qué puede hacer

- **Comandos al instante, sin IA:** "ponle play", "pausa", "la siguiente", "sube el volumen", "volumen al 30", "minimiza la pestaña", "maximízala", "cierra la pestaña", "abre archivos", "abre mis descargas", "abre Opera" y "gracias" se hacen en tu PC, aunque vengan con saludo ("¿cómo andas? oye, ponle play"). Son inmediatos, no gastan cupo de Groq y no dependen de que el modelo entienda. Lo que trae algo más ("pon la canción de AC/DC") va al modelo.
- **Nunca "listo" sin hacerlo:** si el modelo dice que ya hizo algo ("te pongo play", "abrí Opera") sin haber usado ninguna herramienta, Sokari se lo reclama una vez. Si insiste, en lugar de repetirte el "listo" te dice que no lo hizo.
- Platicar y responder preguntas. Usa los modelos gratis de Groq y rota entre GPT-OSS 120B, Qwen 3 y GPT-OSS 20B para no quedarse sin cupo. Cada pedido lleva solo lo necesario: las herramientas de todos los días y las demás (archivos, portapapeles, perfiles, Obsidian, calculadora, tus otras PCs…) cuando las mencionas, con los últimos 12 mensajes de la conversación. Si el modelo contesta "no puedo" y le faltaba alguna, se le repite con todas. El log anota cuántos tokens gasta cada respuesta.
- Abrir apps (incluidas las del menú Inicio, como Discord, Steam, Spotify u Opera; si ya está abierta, la trae al frente), el Explorador, tu navegador predeterminado, carpetas, archivos y páginas. No abre programas ni scripts sueltos (.exe, .bat, accesos directos).
- Buscar en internet y leer páginas completas.
- Controlar el volumen (también a un nivel exacto) y la música: pausa, siguiente y anterior.
- Minimizar, maximizar o cerrar la ventana de enfrente (la tuya, nunca la de Sokari), mostrar el escritorio, cambiar de ventana, minimizar todo, bloquear la PC, poner un video en pantalla completa y cerrar la pestaña.
- Ver qué ventanas tienes abiertas y traer una al frente.
- Escribir texto donde está el cursor y darle Enter. No ve la pantalla: te dice en qué ventana escribió, pero no puede saber si se envió. Nunca escribe en terminales.
- Leer y copiar al portapapeles.
- Listar, leer, buscar y mover archivos, sin sobrescribir nada. Si borra algo, siempre va a la Papelera. No toca rutas de red ni las carpetas donde Sokari guarda su configuración y su memoria.
- Ver CPU, RAM, disco y batería.
- Hacer cálculos exactos con su propia calculadora, sin acceso a nada más.
- Poner recordatorios con hora (te avisa solo cuando llega el momento) o para la próxima vez que le hables.
- Recordar datos para siempre, con perfiles por persona que puedes proteger con contraseña, y exportarlos a Obsidian.
- Crear comandos propios que junten varias acciones ("crea un comando que abra X y ponga música").
- Mandarle órdenes a tus otras PCs por Tailscale ("dile a mi laptop que…"). Solo registra direcciones de Tailscale. Ver [Tus otras PCs](#tus-otras-pcs).

## Privacidad y seguridad

- La detección de "Hey Sokari" corre en tu PC y no sale nada hasta que la oye. Después, tu voz va a Groq para pasarla a texto y el texto va al modelo de Groq.
- La voz de Sokari se genera en tu PC con las voces de Windows. Las búsquedas van a DuckDuckGo, o a Bing si DuckDuckGo falla.
- Lo que le pidas leer (un archivo, el portapapeles o el título de una ventana) viaja a Groq como parte de la conversación. Tenlo en cuenta si es algo delicado.
- Sokari no puede ejecutar comandos libres ni hacer clic en cualquier parte: solo tiene un set cerrado de acciones. No abre programas ni scripts sueltos y nunca escribe en terminales.
- **Acceso completo (menos borrar)** viene prendido: Sokari hace todo sin preguntarte (mover archivos, mandar mensajes, subir archivos, guardar datos, exportar a Obsidian) y solo pide un "sí" de voz antes de **borrar**. Lo apagas en Configuración → General, en Inicio, en el menú del ícono o diciéndole "pregúntame antes"; "tienes permiso para todo" lo vuelve a prender.
  - El riesgo: una página, un archivo, el portapapeles o el título de una pestaña pueden traer instrucciones escondidas para el modelo. Con acceso completo, las podría seguir sin avisarte.
  - Prenderlo cuando ya leyó algo de afuera pide tu "sí": una página no puede dárselo sola.
- Con el acceso completo apagado, mientras algo de afuera siga en la conversación, Sokari te pide un "sí" de voz antes de enviar texto, abrir un archivo, mover o borrar, crear o ejecutar comandos propios y usar la red entre tus PCs. La pregunta la arma Sokari, no el modelo, así que escuchas lo que va a hacer de verdad.
  - Cuenta solo la conversación actual: al volver a decir "Hey Sokari", lo que leyó antes se borra.
  - "Sí a todo" hace que no vuelva a preguntar en esa conversación. "¿Qué?" repite la pregunta.
- No lee páginas de tu red local (router, otras PCs, localhost), ni siquiera si una página pública redirige ahí.
- La palabra de apagado se revisa en tu PC. Nunca se le manda al modelo ni se guarda en la memoria, y las herramientas de archivos no pueden leer la carpeta donde está guardada.
- El servidor para tus otras PCs escucha solo en tu IP de Tailscale, nunca en internet. Solo acepta órdenes de dispositivos de tu misma cuenta de Tailscale (Tailscale comprueba con criptografía quién manda cada paquete) o que traigan el secreto de malla, que se genera solo. Sokari solo manda ese secreto a direcciones de Tailscale.

## Dónde guarda las cosas

- `%LOCALAPPDATA%\Sokari\`: configuración (`config.env`), registro (`sokari.log`), sonidos y dispositivos.
- `Escritorio\Sokari\`: tu memoria (datos, conversación reciente, perfiles, recordatorios y comandos propios).
- Las carpetas de la versión anterior, si la tenías: el respaldo de lo de antes. Sokari ya no las usa y puedes borrarlas; mientras existan, como tienen copia de tu API key y tu memoria, sus herramientas de archivos no las pueden leer.

Para usar sonidos propios, elige tu sonido de activación en Configuración → General. Suena completo (hasta 10 s) mientras Sokari ya te escucha: con audífonos no hay problema; con bocinas, mejor uno corto, porque el micrófono lo puede oír. También puedes poner un `busqueda.mp3` en `%LOCALAPPDATA%\Sokari\sounds\`, que suena mientras busca.

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
2. Con eso ya en `master`, crea el release de una de estas dos formas:
   - **En GitHub:** *Actions → Borrador de release → Run workflow*. Escribe la versión (`v2.3.0`) y marca **publicar** si quieres que salga de una vez; si no, queda como borrador.
   - **Desde tu PC:** `git tag v2.3.0 && git push origin v2.3.0`. Queda como borrador.
3. Actions:
   - compila y prueba el exe y la app;
   - revisa que la versión coincida con el código;
   - crea el release con `Sokari.exe` y `Sokari.apk` (la app del celular, con la misma versión).
   
   El APK necesita los secretos de su llave de firma (ver [`movil/README.md`](movil/README.md)). Sin ellos, el release sale solo con `Sokari.exe` y lo avisa en sus notas.
4. Si quedó como borrador, revísalo y publícalo. Hasta que lo publiques, nadie se actualiza.

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
| `groq.c`, `http.c` | Groq (Whisper y chat) con rotación de modelos y control de cupo |
| `agent.c`, `tools*.c`, `calc.c` | Conversación y herramientas |
| `tts.c`, `audio.c`, `sounds.c` | Voces de Windows, micrófono, bocinas y tonos |
| `sphere.c`, `ui_main.c` | Esfera animada, modos de pantalla y subtítulos |
| `ui_settings.c`, `tray.c` | Ventana de Inicio y de configuración, e ícono de la bandeja |
| `memory.c`, `config.c`, `mesh.c`, `update.c` | Memoria, configuración, otras PCs y actualizaciones |

## Desde el celular

La app de Android de `movil/` te deja hablarle a tu Sokari de la PC desde el celular, en casa o fuera, por Tailscale. Instalación y límites en [`movil/README.md`](movil/README.md).

## Entrenar "Hey Sokari"

El clasificador de "Hey Sokari" se entrena con voces sintéticas (muchas voces distintas, para que responda a cualquiera) y queda como `res/hey_sokari.jww`.

- El de la versión 2.3.0 se entrenó en una PC sin GPU con los scripts de `herramientas/entreno_local/`.
- Con qué datos, sus números completos y qué hace la sensibilidad están en `herramientas/LEEME.md`, junto con el cuaderno de Google Colab para reentrenarlo con GPU.

## Créditos

- [cJSON](https://github.com/DaveGamble/cJSON) (licencia MIT), en `src/third_party/`.
- El detector de la palabra es un puerto a C de [openWakeWord](https://github.com/dscripka/openWakeWord) (código Apache 2.0).
- Sus modelos (la parte común, `wakeword.bin`) y el clasificador de "Hey Sokari" que se entrena encima (`hey_sokari.jww`) están bajo [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/): uso no comercial y con crédito, y quien los modifique tiene que compartirlos igual. Por eso Sokari es gratis y no comercial. Detalles en `herramientas/LEEME.md`.
- "Hey Sokari" se entrenó con:
  - FLEURS y Speech Commands de Google (CC BY 4.0);
  - ESC-50 de Karol J. Piczak (CC BY-NC 3.0);
  - voces sintéticas de piper-sample-generator (LibriTTS-R, CC BY 4.0), Piper, MBROLA (uso no comercial) y espeak-ng.
