#!/bin/sh
# Corre las pruebas de Linux (menos test_groq, que necesita una API key de
# Groq) con una carpeta personal temporal: nunca tocan tu configuración ni tu
# memoria. Uso: make -f Makefile.linux tests && sh tests/correr_linux.sh
set -u
home=$(mktemp -d)
fail=0
for t in build-linux/tests/*; do
    [ -f "$t" ] && [ -x "$t" ] || continue
    n=$(basename "$t")
    if [ "$n" = test_groq ]; then
        echo "Se omite test_groq: necesita una API key de Groq."
        continue
    fi
    [ -n "${GITHUB_ACTIONS:-}" ] && echo "::group::$n"
    if HOME="$home" XDG_CONFIG_HOME= XDG_DATA_HOME= SOKARI_SIN_DESCARGAS=1 "$t"; then
        [ -n "${GITHUB_ACTIONS:-}" ] && echo "::endgroup::"
    else
        rc=$?
        [ -n "${GITHUB_ACTIONS:-}" ] && echo "::endgroup::" && echo "::error::$n falló (código $rc)"
        echo "FALLÓ $n (código $rc)"
        fail=1
    fi
done
rm -rf "${home:?}"
exit $fail
