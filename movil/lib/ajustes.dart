// Ajustes de la app. Todo se guarda cifrado con la llave del sistema
// (Android Keystore): el secreto de malla da acceso a tu PC.
import 'package:flutter_secure_storage/flutter_secure_storage.dart';

class Ajustes {
  const Ajustes({
    this.direccion = '',
    this.secreto = '',
    this.escucharAlAbrir = true,
    this.leerRespuestas = true,
  });

  /// IP 100.x.y.z o nombre .ts.net de tu PC.
  final String direccion;

  /// El mismo secreto de malla que tiene Sokari en tu PC.
  final String secreto;

  /// Empezar a escuchar en cuanto se abre la app.
  final bool escucharAlAbrir;

  /// Leer en voz alta lo que contesta Sokari.
  final bool leerRespuestas;

  bool get completos => direccion.trim().isNotEmpty && secreto.trim().isNotEmpty;

  Ajustes copiar({String? direccion, String? secreto, bool? escucharAlAbrir, bool? leerRespuestas}) => Ajustes(
        direccion: direccion ?? this.direccion,
        secreto: secreto ?? this.secreto,
        escucharAlAbrir: escucharAlAbrir ?? this.escucharAlAbrir,
        leerRespuestas: leerRespuestas ?? this.leerRespuestas,
      );
}

abstract class AlmacenAjustes {
  Future<Ajustes> cargar();
  Future<void> guardar(Ajustes a);
}

class AlmacenSeguro implements AlmacenAjustes {
  static const _s = FlutterSecureStorage();

  @override
  Future<Ajustes> cargar() async {
    final v = await _s.readAll();
    return Ajustes(
      direccion: v['direccion'] ?? '',
      secreto: v['secreto'] ?? '',
      escucharAlAbrir: v['escuchar_al_abrir'] != 'no',
      leerRespuestas: v['leer_respuestas'] != 'no',
    );
  }

  @override
  Future<void> guardar(Ajustes a) async {
    await _s.write(key: 'direccion', value: a.direccion.trim());
    await _s.write(key: 'secreto', value: a.secreto.trim());
    await _s.write(key: 'escuchar_al_abrir', value: a.escucharAlAbrir ? 'si' : 'no');
    await _s.write(key: 'leer_respuestas', value: a.leerRespuestas ? 'si' : 'no');
  }
}
