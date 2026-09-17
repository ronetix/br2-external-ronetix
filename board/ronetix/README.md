# Board notes

## Shared patches

`common/patches/` holds patches used by more than one board, so they
exist once instead of as identical (or near-identical) copies under
each board's own `patches/` directory. Referenced from each board's
defconfig via `BR2_LINUX_KERNEL_PATCH` /
`BR2_TARGET_UBOOT_PATCH` alongside that board's own patches, in
whatever order the underlying files need (Buildroot applies them in
the order listed, and a patch to an unrelated file doesn't care about
its position relative to the others).

- `common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
  — used by pm9g45, sama5d3x-cm, and sam9x5-cm. GCC 15 (Ubuntu
  24.10+/25.x/26.x) makes `constexpr` a reserved C23 keyword, and
  `scripts/unifdef.c` uses it as an ordinary identifier. Renames
  `constexpr` → `constexpression`, matching the real upstream kernel
  fix. Applies cleanly to all three kernel trees (spanning kernel
  5.10.80 and 6.6.64) since all three carry an unmodified, identical
  `scripts/unifdef.c`.

## Global patches

`../../global-patches/` (i.e. `br2-external-ronetix/global-patches/`,
sibling to `board/`) holds patches for **stock packages that ship with
the upstream `linux4microchip/buildroot-mchp` tree itself** —
`package/<pkgname>/` in the main Buildroot checkout — as opposed to the
custom-fetched kernel/U-Boot/AT91Bootstrap3 sources or `common/`/
per-board patches above, which are all Ronetix-specific and live
entirely inside this external tree. `BR2_GLOBAL_PATCH_DIR` is
Buildroot's built-in mechanism for patching any package by name from
an external location, without touching the package's own directory in
the main tree. Each board's defconfig sets
`BR2_GLOBAL_PATCH_DIR="$(BR2_EXTERNAL_RONETIX_PATH)/global-patches"`.

- `global-patches/dtc/0002-fix-discarded-const-qualifiers.patch` — dtc
  1.7.2 (the version pinned by `linux4microchip/buildroot-mchp` at
  `linux4microchip-2026.04`) fails to build under glibc 2.43+ (Ubuntu
  25.10+): newer glibc's type-generic `memchr()`/`strrchr()` overloads
  return a `const`-qualified pointer for a `const` input, and dtc
  builds with `-Werror`, so two pre-existing constness mismatches in
  `libfdt/fdt_overlay.c` and `fdtput.c` become hard build failures.
  Backports the fix already applied upstream in dtc v1.8.0
  (`github.com/dgibson/dtc`, issue #180), adapted to the 1.7.2 tree
  pinned here. `package/dtc/` already carries an unrelated `0001`
  patch (`fix include guards for older kernel/u-boot`, a real upstream
  Buildroot commit) — this `0002` doesn't touch that file's numbering
  since it lives in a different directory (`global-patches/dtc/`, not
  `package/dtc/`) and Buildroot looks both up independently for the
  same package name.

## modtest

`package/modtest/` is a Buildroot package (`BR2_PACKAGE_MODTEST`)
providing a production test tool for this tree's three AT91 boards.
Two pieces, both installed to the target's `/usr/bin/`:

- **`gpiotest`** — a GPIO loopback pair test against a production test
  carrier that shorts connector pins together in known pairs: drives
  each pin high and low in turn while every other pin is read as a
  peer, catching opens, shorts, and stuck pins in one sweep. Pure
  libgpiod, builds and links against `libgpiod2` (2.2) for all three
  boards.
- **`modtest`** (`scripts/modtest.sh`) — a POSIX shell runner that
  calls `gpiotest` plus functional checks, printing one
  `TEST <name> <PASS|FAIL|SKIP> <detail>` record per check and a final
  `RESULT PASS`/`FAIL`:
  - **cpu** — sanity-checks `/proc/cpuinfo` against the expected core.
  - **ddr_size**/**ddr_pattern** — reports `/proc/meminfo`'s size and
    runs `memtester` (if installed) over a configurable chunk
    (`DDR_MB`).
  - **nand** — looks up the rootfs MTD partition **by label**
    (`MTD_PART_LABEL`, default `"rootfs"`) via `/proc/mtd`, not a
    hardcoded device index, since a numeric MTD index breaks silently
    the moment anything else on the board probes an MTD device before
    `atmel_nand` does.
  - **mmc** — only runs if `MMC_DEV` is set in the board's env file;
    unset means no check and no output line at all (not even SKIP),
    since a board or fixture with no MMC/SD to test has nothing
    meaningful to report.
  - **eth** — requires an actual DHCP-leased IPv4 address, not just
    link-up (a 169.254.0.0/16 zeroconf address doesn't count as a
    lease). Polls `operstate` for up to `ETH_LINK_TIMEOUT` seconds
    (default 8) instead of a fixed sleep, so a fast link returns
    immediately and a slower one isn't failed on a race; makes one
    bounded `udhcpc` attempt if no lease is already present.
  - **i2c** — bus-presence check (one device answering is enough) for
    each bus number listed in `I2C_BUSES`. Empty (default) reports
    SKIP — the board's I2C buses haven't been characterized for a
    production fixture yet. `I2C_BUSES=0` means a carrier has been
    confirmed to have **no** I2C device to test at all, a different,
    more final state than "not yet configured" — no check, no output
    line.
  - **usb** — counts USB-attached block devices; `USB_STORAGE_COUNT=0`
    (default) reports the count as informational only, a real number
    turns it into a pass/fail check against the fixture's expected
    count.
  - **gpio** — runs `gpiotest -c $PAIRMAP` (default
    `/etc/modtest/pins.pairs`) if a pin map is installed for the
    board; otherwise SKIPs.

  None of pm9g45/sama5d3x-cm/sam9x5-cm (AT91SAM9G45, AT91SAM9x5,
  SAMA5D3x) have an on-chip thermal sensor, so there is no temperature
  check.

Per-board defaults live in `package/modtest/files/config/<board>.env`,
installed to `/etc/modtest/modtest.env` and sourced by `modtest` at
startup — which board's file gets installed is selected by a Kconfig
choice (`BR2_PACKAGE_MODTEST_PM9G45` / `_SAMA5D3X_CM` / `_SAM9X5_CM`),
set once per defconfig. Only facts confirmed on real hardware are
asserted as defaults; anything fixture-specific that hasn't been
characterized is left unconfigured rather than guessed.

**GPIO pin-pair map**: `modtest.mk` installs
`package/modtest/files/config/<board>.pairs` to `/etc/modtest/pins.pairs`
automatically when that file exists for the selected board — silently
skipped otherwise, so `modtest` reports SKIP for the `gpio` check.
`sama5d3x-cm.pairs` is shipped: 51 pairs (102 unique labels) across the
SAMA5-CARRIER's J12–J17 headers (`SAMA5-CARRIER_Schematic.PDF`, sheet 6
"IO"), matching the carrier's shorting-jig convention (each header's
pin1–pin2, pin3–pin4, ... shorted together, wired on the schematic as
two different SoC GPIO signals per row). `pm9g45.pairs` is also shipped:
31 pairs (62 unique labels) across the BB9G45 baseboard's J12/J13
headers (`pm9g45_carrier_scheamtics.pdf`, sheet 6 "Extenstion I/O,
WIFI"), same shorting-jig convention — see the pm9g45-specific notes
below. sam9x5-cm has none yet — its carrier's pinout isn't defined.

pm9g45-specific notes:

- **Requires two baseboard modifications** (see `pm9g45.pairs`'s own
  header): connect `USART1_SHDN`/`USART2_SHDN`/`USART3_SHDN` to GND at
  `J16`, and remove `R91` and `R86` (bottom/top side respectively) to
  free `PB5` and `PE31` for testing.
- **J14 is deliberately not used**: its pins are the raw EBI/memory bus
  (`D0-D15`, `A0-A10`, `NCS1`, `WE/WR0`, `NRD`, `BS1/WR1`, `BS3/WR3`),
  confirmed against the production kernel dts to be the live NAND
  interface this SoM boots and runs from (`pm9g45.dts`'s
  `nand-controller` is enabled with `pinctrl_nand_cs`/`pinctrl_nand_rb`,
  and `at91sam9g45.dtsi`'s EBI/NAND bus muxing covers the same physical
  pins) — not a spare expansion port.
- **`PB12`/`PB13` (J12 row 7) are excluded**: `dbgu` console (RXD/TXD),
  needed for modtest's own logging.
- **`PD0`/`PD8` (J13 rows 9/10) are excluded**: USB0/USB1
  `atmel,vbus-gpio` (see the USB VBUS patch below) — actively driven by
  the kernel, not free.
- **`PD30`/`PD29` (J13 row 17) are tested**: `PD30` is `led0` (see the
  same patch) in production; `pm9g45-modtest.dts` disables it (see its
  own comment there) to free the pin, no jumper needed since row 17 is
  a normal adjacent pair on the header.
- **`PD31`/`PE31` (J12 pins 38/36) need an external jumper**: each is
  `NC`-paired with its own row partner on the header itself (pin35-36
  is `NC`/`PE31`, pin37-38 is `NC`/`PD31`), so testing them as a pair
  needs a jumper bridging pins 36 and 38 directly, on top of the `R86`
  removal above. `pm9g45-modtest.dts` also disables `led1` (see below)
  to free `PD31`.
- **`PB2`/`PB3` (J12 row 5) are tested but not in the pinctrl hog**:
  `PB3` is `SPI0_NPCS0`, permanently pulled to 3.3V via a 100kΩ
  resistor (`R17`) on the SoM itself, for the onboard SPI DataFlash
  footprint's chip-select (`AT45DB321E`/`AT45DB321D`/`AT25DF321A`,
  three alternate parts on the `CPU_IO_SPI_FLASH.SchDoc` sheet) —
  populated on every unit regardless of whether that flash chip itself
  is fitted (not visible in the production kernel dts, since nothing
  there references SPI0). That fixed pull-up already agrees with this
  board's pull-up convention (below), so it does the hog's job on its
  own.
- **`chip=`/`offset=` mapping**: same `PA`→`gpiochip0` ... `PE`→
  `gpiochip4` convention as sama5d3x-cm. Confirmed on real hardware
  via `gpioinfo` (line names `pioA0`.. `pioE31` match this numbering
  exactly) despite the AT91SAM9G45 being an older SoC generation
  (ARM926EJ-S / `atmel,at91rm9200-gpio` driver) than sama5d3x-cm's
  Cortex-A5 `pinctrl-at91` driver.
- **`extbias=up` on every row, via a test-only dts**: gpiotest's own
  runtime bias request (`intbias=`) has no effect on this SoC's
  pinctrl driver (`atmel,at91rm9200-pinctrl`, `pinctrl-at91.c`), same
  conclusion as sama5d3x-cm, so `board/ronetix/pm9g45/dts/
  pm9g45-modtest.dts` defines a boot-time pinctrl hog instead. Unlike
  sama5d3x-cm's hog, it's **pull-up**, not pull-down: per the
  Microchip AT91SAM9G45 datasheet (doc6438E), Table 46-2 "DC
  Characteristics" only lists an `RPULLUP` row (typ. 75kΩ) for general
  PIO lines, and chapter 29.4's only bias-control section is "29.4.1
  Pull-up Resistor Control" — this chip has no internal pull-down at
  all on PA/PB/PD/PE lines, unlike sama5d3x-cm's SoC. Every row is
  marked `extbias=up` to match. Unlike sama5d3x-cm, every peripheral
  this board's production dts enables stays enabled in the test dts
  too — nothing needed disabling, since `pm9g45.pairs` was already
  built to avoid every pin those peripherals claim (see the exclusions
  above); the test dts is a straight copy of `pm9g45.dts` (patches
  replicated by hand — see below) plus the hog.

sama5d3x-cm-specific notes:

- **This map is for `sama5d3x_cm_test_defconfig` only, not
  `sama5d3x_cm_defconfig`**: it assumes the test-only device tree
  (`board/ronetix/sama5d3x-cm/dts/sama5d35ek-modtest.dts`) is what's
  booted. That dts disables several peripherals this carrier doesn't
  populate or doesn't need for the test image (`i2c1`, `macb1`,
  `mmc0`/`mmc1`, `can0`, `usart1`, `adc0`), specifically so their pins
  are free for this loopback test — see the dts file's own comments
  for the reasoning behind each one. `sama5d3x_cm_defconfig` itself
  still boots stock `sama5d35ek.dts` unmodified and carries no
  modtest/test-dts settings at all; production and test are two fully
  separate images built from `configs/sama5d3x_cm_test_defconfig` and
  `configs/sama5d3x_cm_defconfig` respectively.
- **`chip=`/`offset=` mapping**: `PA`→`gpiochip0` ... `PE`→`gpiochip4`,
  offset = the number after the port letter (e.g. `PE24` = `gpiochip4`
  offset 24). Confirmed against a real `gpioinfo` dump on hardware.
- **`PB13`/`PD8` (J16 row 5) are excluded**, not freed: `PB13` is
  macb0's real eth0 RGMII `GRXER` signal, and the test dts's pinctrl
  pull-down hog (below) would otherwise claim it before macb0 can
  probe, taking eth0 down entirely. `PD8` has no partner without it.
- **Pull-down hog**: this carrier has no board pull-up/pull-down
  resistors on any of these nets, so `sama5d35ek-modtest.dts` defines
  a pinctrl group covering every pin in this file as plain GPIO with
  an actively enabled pull-down, applied automatically at boot via the
  pin controller's own "hog" mechanism (`pinctrl_claim_hogs()` in
  `drivers/pinctrl/core.c`) rather than a runtime `gpiotest` request —
  AT91's gpiochip driver has no live per-request bias support, so a
  static, boot-time pinctrl configuration is the only mechanism that
  actually works here.
- **`extbias=down` on every row**: since the pull-down comes from the
  dts, not from `gpiotest` itself, each row is marked `extbias=down`
  (a "board resistor" `gpiotest` doesn't manage, just needs to know
  about) rather than `intbias=down` (which would ask the tool to
  request a live pull that this SoC's gpiochip driver can't actually
  honor). This tells `gpiotest` to rely only on the high phase for its
  short/open checks on these pins.
- `J15` row 7 (`PD28`) is also `USB_A_OC#` (USB-A overcurrent sense)
  per the schematic — nothing currently claims it in Linux, but driving
  it as a test GPIO will interact with the USB-A power-switch
  overcurrent logic.

