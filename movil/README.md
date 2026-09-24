# Sokari en el celular

Una app de Android para hablarle a **tu Sokari de la PC** desde el celular, en casa o fuera. Tocas el botón, hablas, y tu PC hace el trabajo: abre apps, pone música, busca, te recuerda cosas. La respuesta se lee en voz alta en el celular.

El celular no ejecuta nada por su cuenta:

1. Pasa tu voz a texto con el reconocimiento de voz de Android.
2. Se lo manda a tu PC por [Tailscale](https://tailscale.com).
3. Te lee lo que contesta.

## Qué necesitas

- Sokari abierto en tu PC (versión 2.2.0 o más nueva).
- Tailscale en la PC y en el celular, con la misma cuenta.
- El secreto de malla de tu PC: en Sokari, *Configuración → General*, campo "Tus otros dispositivos (Tailscale) — secreto de malla".
  - Sokari lo crea solo la primera vez que arranca con Tailscale conectado.
  - Tiene 64 caracteres: pásatelo al celular copiado, o cámbialo por uno tuyo (el mismo en todas tus PCs).
  - Ahí mismo sale la IP de Tailscale de tu PC (100.x.y.z).

## Instalar

1. En GitHub, pestaña **Actions** → corrida más reciente de "App del celular" → baja el artefacto **Sokari-apk**.
2. Descomprímelo y abre `app-release.apk` en el celular. Android te va a pedir permiso para instalar apps de esa fuente.
3. Abre **Sokari**, toca *Configurar* y pon:
   - la IP de tu PC (o su nombre `.ts.net`);
   - el secreto de malla.
4. Dale **Probar conexión** y luego **Guardar**.

La prueba de conexión no gasta tu cupo de Groq: solo revisa que tu PC conteste y que el secreto coincida.

## Uso

- Al abrirla empieza a escuchar; se puede apagar en Configuración. Toca el botón para hablar, otra vez para cortar, y mientras lee la respuesta, para callarla.
- También puedes escribirle.
- Puedes elegirla como asistente del celular (*Ajustes → Apps → Apps predeterminadas → Asistente digital*, según tu celular). Así se abre manteniendo presionado el botón de inicio o de encendido.

## Si no conecta

| Mensaje | Qué hacer |
|---|---|
| "No encuentro tu PC" | Revisa que la PC esté prendida, con Sokari abierto, y Tailscale conectado en los dos. Si conectaste Tailscale después de abrir Sokari, reinícialo: su servidor para el celular arranca al abrirse. |
| "El secreto no coincide" | Copia otra vez el secreto de *Configuración → General* en la PC. |
| "Sokari está ocupado" | Estaba contestándote a ti o a otra PC; prueba en unos segundos. |

## Límites y privacidad

- **Lo delicado no se puede confirmar desde el celular.** Mover o borrar archivos pide un "sí" de voz frente a la PC.
- **Si la PC está apagada, no hay Sokari.**
- **Tu voz** la pasa a texto el reconocimiento de voz de Android, normalmente el de Google. Según el celular puede hacerse en el propio celular o en sus servidores.
- **El texto** va de tu celular a tu PC por Tailscale, cifrado. Tu PC lo manda a Groq, como cuando le hablas directo.
- **Dónde se conecta:** solo a direcciones de Tailscale (100.64.0.0/10 o nombres `.ts.net`, y solo si resuelven a una IP de Tailscale). Nunca manda el secreto a otro lado.
- **Dónde guarda tus datos:** la dirección y el secreto se guardan cifrados con la llave del sistema (Android Keystore).
- **Sin "Hey Sokari" en el celular.** Oír todo el tiempo gasta batería, y Android obliga a una notificación fija.

## Para desarrollar

Flutter 3.47.5:

```
cd movil
flutter pub get
flutter analyze
flutter test
flutter build apk --release
```

| Archivo | Qué hace |
|---|---|
| `lib/malla.dart` | Habla con la PC: valida que la dirección sea de Tailscale, manda el pedido y traduce los errores |
| `lib/voz.dart` | Reconocimiento de voz y lectura en voz alta de Android |
| `lib/ajustes.dart` | Dirección, secreto y preferencias, guardados cifrados |
| `lib/main.dart`, `lib/pantalla_ajustes.dart` | Las dos pantallas |
| `test/` | Pruebas del cliente (con una PC de mentira) y de las pantallas (con micrófono y voz de mentira) |

El ícono sale de `assets/`: `dart run flutter_launcher_icons`.
