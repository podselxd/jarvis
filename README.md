# Jarvis

Asistente de voz personal para Windows. Se activa diciendo **"Hey Jarvis"**, corre en la nube gratis (Groq) y puede abrir apps, buscar en internet, controlar volumen/música/escritorio, crear sus propios comandos, y recordar cosas entre conversaciones.

## Instalación

### Opción 1: .exe (más simple)

1. Baja `Jarvis.exe` desde la [última versión](https://github.com/podselxd/jarvis/releases/latest).
2. Haz doble clic. La primera vez se abre una ventana pidiendo tu API key de Groq (gratis, sin tarjeta, en [console.groq.com](https://console.groq.com)), tu nombre y una palabra de apagado, todo opcional salvo la key.
3. Listo, arranca — sin consola, sin instalar Python.

### Opción 2: código fuente (para modificarlo)

1. Instala [Python](https://python.org/downloads) si no lo tienes (marca "Add to PATH" al instalar).
2. Descarga este repo — botón verde **Code → Download ZIP**, o clonándolo con git si prefieres — y descomprímelo.
3. Haz doble clic en **`setup.bat`**. Instala dependencias y abre la misma ventana de configuración del .exe.

### Actualizar

Con el .exe: baja la última versión de los [releases](https://github.com/podselxd/jarvis/releases) y reemplaza el archivo — tu `.env`/memoria no se tocan porque viven aparte, en `%LOCALAPPDATA%`/`Desktop/Jarvis`.

Con el código fuente: vuelve a correr **`setup.bat`** cuando quieras — antes de instalar nada, baja solo los cambios nuevos (con git si clonaste así, o bajando el último ZIP de GitHub si no) y sigue de ahí. Excepción: `setup.bat` no se actualiza a sí mismo por seguridad (un .bat modificándose mientras corre puede romperse) — si algún día cambia, hay que bajarlo a mano una vez.

### Compilar tu propio .exe

Con el código fuente descargado, corre **`build_exe.bat`** — genera `dist\Jarvis.exe` con PyInstaller (tarda varios minutos la primera vez).

## Uso

Di "Hey Jarvis", espera el sonido de activación, y habla. Se calla solo cuando dejas de hablar, o dile "adiós"/"listo, gracias" para cortar antes.

Para apagarlo del todo: cierra la ventana (o el proceso desde el Administrador de tareas si corre en segundo plano), o la palabra de apagado que hayas configurado.

## Qué puede hacer

- Charlar y responder preguntas (GPT-OSS-120B vía Groq)
- Abrir apps, carpetas y archivos
- Buscar en internet
- Controlar volumen, música y ventanas del escritorio
- Encontrar y enfocar una ventana/pestaña ya abierta por título, y ver qué tienes abierto
- Escribir texto donde esté el foco — decide solo cuándo es seguro enviarlo (buscar, navegar) y cuándo preguntar primero (mensajes a otras personas)
- Crear comandos propios que combinan varias acciones ("crea un comando que abra X e Y")
- Recordar datos entre conversaciones (12 horas de contexto automático + memoria permanente buscable)
- Leer archivos y carpetas (Descargas/Escritorio/Documentos), buscar archivos por nombre
- Leer y escribir el portapapeles
- Ver el estado de la PC: batería, CPU, RAM, espacio en disco
- Recordatorios con hora exacta: avisa solo, sin que nadie le hable primero, apenas llega el momento
- Leer el contenido completo de una página web, no solo el fragmento de una búsqueda
- Mover archivos para organizar, y "borrar" (siempre a la Papelera de Reciclaje, nunca para siempre)
- Calcular matemática de verdad (no estimada) en un sandbox aislado, sin acceso a archivos ni red
- Conectar tus propios dispositivos entre sí vía Tailscale y mandarles comandos ("dile a mi escritorio que...")
- Una ventana visual (esfera animada) que cambia de color mientras habla

## Notas

- **Sonidos**: pon tus propios `activacion.mp3` y `busqueda.mp3` en la carpeta `sounds/` (no vienen incluidos).
- **Privacidad**: el audio y el texto pasan por Groq (voz y modelo), Microsoft Edge (voz de salida) y motores de búsqueda públicos. No hay nada corriendo local salvo la detección de "Hey Jarvis". Si le pides que lea un archivo, un título de ventana o el portapapeles, ese contenido también viaja a Groq como parte de la conversación — mismo nivel de confianza que todo lo demás, pero tenlo presente si es algo sensible.
- **Seguridad**: a propósito Jarvis no puede ejecutar comandos de shell libres ni hacer clicks en cualquier parte de la pantalla — solo un set acotado de acciones seguras. Sí puede escribir texto (para dictar mensajes, notas, etc.), pero nunca presiona Enter ni envía nada por su cuenta: el texto queda escrito para que lo revises y decidas si lo mandas, así un audio mal entendido nunca termina enviando algo solo.
- **Palabra de apagado**: opcional, se configura al arrancar por primera vez. Es un freno que el modelo de IA nunca conoce — se revisa en el texto transcrito antes de mandarle nada a Groq, así no depende de que la IA "decida" respetarlo.
- **Multi-dispositivo**: si instalas Tailscale, Jarvis levanta un servidor que escucha *solo* en tu IP de Tailscale (nunca en internet público) protegido además por un secreto propio (`JARVIS_MESH_SECRET`, generado solo, no lo inventas tú) — pensado para tus propios dispositivos, no para exponerlo a terceros.
- **Memoria**: se guarda en `Desktop/Jarvis/` (fuera del repo, no se sube a GitHub).