Operational note: building a single package target (e.g. `make
modtest`) populates `target/` but does **not** regenerate the final
flashable images (`rootfs.ubi`/`rootfs.ubifs`) — those are only
rebuilt by a full, plain `make`. Rebuild the full image before
reflashing after changing a single package.

## pm9g45

| Component | Source | Version pinned |
|---|---|---|
| Kernel | `https://github.com/linux4microchip/linux.git` | commit `c5ee3b5209256990ae272d97b69103e3289f2190` (tip of `linux-6.6-mchp` as of 2026-09-14) |
| U-Boot | `https://github.com/linux4sam/u-boot-at91.git` | commit `ffe4012789d5742fdd96c0e238138b1e83e4f460` (tip of `u-boot-2024.07-mchp` as of 2026-09-14) |
| AT91Bootstrap3 | `https://github.com/ronetix/at91bootstrap.git` | branch `at91bootstrap-3.10.4_rnx`, defconfig `pm9g45_nf_uboot` (`contrib/board/ronetix/pm9g45/`) |

Kernel and U-Boot are pinned to specific commits (not floating branch
names) in `configs/pm9g45_defconfig`'s
`BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION` /
`BR2_TARGET_UBOOT_CUSTOM_REPO_VERSION` — Buildroot's git downloader
accepts a raw commit hash there exactly like a branch or tag name. To
move forward later: check out the branch, pick a new commit, verify a
build, then update the pinned hash deliberately — don't just drop back
to the bare branch name. AT91Bootstrap3 stays on the branch name.

