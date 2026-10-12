Name:           adrakhonic
Version:        %{version}
Release:        1%{?dist}
Summary:        The Adrakhonic theorem, a screensaver from "Anathem"
License:        MIT
Source0:        adrakhonic-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The Adrakhonic theorem from Neal Stephenson's novel Anathem: in a triangle
with a square corner, the square on the long side is as big as the squares on
the other two sides together.

A fraa at a slate proves it four ways in coloured chalk, with pieces that slide,
shear and turn, and with a different triangle every time.

On Wayland desktops it starts by itself after five minutes idle.
adrakhonic-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/adrakhonic

%files
%license LICENSE
%doc README.md
%{_bindir}/adrakhonic
%{_bindir}/adrakhonic-screensaver
%{_datadir}/applications/adrakhonic.desktop
%{_datadir}/icons/hicolor/256x256/apps/adrakhonic.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/adrakhonic-screensaver.desktop
