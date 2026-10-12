# The Adrakhonic Theorem - a screensaver of the theorem from Neal Stephenson's "Anathem"
VERSION  := 1.0.0
PREFIX   ?= /usr
SYSCONFDIR ?= /etc
DESTDIR  ?=
CC       ?= cc
# gnu11 keeps the binary free of very recent glibc symbols, so packages built on a
# new distribution still run on older ones
CFLAGS   ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Isrc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)

MINGW    ?= x86_64-w64-mingw32
MINGW_ROOT ?= /usr/$(MINGW)/sys-root/mingw

all: build/adrakhonic

# the pictures in assets/art are built into the program, so it must be compiled from here
build/adrakhonic: src/adrakhonic.c src/stb_image.h $(wildcard assets/art/*)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) -o $@ src/adrakhonic.c $(LDFLAGS) $(SDL_LIBS) -lm

check: build/adrakhonic
	SDL_VIDEODRIVER=dummy ./build/adrakhonic --snapshot build/check.bmp --time 120 --size 640x400 && echo "ran a full round"

install: all
	install -Dm755 build/adrakhonic $(DESTDIR)$(PREFIX)/bin/adrakhonic
	install -Dm755 packaging/linux/adrakhonic-screensaver $(DESTDIR)$(PREFIX)/bin/adrakhonic-screensaver
	install -Dm644 packaging/linux/adrakhonic.desktop $(DESTDIR)$(PREFIX)/share/applications/adrakhonic.desktop
	install -Dm644 packaging/linux/adrakhonic-autostart.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/adrakhonic-screensaver.desktop
	install -Dm644 packaging/adrakhonic.png $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/adrakhonic.png
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/adrakhonic/README.md
	install -Dm644 LICENSE $(DESTDIR)$(PREFIX)/share/doc/adrakhonic/LICENSE

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/adrakhonic $(DESTDIR)$(PREFIX)/bin/adrakhonic-screensaver
	rm -f $(DESTDIR)$(PREFIX)/share/applications/adrakhonic.desktop $(DESTDIR)$(SYSCONFDIR)/xdg/autostart/adrakhonic-screensaver.desktop
	rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/256x256/apps/adrakhonic.png
	rm -rf $(DESTDIR)$(PREFIX)/share/doc/adrakhonic

# ---- packages (written to dist/) ----
ARCH_DEB := $(shell dpkg --print-architecture 2>/dev/null || echo amd64)

deb: all
	rm -rf build/deb && mkdir -p build/deb/DEBIAN dist
	$(MAKE) install DESTDIR=build/deb PREFIX=/usr
	strip build/deb/usr/bin/adrakhonic
	sed -e 's/@VERSION@/$(VERSION)/' -e 's/@ARCH@/$(ARCH_DEB)/' \
	    -e "s/@SIZE@/$$(du -sk build/deb/usr | cut -f1)/" packaging/linux/control.in > build/deb/DEBIAN/control
	echo /etc/xdg/autostart/adrakhonic-screensaver.desktop > build/deb/DEBIAN/conffiles
	dpkg-deb --root-owner-group --build build/deb dist/adrakhonic_$(VERSION)_$(ARCH_DEB).deb

rpm:
	mkdir -p dist build/rpm/SOURCES
	git archive --format=tar.gz --prefix=adrakhonic-$(VERSION)/ -o build/rpm/SOURCES/adrakhonic-$(VERSION).tar.gz HEAD
	rpmbuild -bb $(RPMFLAGS) --define "_topdir $(CURDIR)/build/rpm" --define "version $(VERSION)" packaging/linux/adrakhonic.spec
	cp build/rpm/RPMS/*/adrakhonic-$(VERSION)-*.rpm dist/

# Windows: the screensaver itself, and a small Adrakhonic.scr that goes in the
# Windows system folder (so it is listed with the other screensavers) and starts it
windows:
	@mkdir -p build/win
	$(MINGW)-gcc -O2 -std=gnu11 -Wall -Isrc -I$(MINGW_ROOT)/include/SDL2 -Dmain=SDL_main \
	    -o build/win/adrakhonic.exe src/adrakhonic.c \
	    -L$(MINGW_ROOT)/lib -lmingw32 -lSDL2main -lSDL2 -lm -mwindows -static-libgcc
	$(MINGW)-windres packaging/windows/stub.rc -O coff -o build/win/stub.res
	$(MINGW)-gcc -O2 -Wall -o build/win/Adrakhonic.scr packaging/windows/stub.c build/win/stub.res -mwindows -static
	$(MINGW)-strip build/win/adrakhonic.exe build/win/Adrakhonic.scr
	cp $(MINGW_ROOT)/bin/SDL2.dll build/win/
	[ ! -f $(MINGW_ROOT)/bin/SDL3.dll ] || cp $(MINGW_ROOT)/bin/SDL3.dll build/win/

installer: windows
	mkdir -p dist
	makensis -V2 -DVERSION=$(VERSION) packaging/windows/adrakhonic.nsi

dist: deb rpm installer

clean:
	rm -rf build dist

.PHONY: all check install uninstall deb rpm windows installer dist clean
