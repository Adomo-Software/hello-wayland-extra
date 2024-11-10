# USE_XDG_DECORATION := 1

CFLAGS ?= -std=c11 -Wall -Wextra -Wno-unused-parameter -g
PKG_CONFIG ?= pkg-config

# Host deps
WAYLAND_FLAGS = $(shell $(PKG_CONFIG) cairo wayland-client --cflags --libs)
WAYLAND_PROTOCOLS_DIR = $(shell $(PKG_CONFIG) wayland-protocols --variable=pkgdatadir)

# Build deps
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

XDG_SHELL_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

HEADERS=xdg-shell-client-protocol.h shm.h
SOURCES=main.c xdg-shell-protocol.c shm.c

all: hello-wayland

ifdef USE_XDG_DECORATION
CFLAGS += -DUSE_XDG_DECORATION
HEADERS += xdg-decoration-unstable-v1-client-protocol.h
SOURCES += xdg-decoration-unstable-v1-client-protocol.c
XDG_DECORATION_PROTOCOL = $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
xdg-decoration-unstable-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_DECORATION_PROTOCOL) xdg-decoration-unstable-v1-client-protocol.h
xdg-decoration-unstable-v1-client-protocol.c:
	$(WAYLAND_SCANNER) private-code $(XDG_DECORATION_PROTOCOL) xdg-decoration-unstable-v1-client-protocol.c
endif

hello-wayland: $(HEADERS) $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $(SOURCES) -lrt $(WAYLAND_FLAGS)

xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_PROTOCOL) xdg-shell-client-protocol.h

xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(XDG_SHELL_PROTOCOL) xdg-shell-protocol.c

.PHONY: clean
clean:
	$(RM) hello-wayland xdg-shell-protocol.c xdg-shell-client-protocol.h xdg-decoration-unstable-v1-client-protocol.*
