# gpiotest - builds against libgpiod 1.x or 2.x, whichever pkg-config finds.
#
#   make                                  native build
#   make CROSS_COMPILE=arm-linux-gnueabi- cross build
#
# Under Buildroot the recipe drives this via TARGET_MAKE_ENV /
# TARGET_CONFIGURE_OPTS, which already set CC and export PKG_CONFIG
# pointed at the staging sysroot, so plain "pkg-config" resolves correctly
# without CROSS_COMPILE needing to be passed explicitly.

CROSS_COMPILE ?=
CC            ?= $(CROSS_COMPILE)gcc
PKG_CONFIG    ?= pkg-config

DESTDIR ?=
prefix  ?= /usr
bindir  ?= $(prefix)/bin

GPIOD_VERSION := $(shell $(PKG_CONFIG) --modversion libgpiod 2>/dev/null)
ifeq ($(GPIOD_VERSION),)
$(error libgpiod not found by pkg-config - select BR2_PACKAGE_LIBGPIOD or BR2_PACKAGE_LIBGPIOD2)
endif
GPIOD_MAJOR := $(firstword $(subst ., ,$(GPIOD_VERSION)))

# GPIOD_LINE_REQUEST_FLAG_BIAS_* and gpiod_line_set_config() only exist from
# libgpiod 1.5 onward. Below that, pull-up/down cannot be requested from
# userspace at all - the pin test needs 1.5+ (or the 2.x API).
GPIOD_HAVE_BIAS := $(shell $(PKG_CONFIG) --atleast-version=1.5.0 libgpiod && echo 1 || echo 0)
ifeq ($(GPIOD_MAJOR)-$(GPIOD_HAVE_BIAS),1-0)
$(warning libgpiod $(GPIOD_VERSION) predates 1.5: no bias support, gpiotest cannot drive pull-up/down)
endif

GPIOD_CFLAGS := $(shell $(PKG_CONFIG) --cflags libgpiod)
GPIOD_LIBS   := $(shell $(PKG_CONFIG) --libs libgpiod)

CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter \
           -DGPIOD_MAJOR=$(GPIOD_MAJOR) -DGPIOD_HAVE_BIAS=$(GPIOD_HAVE_BIAS) \
           $(GPIOD_CFLAGS)
LDLIBS  += $(GPIOD_LIBS)

OBJS = src/gpiotest.o src/gpio_compat.o

all: gpiotest
	@echo "built against libgpiod $(GPIOD_VERSION) (API generation $(GPIOD_MAJOR))"

gpiotest: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

src/%.o: src/%.c src/gpio_compat.h
	$(CC) $(CFLAGS) -c $< -o $@

install: gpiotest
	install -d $(DESTDIR)$(bindir)
	install -m 0755 gpiotest $(DESTDIR)$(bindir)/gpiotest
	install -m 0755 scripts/modtest.sh $(DESTDIR)$(bindir)/modtest
	install -d $(DESTDIR)/etc/modtest

clean:
	rm -f gpiotest $(OBJS)

.PHONY: all install clean
