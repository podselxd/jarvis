#!/bin/sh
# Revisa que la versión sea la misma en src/config.h y en res/jarvis.rc y, si
# se le pasa un tag (v2.0.2), que coincida con ese tag. Si no coincidieran, el
# exe publicado se creería de otra versión y el actualizador lo volvería a
# bajar cada 6 horas. Uso: sh tests/check_version.sh [v2.0.2]
cd "$(dirname "$0")/.." || exit 1
cfg=$(tr -d '\r' < src/config.h)
rc=$(tr -d '\r' < res/jarvis.rc)
v=$(printf '%s\n' "$cfg" | sed -n 's/^#define JARVIS_VERSION "\(.*\)"$/\1/p')
vw=$(printf '%s\n' "$cfg" | sed -n 's/^#define JARVIS_VERSION_W L"\(.*\)"$/\1/p')
fv=$(printf '%s\n' "$rc" | sed -n 's/^FILEVERSION \([0-9]*\),\([0-9]*\),\([0-9]*\),.*/\1.\2.\3/p')
pv=$(printf '%s\n' "$rc" | sed -n 's/^PRODUCTVERSION \([0-9]*\),\([0-9]*\),\([0-9]*\),.*/\1.\2.\3/p')
fs=$(printf '%s\n' "$rc" | sed -n 's/.*VALUE "FileVersion", "\(.*\)".*/\1/p')
ps=$(printf '%s\n' "$rc" | sed -n 's/.*VALUE "ProductVersion", "\(.*\)".*/\1/p')
echo "config.h: $v / $vw | jarvis.rc: $fv, $pv, $fs, $ps${1:+ | tag: $1}"
[ -n "$v" ] || { echo "FALLA: no encontré JARVIS_VERSION en src/config.h"; exit 1; }
for x in "$vw" "$fv" "$pv" "$fs" "$ps"; do
    [ "$x" = "$v" ] || { echo "FALLA: la versión no es la misma en config.h y jarvis.rc"; exit 1; }
done
if [ -n "$1" ] && [ "${1#v}" != "$v" ]; then
    echo "FALLA: el tag $1 no coincide con la versión del código ($v)"
    exit 1
fi
echo ok
