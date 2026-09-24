import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:sokari_remoto/malla.dart';

/// Una PC de mentira que cuenta los pedidos que le llegan.
class PcFalsa {
  PcFalsa(this.responder);

  final Future<http.Response> Function(http.Request r) responder;
  final pedidos = <http.Request>[];

  MockClient get cliente => MockClient((r) {
        pedidos.add(r);
        return responder(r);
      });
}

http.Response texto(String t, int codigo) =>
    http.Response.bytes(utf8.encode(t), codigo, headers: {'content-type': 'text/plain; charset=utf-8'});

void main() {
  group('direccionPermitida (mismas reglas que la PC)', () {
    for (final ok in ['100.64.0.1', '100.127.255.255', '100.100.5.9', 'mi-pc.tu-red.ts.net', 'KAORI.TAIL1234.TS.NET']) {
      test('acepta $ok', () => expect(direccionPermitida(ok), isTrue));
    }
    for (final no in [
      '100.63.255.255', '100.128.0.1', '192.168.1.10', '10.0.0.5', '8.8.8.8', '256.64.0.1', '100.64.0',
      '100.64.0.1.evil.com', 'evil.com', 'ts.net', '.ts.net', '-x.ts.net', 'a..ts.net', 'x.ts.net:80',
      'user@x.ts.net', 'x.ts.net/evil', 'x.ts.net.evil.com', '', ' 100.64.0.1',
    ]) {
      test('niega "$no"', () => expect(direccionPermitida(no), isFalse));
    }
  });

  group('ClienteMalla', () {
    test('manda el comando como la malla de la PC y devuelve la respuesta', () async {
      final pc = PcFalsa((_) async => texto('Abrí Spotify.', 200));
      final c = ClienteMalla(direccion: '100.101.102.103', secreto: 'abc', cliente: pc.cliente);
      expect(await c.enviar('  abre Spotify y pon música de acción  '), 'Abrí Spotify.');
      final r = pc.pedidos.single;
      expect(r.method, 'POST');
      expect(r.url.toString(), 'http://100.101.102.103:8765/comando');
      expect(r.headers['X-Sokari-Secret'], 'abc');
      expect(jsonDecode(utf8.decode(r.bodyBytes)), {'comando': 'abre Spotify y pon música de acción'});
    });

    test('una respuesta vacía es "Listo."', () async {
      final pc = PcFalsa((_) async => texto('', 200));
      expect(await ClienteMalla(direccion: '100.64.0.1', secreto: 's', cliente: pc.cliente).enviar('hola'), 'Listo.');
    });

    Future<String> error(int codigo, [String cuerpo = '']) async {
      final pc = PcFalsa((_) async => texto(cuerpo, codigo));
      try {
        await ClienteMalla(direccion: '100.64.0.1', secreto: 's', cliente: pc.cliente).enviar('hola');
      } on ErrorMalla catch (e) {
        return e.mensaje;
      }
      return 'sin error';
    }

    test('401: el secreto no coincide', () async => expect(await error(401), contains('secreto no coincide')));
    test('503: pasa el aviso de la PC', () async {
      expect(await error(503, 'Sokari está ocupado ahora, prueba en un momento.'), contains('ocupado'));
    });
    test('otro error dice el código', () async => expect(await error(500), contains('HTTP 500')));

    test('si la PC no contesta a tiempo, avisa', () async {
      final pc = PcFalsa((_) => Completer<http.Response>().future);
      final c = ClienteMalla(
          direccion: '100.64.0.1', secreto: 's', cliente: pc.cliente, espera: const Duration(milliseconds: 50));
      expect(c.enviar('hola'), throwsA(isA<ErrorMalla>().having((e) => e.mensaje, 'mensaje', contains('a tiempo'))));
    });

    test('sin conexión: la PC apagada o sin Tailscale', () async {
      for (final falla in <Object>[const SocketException('sin ruta'), http.ClientException('conexión rechazada')]) {
        final pc = PcFalsa((_) async => throw falla);
        final c = ClienteMalla(direccion: '100.64.0.1', secreto: 's', cliente: pc.cliente);
        expect(c.enviar('hola'), throwsA(isA<ErrorMalla>().having((e) => e.mensaje, 'm', contains('No encuentro'))));
      }
    });

    test('nunca manda el secreto fuera de Tailscale', () async {
      final pc = PcFalsa((_) async => texto('no debería llegar', 200));
      for (final d in ['192.168.1.20', 'mi-pc.local', 'evil.com']) {
        await expectLater(ClienteMalla(direccion: d, secreto: 's', cliente: pc.cliente).enviar('hola'),
            throwsA(isA<ErrorMalla>()));
      }
      expect(pc.pedidos, isEmpty);
    });

    test('un nombre .ts.net solo se usa si resuelve a una IP de Tailscale', () async {
      final pc = PcFalsa((_) async => texto('ok', 200));
      final bueno = ClienteMalla(
          direccion: 'kaori.tail1234.ts.net',
          secreto: 's',
          cliente: pc.cliente,
          resolver: (_) async => [InternetAddress('100.90.1.2')]);
      expect(await bueno.enviar('hola'), 'ok');
      expect(pc.pedidos.single.url.host, '100.90.1.2');

      final malo = ClienteMalla(
          direccion: 'kaori.tail1234.ts.net',
          secreto: 's',
          cliente: pc.cliente,
          resolver: (_) async => [InternetAddress('192.168.1.5')]);
      await expectLater(malo.enviar('hola'),
          throwsA(isA<ErrorMalla>().having((e) => e.mensaje, 'm', contains('no apunta a una IP de Tailscale'))));
      expect(pc.pedidos, hasLength(1));
    });

    test('sin secreto o sin texto no manda nada', () async {
      final pc = PcFalsa((_) async => texto('ok', 200));
      await expectLater(ClienteMalla(direccion: '100.64.0.1', secreto: ' ', cliente: pc.cliente).enviar('hola'),
          throwsA(isA<ErrorMalla>()));
      await expectLater(ClienteMalla(direccion: '100.64.0.1', secreto: 's', cliente: pc.cliente).enviar('  '),
          throwsA(isA<ErrorMalla>()));
      expect(pc.pedidos, isEmpty);
    });

    test('probar: un comando vacío revisa el secreto sin gastar Groq', () async {
      final pc = PcFalsa((r) async {
        final comando = jsonDecode(r.body)['comando'];
        return comando == '' && r.headers['X-Sokari-Secret'] == 'bueno'
            ? texto('falta el comando', 400)
            : texto('secreto invalido', 401);
      });
      await ClienteMalla(direccion: '100.64.0.1', secreto: 'bueno', cliente: pc.cliente).probar();
      await expectLater(ClienteMalla(direccion: '100.64.0.1', secreto: 'malo', cliente: pc.cliente).probar(),
          throwsA(isA<ErrorMalla>().having((e) => e.mensaje, 'm', contains('secreto no coincide'))));
    });
  });
}
