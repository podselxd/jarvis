#!/bin/sh
# Prueba la extensión de GNOME de verdad: arranca GNOME Shell sin pantalla,
# con su propio D-Bus y una carpeta personal temporal, le instala la
# extensión y corre test_gnome_real, que usa las herramientas de Sokari
# contra dos ventanas de prueba (una app normal y una "terminal").
# Nunca toca tu sesión ni tu configuración.
#   make -f Makefile.linux prueba-gnome && sh tests/linux/gnome/probar_extension.sh
set -u
if [ -z "${SOKARI_DENTRO:-}" ]; then
    SOKARI_DENTRO=1 exec dbus-run-session -- sh "$0" "$@"
fi
root=$(pwd)
tests="$root/build-linux/tests-gnome"
[ -x "$tests/test_gnome_real" ] || { echo "Primero: make -f Makefile.linux prueba-gnome"; exit 2; }
T=$(mktemp -d)
export SOKARI_PRUEBA_DIR="$T" HOME="$T/home"
export XDG_RUNTIME_DIR="$T/run" XDG_CONFIG_HOME= XDG_DATA_HOME= XDG_CACHE_HOME=
export XDG_CURRENT_DESKTOP=GNOME XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-sokari-prueba
export GSETTINGS_BACKEND=keyfile NO_AT_BRIDGE=1 GDK_BACKEND=wayland
# La extensión solo atiende al Sokari instalado en el sistema; en la prueba,
# también a lo que está en build-linux/tests-gnome.
export SOKARI_EXTENSION_ALLOW="$tests/"
mkdir -p "$XDG_RUNTIME_DIR" "$HOME/.local/share/gnome-shell/extensions" "$HOME/.local/share/applications" \
    "$HOME/.config/glib-2.0/settings" "$T/bin"
chmod 700 "$XDG_RUNTIME_DIR"
cp -r "$root/linux/extension/sokari@podselxd.github.io" "$HOME/.local/share/gnome-shell/extensions/"
cat > "$HOME/.config/glib-2.0/settings/keyfile" <<EOF
[org/gnome/shell]
enabled-extensions=['sokari@podselxd.github.io']
disable-user-extensions=false
welcome-dialog-last-shown-version='999'
EOF

# Las dos apps de prueba, con lo que dicen en $T/<quién>.out.
app() { # id nombre archivo categorías
    printf '#!/bin/sh\nexec "%s/ventana_de_prueba" "%s" "%s" > "%s/%s.out" 2>/dev/null\n' \
        "$tests" "$1" "$2" "$T" "$3" > "$T/bin/$3"
    chmod +x "$T/bin/$3"
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nCategories=%s\n' \
        "$2" "$T/bin/$3" "$4" > "$HOME/.local/share/applications/$1.desktop"
}
app io.github.podselxd.SokariPrueba "Prueba de Sokari" prueba "Utility;"
# Y la de Sokari mismo, para que GNOME sepa que su ventana es «Sokari».
printf '[Desktop Entry]\nType=Application\nName=Sokari\nExec=%s\nCategories=Utility;\n' \
    "$root/build-linux/sokari" > "$HOME/.local/share/applications/io.github.podselxd.Sokari.desktop"
export SOKARI_BIN="$root/build-linux/sokari"
app io.github.podselxd.SokariTerminal "Terminal de prueba" terminal "System;TerminalEmulator;"
# Los títulos de las ventanas ("Inicio - Prueba de Sokari") salen de aquí.
sed -i 's/"Prueba de Sokari" > /"Ventana de prueba" > /' "$T/bin/prueba"

# Un bus "de sistema" vacío: el Shell pregunta por logind y sin bus no arranca.
dbus-daemon --session --fork --print-pid=1 --address="unix:path=$T/run/system_bus" > "$T/system_bus.pid"
export DBUS_SYSTEM_BUS_ADDRESS="unix:path=$T/run/system_bus"

opts="--headless --virtual-monitor 1280x800 --wayland-display $WAYLAND_DISPLAY"
help=$(gnome-shell --help-all 2>&1)
echo "$help" | grep -q -- "--wayland " && opts="$opts --wayland"
echo "$help" | grep -q -- "--no-x11" && opts="$opts --no-x11"
echo "GNOME Shell $(gnome-shell --version 2>/dev/null | sed 's/[^0-9.]//g') con: $opts"
# shellcheck disable=SC2086
gnome-shell $opts > "$T/shell.log" 2>&1 &
shell=$!

D="--session --dest org.gnome.Shell --object-path /org/gnome/Shell/Extensions/Sokari"
ready=0
for i in $(seq 1 150); do
    if gdbus introspect $D 2>/dev/null | grep -q SokariShell1; then ready=1; break; fi
    kill -0 $shell 2>/dev/null || break
    sleep 0.2
done
rc=1
if [ $ready = 1 ]; then
    # Alguien que no es Sokari (gdbus) no puede usarla.
    if gdbus call $D --method io.github.podselxd.SokariShell1.ListWindows 2>&1 | grep -q AccessDenied; then
        echo "ok    la extensión no atiende a otros programas (gdbus: AccessDenied)"
        "$tests/test_gnome_real"
        rc=$?
    else
        echo "FALLA la extensión le contestó a gdbus, que no es Sokari"
    fi
else
    echo "FALLA GNOME Shell no arrancó o no cargó la extensión"
fi
kill $shell 2>/dev/null
wait $shell 2>/dev/null
kill "$(cat "$T/system_bus.pid")" 2>/dev/null
if [ $rc != 0 ]; then
    echo "--- lo que dijo GNOME Shell ---"
    grep -v -E "evolution|Screencast|PolicyKit|GeoClue|DisplayManager|a11y|camera|colord|RealtimeKit|accountsservice|ibus|CalendarServer|AuthenticationAgent|Settings, expect|SessionManager" "$T/shell.log" | tail -40
fi
rm -rf "${T:?}"
exit $rc
