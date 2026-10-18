# omarchy-pad-bridge: the guest display/input bridge between a headless Hyprland
# session and ishwl (the native iPad window).  Built for the Arch (glibc) root by
# installation/omarchy/build-guest-tools.sh, inside the aarch64 colima VM:
#
#   sh installation/omarchy/build-guest-tools.sh
#
# or by hand anywhere with wayland-client and wayland-protocols:
#
#   make -j4 && make install DESTDIR=/some/rootfs
PREFIX ?= /usr/local
PKGS := wayland-client
WP := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
SCANNER := $(shell pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || echo wayland-scanner)

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Igen -Isrc \
          $(shell pkg-config --cflags $(PKGS))
LDLIBS += $(shell pkg-config --libs $(PKGS))

# wlr-* XMLs are vendored from Hyprland's protocols/ (licence headers kept);
# xdg-shell comes from wayland-protocols.
PROTOCOLS := wlr-screencopy-unstable-v1=protocols/wlr-screencopy-unstable-v1.xml \
             wlr-virtual-pointer-unstable-v1=protocols/wlr-virtual-pointer-unstable-v1.xml \
             virtual-keyboard-unstable-v1=protocols/virtual-keyboard-unstable-v1.xml \
             xdg-shell=$(WP)/stable/xdg-shell/xdg-shell.xml
PROTO_NAMES := $(foreach p,$(PROTOCOLS),$(firstword $(subst =, ,$(p))))
PROTO_HEADERS := $(PROTO_NAMES:%=gen/%-client-protocol.h)
PROTO_SOURCES := $(PROTO_NAMES:%=gen/%-protocol.c)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o) $(PROTO_NAMES:%=build/%-protocol.o)

# The compositor-side mock used by tools/gate-0.6.sh to exercise the bridge without
# Hyprland (it needs the server-side generated code, which the shipped binary does not).
MOCK_PROTO_HEADERS := $(PROTO_NAMES:%=gens/%-server-protocol.h)
MOCK_PROTO_SOURCES := $(PROTO_NAMES:%=gens/%-protocol.c)
MOCK_OBJS := build/mock-compositor.o $(PROTO_NAMES:%=build/%-server-protocol.o)

all: omarchy-pad-bridge

mock-compositor: $(MOCK_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(shell pkg-config --libs wayland-server)

# The gate scripts' stand-in for hyprctl (it cannot set socket timeouts under iSH).
hypr-ipc: tools/hypr-ipc.c
	$(CC) $(CFLAGS) -o $@ $<

omarchy-pad-bridge: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

proto_xml = $(lastword $(subst =, ,$(filter $(1)=%,$(PROTOCOLS))))

gen/%-client-protocol.h:
	@mkdir -p gen
	$(SCANNER) client-header $(call proto_xml,$*) $@

gen/%-protocol.c:
	@mkdir -p gen
	$(SCANNER) private-code $(call proto_xml,$*) $@

build/%-protocol.o: gen/%-protocol.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: src/%.c src/bridge.h $(PROTO_HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

gens/%-server-protocol.h:
	@mkdir -p gens
	$(SCANNER) server-header $(call proto_xml,$*) $@

gens/%-protocol.c:
	@mkdir -p gens
	$(SCANNER) private-code $(call proto_xml,$*) $@

build/%-server-protocol.o: gens/%-protocol.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c -o $@ $<

build/mock-compositor.o: tools/mock-compositor.c $(MOCK_PROTO_HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -Igens $(shell pkg-config --cflags wayland-server) -c -o $@ $<

install: all
	install -Dm755 omarchy-pad-bridge $(DESTDIR)$(PREFIX)/bin/omarchy-pad-bridge

clean:
	rm -rf build gen gens omarchy-pad-bridge mock-compositor hypr-ipc

.PHONY: all install clean mock-compositor hypr-ipc
.SECONDARY: $(PROTO_SOURCES)