Patches (3, all verified to apply cleanly against the pinned base):

- `patches/linux/0001-dts-microchip-pm9g45.dts-modify-partition-table.patch`
  — changes `arch/arm/boot/dts/microchip/pm9g45.dts`'s NAND partition
  layout from the stock barebox-oriented table to a U-Boot-oriented one
  (u-boot/env/env2/dtb/kernel/rootfs).
- `patches/uboot/0001-configs-pm9g45_defconfig-modify-to-boot-Linux-kernel.patch`
  — changes `CONFIG_BOOTARGS`/`CONFIG_BOOTCOMMAND` in
  `configs/pm9g45_defconfig` to boot a UBIFS rootfs instead of JFFS2.
- `../common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
  (shared with sama5d3x-cm and sam9x5-cm — see "Shared patches" above).
- `patches/linux/0002-dts-microchip-pm9g45.dts-fix-led0-gpio-add-usb-vbus.patch`
  — two fixes to `pm9g45.dts`, derived from the PM9G45/BB9G45
  schematics (`pm9g45_Variant_Industrial_Schematic.PDF` for the SoM,
  `bb9g45-sch.pdf` for the baseboard):
  - `led0`'s gpio was `&pioD 0`. Per the baseboard schematic (sheet 4,
    "MMC, ETH, USB"), PD0 is actually the enable pin for `U10`
    (`AP2142AS`, a TPS2042-compatible dual USB power switch) channel 1,
    which supplies VBUS to `J10` "USB Host A" — not an LED. Moved to
    `&pioD 30`, which is otherwise unused in this dts.
  - Added `atmel,vbus-gpio = <&pioD 0 GPIO_ACTIVE_LOW &pioD 8
    GPIO_ACTIVE_LOW>;` to `usb0` (OHCI, `num-ports = <2>`), so the
    kernel actively drives both channels of `U10`: PD0 enables VBUS
    for `J10` "USB Host A" (USB-A), PD8 enables VBUS for `J3` "USB
    Micro-AB" (USB-B, host/device, per the SoM schematic's
    `HDPB`/`HDMB` labeling). `usb1` (EHCI) has no `atmel,vbus-gpio` of
    its own — it shares the same physical VBUS with `usb0` via the
    OHCI companion-controller model, same pattern as the upstream
    `at91sam9m10g45ek.dts` reference board.
  - Polarity is `GPIO_ACTIVE_LOW` — the switch IC's `EN` pins are
    active-low. Confirmed on real hardware: both USB-A and USB-B work
    correctly.

`configs/pm9g45_test_defconfig` boots a test-only dts,
`board/ronetix/pm9g45/dts/pm9g45-modtest.dts` (see "modtest" above for
why), and autostarts modtest at boot and brings up eth0 via DHCP
automatically — otherwise identical to `pm9g45_defconfig`. The test
dts's own header comment explains why it exists and how it relates to
this board's two dts-modifying patches above (both replicated into it
by hand, since it's built standalone and never goes through the patch
queue).

## sama5d3x-cm

| Component | Source | Version pinned |
|---|---|---|
| Kernel | `linux4microchip/linux`, tag `linux4microchip-2021.10` (Linux 5.10.80) | tarball, via `configs/sama5d3x_cm_defconfig`'s `github()` call |
| U-Boot | `https://github.com/linux4sam/u-boot-at91.git` | branch `u-boot-2024.07-mchp` |
| AT91Bootstrap3 | `https://github.com/ronetix/at91bootstrap.git` | branch `at91bootstrap-3.10.4_rnx`, defconfig `sama5d3x_cm_nf_uboot` (`contrib/board/ronetix/sama5d3x_cm/`) |

