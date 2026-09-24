// Oído y voz del celular: el reconocimiento de voz y la lectura en voz alta
// que ya trae Android. Van detrás de interfaces para poder probar la app sin
// micrófono.
import 'package:flutter_tts/flutter_tts.dart';
import 'package:speech_to_text/speech_recognition_error.dart';
import 'package:speech_to_text/speech_to_text.dart';

abstract class Oido {
  /// Pide el micrófono y prepara el reconocimiento. false si no hay.
  Future<bool> preparar();

  /// Escucha hasta que te calles. [alOir] recibe el texto parcial y, al final,
  /// el definitivo con esFinal = true. [alTerminar] avisa si dejó de escuchar
  /// sin texto definitivo.
  Future<void> escuchar({
    required void Function(String texto, bool esFinal) alOir,
    required void Function(String error) alFallar,
    required void Function() alTerminar,
  });

  Future<void> parar();
}

abstract class Voz {
  Future<void> decir(String texto);
  Future<void> callar();
}

/// Qué decirle a la persona según el error del reconocedor de Android.
String mensajeErrorOido(String codigo) {
  switch (codigo) {
    case 'error_no_match':
    case 'error_speech_timeout':
      return 'No te oí. Toca el botón y vuelve a intentar.';
    case 'error_permission':
    case 'error_insufficient_permissions':
      return 'Sokari necesita permiso para usar el micrófono (Ajustes del celular → Apps → Sokari → Permisos).';
    case 'error_network':
    case 'error_network_timeout':
    case 'error_server':
    case 'error_server_disconnected':
      return 'El reconocimiento de voz necesita internet en el celular.';
    case 'error_busy':
    case 'error_recognizer_busy':
      return 'El reconocimiento de voz está ocupado; intenta otra vez.';
    case 'no_disponible':
      return 'Este celular no tiene reconocimiento de voz. Instala o activa la app de Google.';
    default:
      return 'No pude usar el reconocimiento de voz ($codigo).';
  }
}

class OidoAndroid implements Oido {
  final _stt = SpeechToText();
  bool _listo = false;
  bool _dioFinal = false;
  void Function(String)? _alFallar;
  void Function()? _alTerminar;

  @override
  Future<bool> preparar() async {
    if (_listo) return true;
    _listo = await _stt.initialize(onError: _error, onStatus: _estado);
    return _listo;
  }

  void _error(SpeechRecognitionError e) {
    final f = _alFallar;
    _alFallar = null;
    _alTerminar = null;
    f?.call(e.errorMsg);
  }

  void _estado(String estado) {
    if (estado == SpeechToText.doneStatus && !_dioFinal) {
      final t = _alTerminar;
      _alTerminar = null;
      _alFallar = null;
      t?.call();
    }
  }

  @override
  Future<void> escuchar({
    required void Function(String texto, bool esFinal) alOir,
    required void Function(String error) alFallar,
    required void Function() alTerminar,
  }) async {
    _dioFinal = false;
    _alFallar = alFallar;
    _alTerminar = alTerminar;
    await _stt.listen(
      onResult: (r) {
        if (r.finalResult) {
          _dioFinal = true;
          _alFallar = null;
          _alTerminar = null;
        }
        alOir(r.recognizedWords, r.finalResult);
      },
      listenOptions: SpeechListenOptions(
        localeId: 'es_MX',
        listenFor: const Duration(seconds: 30),
        pauseFor: const Duration(seconds: 3),
        partialResults: true,
        cancelOnError: true,
      ),
    );
  }

  @override
  Future<void> parar() => _stt.stop();
}

class VozAndroid implements Voz {
  final _tts = FlutterTts();
  bool _lista = false;

  @override
  Future<void> decir(String texto) async {
    if (!_lista) {
      await _tts.setLanguage('es-MX');
      await _tts.awaitSpeakCompletion(true);
      _lista = true;
    }
    await _tts.speak(texto);
  }

  @override
  Future<void> callar() async {
    await _tts.stop();
  }
}
