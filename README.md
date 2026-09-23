# Jarvis

Asistente de voz personal para Windows. Se activa diciendo **"Hey Jarvis"**, corre en la nube gratis (Groq) y puede abrir apps, buscar en internet, controlar volumen/música/escritorio, crear sus propios comandos, y recordar cosas entre conversaciones.

## Instalación (1 clic)

1. Instala [Python](https://python.org/downloads) si no lo tienes (marca "Add to PATH" al instalar) y [Git](https://git-scm.com/downloads) si no lo tienes.
2. Cloná este repo (`git clone https://github.com/podselxd/jarvis`) — mejor que bajar el ZIP, porque así `setup.bat` puede auto-actualizarse solo más adelante — y hacé doble clic en **`setup.bat`**.
3. La primera vez te va a pedir: una API key gratis de [console.groq.com](https://console.groq.com) (sin tarjeta), tu nombre (opcional, para que te reconozca desde el arranque), y una palabra de apagado (opcional, ver Seguridad abajo).
4. Te pregunta si quieres conectarlo a tus otros dispositivos (Tailscale) y si quieres que inicie con Windows.
5. Listo, arranca.

### Actualizar

Si clonaste con git, volvé a correr **`setup.bat`** cuando quieras — antes de instalar nada, baja solo los cambios nuevos del repo (`git pull`) y sigue de ahí. Tu `.env`, memoria y configuración personal nunca se tocan.

## Uso

Di "Hey Jarvis", espera el sonido de activación, y habla. Se calla solo cuando dejas de hablar, o decile "adiós"/"listo, gracias" para cortar antes.

Para apagarlo del todo: doble clic en **`detener.bat`** (o la palabra de apagado que configuraste, si la configuraste).

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
- Conectar tus propios dispositivos entre sí vía Tailscale y mandarles comandos ("decile a mi escritorio que...")

## Notas

- **Sonidos**: pon tus propios `activacion.mp3` y `busqueda.mp3` en la carpeta `sounds/` (no vienen incluidos).
- **Privacidad**: el audio y el texto pasan por Groq (voz y modelo), Microsoft Edge (voz de salida) y motores de búsqueda públicos. No hay nada corriendo local salvo la detección de "Hey Jarvis". Si le pedís que lea un archivo, un título de ventana o el portapapeles, ese contenido también viaja a Groq como parte de la conversación — mismo nivel de confianza que todo lo demás, pero tenlo presente si es algo sensible.
- **Seguridad**: a propósito Jarvis no puede ejecutar comandos de shell libres ni hacer clicks en cualquier parte de la pantalla — solo un set acotado de acciones seguras. Sí puede escribir texto (para dictar mensajes, notas, etc.), pero nunca presiona Enter ni envía nada por su cuenta: el texto queda escrito para que lo revises y decidas si lo mandas, así un audio mal entendido nunca termina enviando algo solo.
- **Palabra de apagado**: opcional, se configura en `setup.bat`. Es un freno que el modelo de IA nunca conoce — se revisa en el texto transcripto antes de mandarle nada a Groq, así no depende de que la IA "decida" respetarlo.
- **Multi-dispositivo**: si instalás Tailscale, Jarvis levanta un servidor que escucha *solo* en tu IP de Tailscale (nunca en internet público) protegido además por un secreto propio (`JARVIS_MESH_SECRET` en `.env`, generado solo, no lo inventas vos) — pensado para tus propios dispositivos, no para exponerlo a terceros.
- **Memoria**: se guarda en `Desktop/Jarvis/` (fuera del repo, no se sube a GitHub).
