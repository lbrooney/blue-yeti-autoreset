CC ?= cc
PKG_CONFIG ?= pkg-config
INSTALL ?= install

bindir = /usr/bin
libexecdir = /usr/lib/blue-yeti-autoreset
systemdunitdir = /usr/lib/systemd/system
udevrulesdir = /usr/lib/udev/rules.d
docdir = /usr/share/doc/blue-yeti-autoreset
licensedir = /usr/share/licenses/blue-yeti-autoreset

CPPFLAGS += $(shell $(PKG_CONFIG) --cflags libusb-1.0)
CFLAGS ?= -O2
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wformat=2 -Werror
LDLIBS += $(shell $(PKG_CONFIG) --libs libusb-1.0)

.PHONY: all check clean install

all: build/blue-yeti-reset

build:
	mkdir -p build

build/blue-yeti-reset: src/blue-yeti-reset.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< $(LDLIBS) -o $@

check: build/blue-yeti-reset
	./build/blue-yeti-reset >/dev/null
	sh -n scripts/blue-yeti-autoreset tests/check-guard.sh
	sh ./tests/check-guard.sh
	udevadm verify --no-style udev/70-blue-yeti-autoreset.rules

install: build/blue-yeti-reset
	$(INSTALL) -D -m 0755 build/blue-yeti-reset \
		"$(DESTDIR)$(bindir)/blue-yeti-reset"
	$(INSTALL) -D -m 0755 scripts/blue-yeti-autoreset \
		"$(DESTDIR)$(libexecdir)/blue-yeti-autoreset"
	$(INSTALL) -D -m 0644 systemd/blue-yeti-autoreset@.service \
		"$(DESTDIR)$(systemdunitdir)/blue-yeti-autoreset@.service"
	$(INSTALL) -D -m 0644 udev/70-blue-yeti-autoreset.rules \
		"$(DESTDIR)$(udevrulesdir)/70-blue-yeti-autoreset.rules"
	$(INSTALL) -D -m 0644 README.md "$(DESTDIR)$(docdir)/README.md"
	$(INSTALL) -D -m 0644 LICENSE "$(DESTDIR)$(licensedir)/LICENSE"

clean:
	rm -rf build