- **NAND ECC DT binding**: at kernel 5.10.80, `sama5d3xcm.dtsi` uses
  the generic NAND ECC bindings (`nand-ecc-strength` /
  `nand-ecc-step-size`), not the older Atmel-specific
  `atmel,pmecc-cap` properties. `patches/linux/0001-...patch` sets
  `nand-ecc-strength` from `4` to `8`, matching this SoC's PMECC
  hardware engine — the kernel and U-Boot sides need to agree with
  each other and with what the engine supports, which
  `CONFIG_PMECC_CORRECT_BITS_8=y` in the AT91Bootstrap3 defconfig
  cross-checks.
- **AT91Bootstrap3 board directory is `sama5d3x_cm`, not
  `sama5d3x_cmp`**: `BR2_TARGET_AT91BOOTSTRAP3_DEFCONFIG` points at
  `contrib/board/ronetix/sama5d3x_cm/sama5d3x_cm_nf_uboot_defconfig`.
  `board/sama5d3x_cmp/` (trailing `p`, "CM Plus") is a different,
  unrelated upstream board with a similar name.

Patches (3 kernel + 4 U-Boot):

- `patches/linux/0001-sama5d3xcm.dtsi-change-NAND-ECC-strength-from-4-to-8.patch`
  — see above.
- `../common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
  (shared — see "Shared patches" above).
- `patches/uboot/0001-configs-sama5d3xek_nandflash_defconfig-change-NAND-E.patch`
  — `CONFIG_PMECC_CAP=4` → `=8`, the U-Boot-side counterpart to the
  kernel dtsi change.
- `patches/uboot/0002-sama5d3xek_nandflash_defconfig-add-random-MAC-addres.patch`
  — adds `CONFIG_NET_RANDOM_ETHADDR=y`, `CONFIG_PHY_MICREL=y`,
  `CONFIG_PHY_MICREL_KSZ90X1=y` (the Microchip KSZ9131 PHY is a
  `KSZ90X1`-family part in this driver's naming).
- `patches/uboot/0003-configs-sama5d3xek_nandflash_defconfig-select-UBI-s-.patch`
  — changes the stock bootargs' `ubi.mtd=6` (a numeric MTD index) to
  `ubi.mtd=rootfs` (the mtdparts partition name). A SPI-NOR chip on
  this module probes before the NAND controller and claims global
  mtd0, shifting every NAND partition's index up by one, so a numeric
  index points at the wrong partition depending on probe order; the
  named lookup is correct regardless of what else probes first.
- `patches/linux/0002-sama5d3xmb_gmac.dtsi-use-rgmii-id-phy-mode-for-the-K.patch`
  — changes `phy-mode` from `"rgmii"` to `"rgmii-id"` for the GMAC PHY
  (Microchip KSZ9131). The KSZ9131 driver
  (`ksz9131_config_init()`) needs `rgmii-id` to enable its own
  internal RX+TX delay lines; the stock dtsi's `*-skew-ps` properties
  are specific to the KSZ9021 PHY driver's binding and aren't read by
  the KSZ9131 driver at all. This board has no external PCB-level
  delay compensation (RC-only PHY reset, no reset GPIO), so the
  internal delay lines are required for the RGMII data path to be
  correctly aligned. Confirmed on real hardware.
- `patches/uboot/0004-arch-dts-sama5d3xcm.dtsi-use-rgmii-id-phy-mode-for-t.patch`
  — the same `rgmii-id` fix, on the U-Boot side. U-Boot vendors two
  parallel device-tree source trees for this board
  (`dts/upstream/src/arm/microchip/`, used only when
  `CONFIG_OF_UPSTREAM` is selected, and the older, U-Boot-only
  `arch/arm/dts/`); `sama5d3xek_nandflash_defconfig` does not select
  `CONFIG_OF_UPSTREAM`, so `arch/arm/dts/sama5d3xcm.dtsi` is the file
  that actually matters here. Confirmed on real hardware: `tftp` at
  the U-Boot prompt completes cleanly at `1000Mbps full-duplex`.

  Also worth knowing: this package's `output/build/` directory is
  named `uboot-u-boot-2024.07-mchp`, not `uboot-2024.07-mchp` —
  Buildroot's custom-git naming is `<pkgname>-<CUSTOM_REPO_VERSION>`,
  and the version string itself starts with `u-boot-`, doubling the
  prefix.

**Status: NAND boot, rootfs mount, Linux-side Ethernet, and U-Boot-side
Ethernet all confirmed working on real sama5d3x-cm hardware. modtest's
`sama5d3x_cm_test_defconfig` image, with its GPIO loopback test and
functional checks, confirmed working end-to-end on real hardware.**

## sam9x5-cm

| Component | Source | Version pinned |
|---|---|---|
| Kernel | `https://github.com/linux4microchip/linux.git` | commit `c5ee3b5209256990ae272d97b69103e3289f2190` (same repo/branch as pm9g45; verified built+booted on real AT91SAM9X35 hardware) |
| U-Boot | `https://github.com/linux4sam/u-boot-at91.git` | commit `ffe4012789d5742fdd96c0e238138b1e83e4f460` (`u-boot-2024.07-mchp` branch tip, same commit as pm9g45; verified built+booted on real sam9x5-cm hardware) |
| AT91Bootstrap3 | `https://github.com/ronetix/at91bootstrap.git` | branch `at91bootstrap-3.10.4_rnx`, defconfig `sam9x5_cm_nf_uboot` (`contrib/board/ronetix/sam9x5_cm/`) |

