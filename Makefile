CC         ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS  ?= -O2
CFLAGS  += -Wall -Wextra -Wno-deprecated-declarations $(shell $(PKG_CONFIG) --cflags gtk4)
# GTK4 dlopens libvulkan itself; dropping -lvulkan from the link line keeps
# the binary runnable on systems without a vulkan loader.
LDLIBS  += $(filter-out -lvulkan,$(shell $(PKG_CONFIG) --libs gtk4))

PREFIX     ?= /usr/local
RESOURCES_C := src/resources.c
ICONS      := $(wildcard data/icons/*.png)

winver: src/winver.c $(RESOURCES_C)
	$(CC) $(CFLAGS) -o $@ src/winver.c $(RESOURCES_C) $(LDLIBS)

$(RESOURCES_C): data/icons.gresource.xml $(ICONS)
	glib-compile-resources data/icons.gresource.xml --sourcedir=data/icons \
		--generate-source --c-name winver_icons --target=$@

install: winver
	install -Dm755 winver $(DESTDIR)$(PREFIX)/bin/winver

clean:
	rm -f winver $(RESOURCES_C)

.PHONY: install clean
