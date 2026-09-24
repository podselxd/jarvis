// Cliente de la malla de Sokari: le manda a tu PC lo que dijiste y trae su
// respuesta. Habla con el mismo servidor que usan tus otras PCs (src/mesh.c):
// POST /comando en el puerto 8765, solo por Tailscale y con el secreto de
// malla en cada pedido.
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:http/http.dart' as http;

/// Puerto en el que Sokari escucha a tus otros dispositivos.
const puertoMalla = 8765;

/// IPs de Tailscale: 100.64.0.0/10.
bool esIpTailscale(List<int> b) => b.length == 4 && b[0] == 100 && b[1] >= 64 && b[1] <= 127;

final _ipv4 = RegExp(r'^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$');
final _nombre = RegExp(r'^[A-Za-z0-9.-]+$');

/// Solo direcciones de Tailscale, con las mismas reglas que la PC
/// (mesh_host_allowed): una IP 100.64.0.0/10 o un nombre MagicDNS *.ts.net.
/// Así el secreto, que viaja en cada pedido, nunca sale hacia otra dirección.
bool direccionPermitida(String host) {
  final ip = _ipv4.firstMatch(host);
  if (ip != null) {
    final b = [for (var i = 1; i <= 4; i++) int.parse(ip.group(i)!)];
    return b.every((v) => v <= 255) && esIpTailscale(b);
  }
  if (host.length <= 7 || host.length > 253 || !_nombre.hasMatch(host)) return false;
  return !host.startsWith('.') &&
      !host.startsWith('-') &&
      !host.contains('..') &&
      host.toLowerCase().endsWith('.ts.net');
}

/// Resuelve un nombre a sus IPv4 (inyectable para las pruebas).
typedef Resolver = Future<List<InternetAddress>> Function(String host);

Future<List<InternetAddress>> resolverDelSistema(String host) =>
    InternetAddress.lookup(host, type: InternetAddressType.IPv4);

/// Un problema al hablar con la PC, con un mensaje para mostrar tal cual.
class ErrorMalla implements Exception {
  const ErrorMalla(this.mensaje);

  final String mensaje;

  @override
  String toString() => mensaje;
}

const _noLlego = 'No encuentro tu PC. Revisa que esté prendida, con Sokari abierto, y que Tailscale '
    'esté conectado en los dos.';

class ClienteMalla {
  ClienteMalla({
    required this.direccion,
    required this.secreto,
    http.Client? cliente,
    Resolver? resolver,
    this.espera = const Duration(seconds: 90),
  })  : _cliente = cliente ?? http.Client(),
        _resolver = resolver ?? resolverDelSistema;

  final String direccion;
  final String secreto;

  /// Cuánto esperar la respuesta: la PC puede usar varias herramientas
  /// seguidas antes de contestar.
  final Duration espera;

  final http.Client _cliente;
  final Resolver _resolver;

  /// Manda lo que dijiste y devuelve la respuesta de Sokari.
  Future<String> enviar(String comando) async {
    final texto = comando.trim();
    if (texto.isEmpty) throw const ErrorMalla('No hay nada que mandar.');
    final r = await _post(texto, espera);
    if (r.statusCode == 200) {
      final cuerpo = _texto(r).trim();
      return cuerpo.isEmpty ? 'Listo.' : cuerpo;
    }
    throw _error(r);
  }

  /// Revisa la conexión y el secreto sin gastar tu cupo de Groq: con un
  /// comando vacío la PC revisa el secreto y contesta "falta el comando".
  Future<void> probar() async {
    final r = await _post('', const Duration(seconds: 10));
    if (r.statusCode == 400) return;
    if (r.statusCode == 200) return;
    throw _error(r);
  }

  Future<http.Response> _post(String comando, Duration limite) async {
    final host = direccion.trim();
    if (!direccionPermitida(host)) {
      throw const ErrorMalla('La dirección de tu PC tiene que ser de Tailscale: su IP 100.x.y.z o su nombre '
          'que termina en .ts.net.');
    }
    if (secreto.trim().isEmpty) {
      throw const ErrorMalla('Falta el secreto de malla. Cópialo de Sokari en tu PC: Configuración → General.');
    }
    final ip = await _ipTailscale(host);
    final uri = Uri(scheme: 'http', host: ip, port: puertoMalla, path: '/comando');
    try {
      return await _cliente
          .post(
            uri,
            headers: {
              'Content-Type': 'application/json; charset=utf-8',
              'X-Sokari-Secret': secreto.trim(),
            },
            body: jsonEncode({'comando': comando}),
          )
          .timeout(limite);
    } on TimeoutException {
      throw const ErrorMalla('Tu PC no contestó a tiempo. Si estaba haciendo algo largo, vuelve a preguntar en '
          'un momento.');
    } on SocketException {
      throw const ErrorMalla(_noLlego);
    } on http.ClientException {
      throw const ErrorMalla(_noLlego);
    }
  }

  /// La IP a la que se conecta: la que escribiste, o la de tu nombre .ts.net
  /// solo si todas sus direcciones son de Tailscale (como hace la PC).
  Future<String> _ipTailscale(String host) async {
    if (_ipv4.hasMatch(host)) return host;
    List<InternetAddress> ips;
    try {
      ips = await _resolver(host).timeout(const Duration(seconds: 5));
    } on TimeoutException {
      throw const ErrorMalla(_noLlego);
    } on SocketException {
      throw const ErrorMalla('No encuentro tu PC por su nombre. ¿Tailscale está conectado en el celular?');
    }
    final v4 = ips.where((a) => a.type == InternetAddressType.IPv4).toList();
    if (v4.isEmpty || !v4.every((a) => esIpTailscale(a.rawAddress))) {
      throw const ErrorMalla('Ese nombre no apunta a una IP de Tailscale; no le mando el secreto.');
    }
    return v4.first.address;
  }

  String _texto(http.Response r) => utf8.decode(r.bodyBytes, allowMalformed: true);

  ErrorMalla _error(http.Response r) {
    switch (r.statusCode) {
      case 401:
        return const ErrorMalla('El secreto no coincide con el de tu PC (Configuración → General en Sokari).');
      case 404:
        return const ErrorMalla('En esa dirección no contesta Sokari.');
      case 413:
        return const ErrorMalla('Es demasiado largo para mandarlo de una vez.');
      case 503:
        final cuerpo = _texto(r).trim();
        return ErrorMalla(cuerpo.isEmpty ? 'Sokari está ocupado ahora, prueba en un momento.' : cuerpo);
      default:
        return ErrorMalla('Tu PC contestó con un error (HTTP ${r.statusCode}).');
    }
  }

  void cerrar() => _cliente.close();
}