Derived from the wiki
(`https://wiki.ronetix.at/index.php?title=SAM9X5-CM`) plus a real,
already-built reference tree, whose exact
`BR2_TARGET_AT91BOOTSTRAP3_DEFCONFIG` / `BR2_TARGET_UBOOT_BOARD_DEFCONFIG`
values were carried over verbatim.

- **AT91SAM9x5 is a 5-chip family sharing one board** (G15/G25/G35/X25/
  X35, pin-compatible). The kernel image is identical across all five
  — the SoC variant is detected at runtime — but each needs its own
  device tree. `BR2_LINUX_KERNEL_INTREE_DTS_NAME` builds device trees
  for all five so no single physical variant needs to be known up
  front.
- **No runtime DTB auto-selection on this (NAND) boot path**.
  AT91Bootstrap3's `contrib/board/ronetix/sam9x5_cm/sam9x5_cm.c` has an
  `at91_board_set_dtb_name()` function that reads an on-module ID
  EEPROM and picks the matching dtb name automatically, but it's
  compiled only `#ifdef CONFIG_SDCARD`, and this defconfig uses
  `CONFIG_NANDFLASH=y`. U-Boot's `at91sam9x5ek_nandflash_defconfig`
  picks a device tree at build time via `CONFIG_DEFAULT_DEVICE_TREE`;
  real hardware is **X35** (`at91sam9x35ek`), overridden by the patch
  below to match. Linux itself doesn't depend on this: it gets its own
  device tree from a separate NAND `dtb` partition via a raw
  `nand read` in `CONFIG_BOOTCOMMAND`, independent of U-Boot's
  compiled-in default.
