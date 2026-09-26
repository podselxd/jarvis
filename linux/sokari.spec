# Sokari para Fedora: sh linux/empaquetar.sh rpm (desde la raíz del código).
%global debug_package %{nil}

Name:           sokari
Version:        %{sokari_version}
Release:        1%{?dist}
Summary:        Tu asistente de voz: le dices «Hey Sokari» y hace cosas en tu PC
License:        LicenseRef-Proprietary
URL:            https://github.com/podselxd/sokari

BuildRequires:  gcc make pkgconf-pkg-config libcurl-devel pulseaudio-libs-devel glib2-devel gtk3-devel
BuildRequires:  libayatana-appindicator-gtk3-devel
Requires:       espeak-ng pulseaudio-utils
Recommends:     gnome-shell-extension-appindicator

%description
Le dices «Hey Sokari» y hace cosas en tu PC: abre apps y páginas, pone
música, escribe, maneja tus archivos y ventanas, y manda órdenes a tus otras
PCs por Tailscale. Trae su extensión de GNOME (para ver ventanas y oprimir
teclas en Wayland): cierra sesión y vuelve a entrar una vez después de
instalarlo.

%build
make -f Makefile.linux %{?_smp_mflags} all

%install
make -f Makefile.linux install DESTDIR=%{buildroot} PREFIX=%{_prefix}

%files
%{_bindir}/sokari
%{_datadir}/applications/io.github.podselxd.Sokari.desktop
%{_datadir}/icons/hicolor/*/apps/io.github.podselxd.Sokari.png
%{_datadir}/gnome-shell/extensions/sokari@podselxd.github.io/
