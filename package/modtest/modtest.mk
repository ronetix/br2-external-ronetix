################################################################################
#
# modtest
#
################################################################################

MODTEST_VERSION = 1.0
MODTEST_SITE = $(MODTEST_PKGDIR)/files
MODTEST_SITE_METHOD = local
MODTEST_LICENSE = Ronetix (proprietary)
MODTEST_DEPENDENCIES = libgpiod2

# Vendored straight into this package's files/ directory, so no separate
# fetch is needed and nothing here needs to stay in sync with an
# upstream git tree.

ifeq ($(BR2_PACKAGE_MODTEST_PM9G45),y)
MODTEST_BOARD = pm9g45
else ifeq ($(BR2_PACKAGE_MODTEST_SAMA5D3X_CM),y)
MODTEST_BOARD = sama5d3x-cm
else ifeq ($(BR2_PACKAGE_MODTEST_SAM9X5_CM),y)
MODTEST_BOARD = sam9x5-cm
endif

# Deliberately NOT passing the full $(TARGET_CONFIGURE_OPTS) blob here: it
# sets CFLAGS="$(TARGET_CFLAGS)" as a make command-line variable, which
# silently blocks this package's own vendored Makefile from appending to
# CFLAGS with "+=" (a command-line variable can't be extended by a plain
# assignment in the Makefile without "override") -- including the
# -DGPIOD_MAJOR=... flag that Makefile derives itself from `pkg-config
# --modversion libgpiod`, which is the whole point of that Makefile's own
# CFLAGS construction. Passing only CC and PKG_CONFIG (via TARGET_MAKE_ENV,
# as environment, not command-line, so they don't have that lock-out
# problem either) leaves CFLAGS entirely under the vendored Makefile's own
# control, same as a native/manual build would.
MODTEST_MAKE_ARGS = CC="$(TARGET_CC)" PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)"

define MODTEST_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) $(MODTEST_MAKE_ARGS)
endef

# Not every board has a real GPIO pin-pair map yet (the production test
# carrier's shorted-pair pinout has to be known first -- see
# board/ronetix/README.md's "modtest" section). Install one only if it
# actually exists in the source tree for the selected board; otherwise
# modtest.sh's own PAIRMAP check SKIPs the gpio test cleanly.
MODTEST_PAIRS_FILE = $(MODTEST_PKGDIR)/files/config/$(MODTEST_BOARD).pairs

define MODTEST_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) $(MODTEST_MAKE_ARGS) \
		DESTDIR=$(TARGET_DIR) install
	$(INSTALL) -D -m 0644 $(@D)/config/$(MODTEST_BOARD).env \
		$(TARGET_DIR)/etc/modtest/modtest.env
	$(if $(wildcard $(MODTEST_PAIRS_FILE)), \
		$(INSTALL) -D -m 0644 $(MODTEST_PAIRS_FILE) \
			$(TARGET_DIR)/etc/modtest/pins.pairs)
endef

ifeq ($(BR2_PACKAGE_MODTEST_AUTOSTART),y)
define MODTEST_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(MODTEST_PKGDIR)/files/scripts/S99modtest \
		$(TARGET_DIR)/etc/init.d/S99modtest
endef
endif

$(eval $(generic-package))