- U-Boot patch:
  - `patches/uboot/0001-configs-at91sam9x5ek_nandflash_defconfig-fix-dt-and-mac.patch`
    — two changes to `configs/at91sam9x5ek_nandflash_defconfig`,
    confirmed on real hardware:
    - `CONFIG_DEFAULT_DEVICE_TREE` `"at91sam9g35ek"` →
      `"at91sam9x35ek"`, per the real chip variant above.
    - Adds `CONFIG_NET_RANDOM_ETHADDR=y`, same rationale as the
      sama5d3x-cm counterpart: without it, networking has no MAC
      address unless `ethaddr` is already set in the U-Boot
      environment.
- Kernel patch:
  - `../common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
    (shared — see "Shared patches" above) — needed because this board
    shares pm9g45's kernel repo/branch (`linux4microchip/linux`,
    `linux-6.6-mchp`).
- **`BR2_TARGET_UBOOT_CUSTOM_REPO_VERSION` and
  `BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION` here are floating branch
  names, not pinned commits** (unlike pm9g45/sama5d3x-cm's U-Boot/
  kernel pins). When patching either of this board's U-Boot or kernel
  again, diff against the actual extracted build-directory source
  (`output-*/build/uboot-u-boot-2024.07-mchp/` or
  `output-*/build/linux-<version>/`), not a freshly-cloned copy of the
  branch — Buildroot's download cache is a snapshot from whenever it
  was first fetched, which can be older than the branch's current tip.
  Force a clean re-patch (`uboot-dirclean` / `linux-dirclean`) after
  editing the patch queue rather than relying on Buildroot to notice a
  defconfig change on its own.
