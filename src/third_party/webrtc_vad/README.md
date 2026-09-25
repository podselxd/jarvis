# Detector de voz de WebRTC

El VAD (detector de actividad de voz) de WebRTC: dice si un pedazo de 10, 20
o 30 ms de audio es voz. Sokari lo usa para saber cuándo empiezas y cuándo
terminas de hablar (`src/vad.c`).

- Origen: la carpeta `cbits/webrtc` del paquete `webrtcvad` 2.0.10 de PyPI
  (el código C de WebRTC que ese paquete compila; no se incluye su parte de
  Python).
- Licencia: BSD de los autores de WebRTC (ver `LICENSE`). Los encabezados de
  los archivos mencionan además la concesión de patentes del proyecto WebRTC
  (su archivo PATENTS).
- Único cambio: en `spl_init.c`, la inicialización única en Windows usa
  `InitOnceExecuteOnce` en lugar de una `CRITICAL_SECTION` armada a mano.
