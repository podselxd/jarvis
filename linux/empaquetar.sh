#!/bin/sh
# Arma el paquete de Sokari para Linux desde el código ya compilable:
#   sh linux/empaquetar.sh deb   -> Sokari.deb (Ubuntu 24.04 o más nuevo; se arma en Ubuntu 24.04)
#   sh linux/empaquetar.sh rpm   -> Sokari.rpm (Fedora 44; se arma en Fedora 44)
# Todo lo del paquete queda de root: la extensión de GNOME solo atiende a un
# /usr/bin/sokari que nadie más puede cambiar.
set -eu
kind=${1:-}
version=$(sed -n 's/^#define SOKARI_VERSION "\(.*\)"$/\1/p' src/config.h)
[ -n "$version" ] || { echo "No encontré la versión en src/config.h"; exit 1; }
root=$(pwd)
tmp=$(mktemp -d)
trap 'rm -rf "${tmp:?}"' EXIT

case "$kind" in
deb)
    stage="$tmp/sokari"
    make -f Makefile.linux -j4 all
    make -f Makefile.linux install DESTDIR="$stage" PREFIX=/usr
    # Las bibliotecas que usa, con los nombres de paquete de este Ubuntu.
    mkdir -p "$tmp/src/debian"
    printf 'Source: sokari\n\nPackage: sokari\nArchitecture: any\n' > "$tmp/src/debian/control"
    libs=$(cd "$tmp/src" && dpkg-shlibdeps -O -e "$stage/usr/bin/sokari" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
    [ -n "$libs" ] || { echo "dpkg-shlibdeps no dio las dependencias"; exit 1; }
    size=$(du -sk "$stage/usr" | cut -f1)
    mkdir -p "$stage/DEBIAN"
    cat > "$stage/DEBIAN/control" <<CONTROL
Package: sokari
Version: $version
Architecture: amd64
Maintainer: podselxd <podselxd@users.noreply.github.com>
Installed-Size: $size
Depends: $libs, espeak-ng, pulseaudio-utils
Recommends: pkexec, gnome-shell-extension-appindicator
Section: utils
Priority: optional
Homepage: https://github.com/podselxd/sokari
Description: Sokari, tu asistente de voz
 Le dices «Hey Sokari» y hace cosas en tu PC: abre apps y páginas, pone
 música, escribe, maneja tus archivos y ventanas, y manda órdenes a tus
 otras PCs por Tailscale. Trae su extensión de GNOME (para ver ventanas y
 oprimir teclas en Wayland): cierra sesión y vuelve a entrar una vez
 después de instalarlo.
CONTROL
    dpkg-deb --root-owner-group -Zxz --build "$stage" "$root/Sokari.deb"
    dpkg-deb --info "$root/Sokari.deb" | sed -n '/Package:/,$p'
    ;;
rpm)
    top="$tmp/rpm"
    mkdir -p "$top"
    rpmbuild -bb --build-in-place --define "_topdir $top" --define "sokari_version $version" linux/sokari.spec
    cp "$top"/RPMS/*/sokari-"$version"-*.rpm "$root/Sokari.rpm"
    rpm -qip "$root/Sokari.rpm" | head -12
    rpm -qpR "$root/Sokari.rpm" | grep -v '^rpmlib' | head -30
    ;;
*)
    echo "Uso: sh linux/empaquetar.sh deb|rpm"
    exit 2
    ;;
esac
