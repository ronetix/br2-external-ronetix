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
  — used by pm9g45, sama5d3x-cm, and sam9x5-cm. All three kernel trees
  (spanning two different kernel versions, 5.10.80 and 6.6.64) carry an
  unmodified, identical `scripts/unifdef.c`, so one patch applies
  cleanly to all of them regardless of which kernel commit/tag each
  board is pinned to — verified directly against both kernel sources
  before wiring this in. Previously carried as three separate
  board-local copies (functionally identical; two were byte-identical,
  the third differed only in commit-message wording and incidental
  trailing whitespace on blank context lines) — deduplicated into this
  single copy.

## Global patches

`../../global-patches/` (i.e. `br2-external-ronetix/global-patches/`,
sibling to `board/`) holds patches for **stock packages that ship with
the upstream `linux4microchip/buildroot-mchp` tree itself** —
`package/<pkgname>/` in the main Buildroot checkout — as opposed to the
custom-fetched kernel/U-Boot/AT91Bootstrap3 sources or `common/`/
per-board patches above, which are all Ronetix-specific and live
entirely inside this external tree. The distinction matters because
this repo's maintainer (unlike the Ronetix board patches, which are
this tree's whole reason to exist) has no way to commit a fix directly
into Microchip's upstream repo — `BR2_GLOBAL_PATCH_DIR` is Buildroot's
built-in mechanism for exactly this: patching any package by name from
an external location, without touching the package's own directory in
the main tree. Each board's defconfig sets
`BR2_GLOBAL_PATCH_DIR="$(BR2_EXTERNAL_RONETIX_PATH)/global-patches"`.

- `global-patches/dtc/0002-fix-discarded-const-qualifiers.patch` — dtc
  1.7.2 (the version pinned by `linux4microchip/buildroot-mchp` at
  `linux4microchip-2026.04`) fails to build under glibc 2.43+ (Ubuntu
  25.10+): newer glibc's type-generic `memchr()`/`strrchr()` overloads
  return a `const`-qualified pointer for a `const` input, and dtc
  builds with `-Werror`, so two pre-existing constness mismatches in
  `libfdt/fdt_overlay.c` and `fdtput.c` become hard build failures
  (`error: assignment discards 'const' qualifier from pointer target
  type [-Werror=discarded-qualifiers]`). Confirmed reproducible on this
  tree's actual build host (Ubuntu, glibc 2.43, GCC 15.2.0) with a
  minimal standalone test before writing this patch, and confirmed
  fixed by rebuilding `host-dtc` clean afterward. Backports the fix
  already applied upstream in dtc v1.8.0 (`github.com/dgibson/dtc`,
  issue #180), adapted to the 1.7.2 tree pinned here (upstream's newer
  `xstrndup()`-based version isn't needed; dtc 1.7.2's existing
  `xstrdup()` helper covers the one call site that needs an owned
  mutable copy). `package/dtc/` already carries an unrelated `0001`
  patch (`fix include guards for older kernel/u-boot`, a real upstream
  Buildroot commit) — this `0002` doesn't touch that file's numbering
  since it lives in a different directory (`global-patches/dtc/`, not
  `package/dtc/`) and Buildroot looks both up independently for the
  same package name.

## pm9g45

| Component | Source | Version pinned |
|---|---|---|
| Kernel | `https://github.com/linux4microchip/linux.git` | commit `c5ee3b5209256990ae272d97b69103e3289f2190` (tip of `linux-6.6-mchp` as of 2026-09-14, verified built+booted) |
| U-Boot | `https://github.com/linux4sam/u-boot-at91.git` | commit `ffe4012789d5742fdd96c0e238138b1e83e4f460` (tip of `u-boot-2024.07-mchp` as of 2026-09-14, verified built+booted) |
| AT91Bootstrap3 | `https://github.com/ronetix/at91bootstrap.git` | branch `at91bootstrap-3.10.4_rnx`, defconfig `pm9g45_nf_uboot` (`contrib/board/ronetix/pm9g45/`) |

Kernel and U-Boot are pinned to specific commits (not floating branch
names) in `configs/pm9g45_defconfig`'s
`BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION` /
`BR2_TARGET_UBOOT_CUSTOM_REPO_VERSION` — Buildroot's git downloader
accepts a raw commit hash there exactly like a branch or tag name
(`support/download/git` just does `git checkout -f -q "${cset}"`), so
this doesn't need a submodule or any other mechanism. To move forward
later: check out the branch, pick a new commit, verify a build, then
update the pinned hash deliberately — don't just drop back to the bare
branch name. AT91Bootstrap3 stays on the branch name, per the same
explicit-fork-reference rationale as sama5d3x-cm (see "Design" in the
project doc / top-level README).

Patches (3, all verified to apply cleanly against the pinned base):

- `patches/linux/0001-dts-microchip-pm9g45.dts-modify-partition-table.patch`
  — changes `arch/arm/boot/dts/microchip/pm9g45.dts`'s NAND partition
  layout from the stock barebox-oriented table to a U-Boot-oriented one
  (u-boot/env/env2/dtb/kernel/rootfs). Extracted unchanged from
  `ronetix/buildroot-mchp`'s `board/ronetix/pm9g45/` (fork branch
  `buildroot-at91-linux4microchip-2024.10`).
- `patches/uboot/0001-configs-pm9g45_defconfig-modify-to-boot-Linux-kernel.patch`
  — changes `CONFIG_BOOTARGS`/`CONFIG_BOOTCOMMAND` in
  `configs/pm9g45_defconfig` to boot a UBIFS rootfs instead of JFFS2.
  Extracted as a real commit (`078bc4f93b`, Ilko Iliev, 2025-01-03) from
  `ronetix/u-boot`'s `u-boot-2024.07-mchp` branch, via
  `git rev-list ronetix/u-boot..linux4sam/u-boot-at91` /
  `git format-patch` against the shared base.
- `../common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
  (shared with sama5d3x-cm and sam9x5-cm — see "Shared patches" below)
  — host-build fix, not a board change: GCC 15 (Ubuntu 24.10+/25.x/26.x)
  makes `constexpr` a reserved C23 keyword, and `scripts/unifdef.c` uses
  it as an ordinary identifier. Confirmed needed at kernel 6.6.64 (the
  pinned commit above). Renames `constexpr` → `constexpression`,
  matching the real upstream kernel fix for this issue.
- `patches/linux/0002-dts-microchip-pm9g45.dts-fix-led0-gpio-add-usb-vbus.patch`
  — two fixes to `pm9g45.dts`, both derived from the actual PM9G45/BB9G45
  schematics (`pm9g45_Variant_Industrial_Schematic.PDF` for the SoM,
  `bb9g45-sch.pdf` for the baseboard), not just guessed from the kernel
  source:
  - `led0`'s gpio was `&pioD 0`. Per the baseboard schematic (sheet 4,
    "MMC, ETH, USB"), PD0 is actually the enable pin for `U10`
    (`AP2142AS`, a TPS2042-compatible dual USB power switch) channel 1,
    which supplies VBUS to `J10` "USB Host A" -- not an LED. Moved to
    `&pioD 30`, which is otherwise unused in this dts.
  - Added `atmel,vbus-gpio = <&pioD 0 GPIO_ACTIVE_LOW &pioD 8
    GPIO_ACTIVE_LOW>;` to `usb0` (OHCI, `num-ports = <2>`), so the kernel
    actively drives both channels of `U10`: PD0 enables VBUS for `J10`
    "USB Host A" (USB-A), PD8 enables VBUS for `J3` "USB Micro-AB"
    (USB-B, host/device, per the SoM schematic's `HDPB`/`HDMB` "HOST/
    DEVICE" labeling). `usb1` (EHCI) has no `atmel,vbus-gpio` of its own
    -- it shares the same physical VBUS with `usb0` via the OHCI
    companion-controller model, same pattern as the upstream
    `at91sam9m10g45ek.dts` reference board.
  - Polarity: `GPIO_ACTIVE_LOW` -- the switch IC's `EN` pins are
    active-low. **Confirmed fixed on real pm9g45 hardware**: both USB-A
    and USB-B work correctly with this patch applied.

pm9g45's kernel/U-Boot pins were already migrated to
`linux4microchip/linux` and (indirectly, via `ronetix/u-boot`, now
replaced here with its stock base) reasonably current sources in the
Ronetix fork prior to this br2-external tree being created — this board
needed the least rework.

## sama5d3x-cm

| Component | Source | Version pinned |
|---|---|---|
| Kernel | `linux4microchip/linux`, tag `linux4microchip-2021.10` (Linux 5.10.80) | tarball, via `configs/sama5d3x_cm_defconfig`'s `github()` call |
| U-Boot | `https://github.com/linux4sam/u-boot-at91.git` | branch `u-boot-2024.07-mchp` |
| AT91Bootstrap3 | `https://github.com/ronetix/at91bootstrap.git` | branch `at91bootstrap-3.10.4_rnx`, defconfig `sama5d3x_cm_nf_uboot` (`contrib/board/ronetix/sama5d3x_cm/`) |

This board needed real rework, not a straight port, because the previous
(pre-migration) tree was still on kernel 4.9.87
(`linux4sam/linux-at91` tag `linux4sam_5.8`) — two major kernel
generations behind pm9g45. Two things changed shape as a result:

- **NAND ECC DT binding.** At 4.9.87, `sama5d3xcm.dtsi` used the
  Atmel-specific properties `atmel,has-pmecc` / `atmel,pmecc-cap` /
  `atmel,pmecc-sector-size`. At 5.10.80 the same node has been converted
  to the generic NAND ECC bindings: `nand-ecc-strength` /
  `nand-ecc-step-size`. The old patch (`pmecc-cap = <4>` → `<8>`) does
  **not** apply — `patches/linux/0001-...patch` in this tree is a new
  patch against the new property name (`nand-ecc-strength = <4>` →
  `<8>`), same intent (8-bit PMECC), verified to apply cleanly at
  `linux4microchip-2021.10`. The 8-bit figure is a property of this
  SoC's own PMECC hardware engine, not something selected per NAND
  chip — the kernel and U-Boot sides just need to agree with each other
  (and with what the engine supports), which
  `CONFIG_PMECC_CORRECT_BITS_8=y` in the AT91Bootstrap3 defconfig below
  already cross-checks.
- **AT91Bootstrap3 board directory: `sama5d3x_cm`, not `sama5d3x_cmp`.**
  `BR2_TARGET_AT91BOOTSTRAP3_DEFCONFIG="sama5d3x_cm_nf_uboot"`, matching
  `contrib/board/ronetix/sama5d3x_cm/sama5d3x_cm_nf_uboot_defconfig`.
  The real board is `contrib/board/ronetix/sama5d3x_cm/` (no trailing
  `p`), alongside `contrib/board/ronetix/pm9g45/` — both genuine Ronetix
  boards live under `contrib/board/ronetix/`, not the mainline `board/`
  tree; `board/sama5d3x_cmp/` (trailing `p`, "CM Plus") is a different,
  unrelated upstream board with a similar name and must not be confused
  with it. `sama5d3x_cm_nf_uboot_defconfig` already sets
  `CONFIG_PMECC_CORRECT_BITS_8=y` directly, consistent with the 8-bit
  PMECC patches carried on the kernel and U-Boot side below — good
  cross-check that the 8-bit setting is a deliberate, real board
  property, not something invented for this tree.

Patches (3 kernel + 4 U-Boot):

- `patches/linux/0001-sama5d3xcm.dtsi-change-NAND-ECC-strength-from-4-to-8.patch`
  — see above.
- `../common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
  (shared with pm9g45 and sam9x5-cm — see "Shared patches" below) —
  same GCC-15 host-build fix as pm9g45; confirmed needed at 5.10.80
  (`scripts/unifdef.c` still declares `static bool constexpr;` at this
  tag).
- `patches/uboot/0001-configs-sama5d3xek_nandflash_defconfig-change-NAND-E.patch`
  — `CONFIG_PMECC_CAP=4` → `=8`, the U-Boot-side counterpart to the
  kernel dtsi change (U-Boot's own NAND driver needs to agree with the
  kernel on ECC strength to read/write the same NAND layout). Real
  commit `10a902b45e`, 2025-02-21.
- `patches/uboot/0002-sama5d3xek_nandflash_defconfig-add-random-MAC-addres.patch`
  — adds `CONFIG_NET_RANDOM_ETHADDR=y`, `CONFIG_PHY_MICREL=y`,
  `CONFIG_PHY_MICREL_KSZ90X1=y`. Real commit `f8af13abb3`, 2025-02-24.
  (The Microchip KSZ9131 PHY itself is a `KSZ90X1`-family part in this
  driver's naming and is supported by this config already; the Linux
  side's KSZ9131 support was separately confirmed present at
  `linux4microchip-2026.04`.)
- `patches/uboot/0003-configs-sama5d3xek_nandflash_defconfig-select-UBI-s-.patch`
  — the stock bootargs hardcode `ubi.mtd=6` (a numeric MTD index,
  assuming atmel_nand's 7 partitions occupy global mtd0..mtd6). On this
  module a SPI-NOR chip (`sama5d3xcm.dtsi`'s `nor` node, an at25df321a)
  probes *before* the NAND controller and claims global mtd0, shifting
  every NAND partition one slot up — so numeric `ubi.mtd=6` ends up
  pointing at the `kernel` partition (raw, not UBI-formatted) instead of
  `rootfs`, and UBI fails to attach (`the layout volume was not found`,
  error -22), which then fails the root mount and panics. Fixed by
  changing `ubi.mtd=6` to `ubi.mtd=rootfs` — UBI's `mtd=` module
  parameter accepts the mtdparts partition **name**, which is correct
  regardless of how many other MTD devices probe ahead of atmel_nand.
  **Confirmed fixed on real hardware.**
- `patches/linux/0002-sama5d3xmb_gmac.dtsi-use-rgmii-id-phy-mode-for-the-K.patch`
  — the GMAC PHY (Microchip KSZ9131) trains link fine
  (`Link is Up - 1Gbps/Full`, correctly auto-detected via MDIO ID) but
  passes **zero** traffic in either direction — the classic
  missing-RGMII-delay symptom: autonegotiation doesn't depend on the
  RGMII data path being correctly aligned, actual frame data does. The
  stock `sama5d3xmb_gmac.dtsi` sets `phy-mode = "rgmii"` and instead
  relies on `*-skew-ps` DT properties — those are specific to the
  KSZ9021 PHY driver's own binding (Atmel's original motherboard PHY)
  and are **not read by the KSZ9131 driver at all**, so they're silently
  ignored. Changed `phy-mode` to `"rgmii-id"`, which makes the KSZ9131
  driver (`ksz9131_config_init()`) enable its own internal RX+TX delay
  lines instead — appropriate for a board with no external PCB-level
  delay compensation, which matches this board (RC-only PHY reset —
  R=100k/C=1uF, ~100ms — no reset GPIO). **Confirmed fixed on real
  hardware** — Ethernet works correctly under Linux with this patch
  applied.
- `patches/uboot/0004-arch-dts-sama5d3xcm.dtsi-use-rgmii-id-phy-mode-for-t.patch`
  — U-Boot vendors *two parallel, independently-built* device-tree
  source trees for this board, and only one of them is actually
  compiled into the binary for a given defconfig:
  - `dts/upstream/src/arm/microchip/` — a newer tree kept in sync with
    the Linux kernel's `arch/arm/boot/dts` layout. Used only when
    `CONFIG_OF_UPSTREAM` is selected.
  - `arch/arm/dts/` — an older, U-Boot-only tree with its own,
    differently-organized board files:
    `sama5d36ek.dts` → `sama5d3xmb.dtsi` → `sama5d3xcm.dtsi` (note:
    *not* `sama5d3xmb_gmac.dtsi` — a different filename in this tree).
    Used when `CONFIG_OF_UPSTREAM` is **not** selected.

  `sama5d3xek_nandflash_defconfig`
  (`CONFIG_DEFAULT_DEVICE_TREE="sama5d36ek"`) does **not** select
  `CONFIG_OF_UPSTREAM`, so the legacy `arch/arm/dts/` tree is the one
  actually compiled for this board — confirmed directly on the built
  output tree (`output/build/uboot-u-boot-2024.07-mchp/`): `.dtb`/
  `.cmd`/`.tmp` build artifacts exist under `arch/arm/dts/` and are
  absent under `dts/upstream/`. `arch/arm/dts/sama5d3xcm.dtsi`'s `macb0`
  node has the exact same `phy-mode = "rgmii"` plus KSZ9021-oriented
  `*-skew-ps` properties as the kernel's `sama5d3xmb_gmac.dtsi`. U-Boot's
  own KSZ9131 driver (`drivers/net/phy/micrel_ksz90x1.c`,
  `ksz9131_config_rgmii_delay()`) behaves identically to the Linux
  driver: for plain `PHY_INTERFACE_MODE_RGMII` it explicitly *disables*
  both internal delay lines (bypasses them) rather than ignoring the
  setting, and the `*-skew-ps` properties are consumed by a completely
  different code path (`ksz9021_config()`/`ksz9031_config()`) that never
  runs for a KSZ9131. Changed `phy-mode` → `"rgmii-id"`, same fix and
  reasoning as the kernel-side patch. **Confirmed fixed on real
  hardware**: `tftp` at the U-Boot prompt completes cleanly
  (`ethernet@f0028000: link up, 1000Mbps full-duplex`, correct byte
  count).

  Note the two device-tree trees when touching U-Boot's DTS for this
  board again in future: a patch against `dts/upstream/` alone has no
  effect on this defconfig's binary — the file that matters is
  `arch/arm/dts/sama5d3xcm.dtsi`.

  Also worth knowing: this package's `output/build/` directory is named
  `uboot-u-boot-2024.07-mchp`, not `uboot-2024.07-mchp` — Buildroot's
  custom-git naming is `<pkgname>-<CUSTOM_REPO_VERSION>`, and the
  version string itself starts with `u-boot-`, doubling the prefix.

All 4 U-Boot patches were verified (`git apply --index`, applied in
sequence, not just individually) against
`linux4sam/u-boot-at91`'s `u-boot-2024.07-mchp` branch tip, and all 3
kernel patches the same way against the actual downloaded kernel source
at `linux4microchip-2021.10` — not just eyeballed. This branch's own
history already contains patches 0001 and 0002 as native commits (they
predate this br2-external tree and were extracted from the branch, not
written against it); only 0003/0004 apply as new changes on top.

**Status as of 2026-09-14: NAND boot, rootfs mount, Linux-side Ethernet,
and U-Boot-side Ethernet all confirmed working on real sama5d3x-cm
hardware.**

## sam9x5-cm

| Component | Source | Version pinned |
|---|---|---|
| Kernel | `https://github.com/linux4microchip/linux.git` | commit `c5ee3b5209256990ae272d97b69103e3289f2190` (same repo/branch as pm9g45; verified built+booted on real AT91SAM9X35 hardware on 2026-09-15 — happens to be the same commit pm9g45 is pinned to, since the branch hadn't moved between the two builds) |
| U-Boot | `https://github.com/linux4sam/u-boot-at91.git` | commit `ffe4012789d5742fdd96c0e238138b1e83e4f460` (the `u-boot-2024.07-mchp` branch tip, same commit as pm9g45; **aligned from sam9x5-cm's previous pin, `f52c0efc94e2f3d3a567de29fbe380a38f9b2847` (tag `linux4microchip-2025.04`, 5 commits behind), on 2026-09-15**, after discovering the two boards had drifted to different commits of the same floating branch — see the "gotcha" note below. **Verified built+booted on real sam9x5-cm hardware at this exact commit on 2026-09-15**, no regressions across the gap) |
| AT91Bootstrap3 | `https://github.com/ronetix/at91bootstrap.git` | branch `at91bootstrap-3.10.4_rnx`, defconfig `sam9x5_cm_nf_uboot` (`contrib/board/ronetix/sam9x5_cm/`) |

Derived from the wiki (`https://wiki.ronetix.at/index.php?title=SAM9X5-CM`)
plus a real, already-built reference: the user's own checkout of
`ronetix/buildroot-mchp` (branch `buildroot-at91-linux4microchip-2024.10`),
built with the plain `at91sam9x5ek_defconfig` and no board-specific
top-level defconfig of its own — that defconfig's exact
`BR2_TARGET_AT91BOOTSTRAP3_DEFCONFIG` / `BR2_TARGET_UBOOT_BOARD_DEFCONFIG`
values were read directly from the built tree and carried over here
verbatim.

- **AT91SAM9x5 is a 5-chip family sharing one board** (G15/G25/G35/X25/
  X35, pin-compatible). The kernel image is identical across all five —
  the SoC variant is detected at runtime — but each needs its own device
  tree. `BR2_LINUX_KERNEL_INTREE_DTS_NAME` builds device trees for all
  five so no single physical variant needs to be known up front, matching
  what the real reference build does (its `output/images/` had all five
  `.dtb` files).
- **No runtime DTB auto-selection on this (NAND) boot path.** AT91Bootstrap3's
  `contrib/board/ronetix/sam9x5_cm/sam9x5_cm.c` has an
  `at91_board_set_dtb_name()` function that reads an on-module ID EEPROM
  and picks the matching dtb name automatically — but it's compiled only
  `#ifdef CONFIG_SDCARD`, and this defconfig uses `CONFIG_NANDFLASH=y`
  (matching the `_nf_uboot` naming convention shared with the other two
  boards), so that function is compiled out here. U-Boot's
  `at91sam9x5ek_nandflash_defconfig` picks a device tree at build time via
  `CONFIG_DEFAULT_DEVICE_TREE`, defaulting upstream to `"at91sam9g35ek"`
  (G35) — real hardware confirmed on 2026-09-15 is **X35**
  (`at91sam9x35ek`), so this is now overridden by the patch below to
  match. (Linux itself doesn't depend on this: it gets its own device
  tree from a separate NAND `dtb` partition via a raw `nand read` in
  `CONFIG_BOOTCOMMAND`, independent of U-Boot's compiled-in default —
  only U-Boot's own operation, `devicetree: separate` in the boot log,
  was affected.)
- **U-Boot: one patch, no PMECC or PHY-driver issue.** Unlike
  sama5d3x-cm, no PMECC or PHY-specific issue has been identified yet for
  this board — the stock `at91sam9x5ek_nandflash_defconfig`
  (`CONFIG_MACB=y`, no specific PHY driver forced, i.e. generic
  autodetect) is used as a base, with two fixes:
  - `patches/uboot/0001-configs-at91sam9x5ek_nandflash_defconfig-fix-dt-and-mac.patch`
    — two changes to `configs/at91sam9x5ek_nandflash_defconfig`, both
    **confirmed on real sam9x5-cm hardware** across two boot logs
    (`uploads/bootlog_sam9x5-cm.txt`, 2026-09-15):
    - `CONFIG_DEFAULT_DEVICE_TREE` `"at91sam9g35ek"` → `"at91sam9x35ek"`,
      per the confirmed real chip variant above. Re-flashed and rebooted
      after this change (`U-Boot 2024.07-linux4microchip-2025.04 (Sep 15
      2026 - 13:17:36 +0200)` in the second log) with no regressions —
      NAND, MMC, network, and both USB ports all still initialize
      correctly.
    - Adds `CONFIG_NET_RANDOM_ETHADDR=y`, same rationale as the
      sama5d3x-cm counterpart: without it, networking has no MAC address
      unless `ethaddr` is already set in the U-Boot environment. Both
      boot logs show a blank/corrupt U-Boot environment (`*** Warning -
      bad CRC, using default environment`) followed by `Warning:
      ethernet@f802c000 (eth0) using random MAC address` — exactly the
      intended fallback — and a successful DHCP lease and ping/network
      use each time.
  - **Gotcha hit while adding this patch**: because
    `BR2_TARGET_UBOOT_CUSTOM_REPO_VERSION` here is the floating branch
    name `u-boot-2024.07-mchp` (not a pinned commit, see table above),
    Buildroot's download cache (`dl/uboot/uboot-u-boot-2024.07-mchp-git*.tar.gz`)
    is a snapshot taken whenever it was first fetched, not the branch's
    current tip. A patch generated against a freshly-cloned copy of the
    branch didn't match that older cached snapshot and failed to apply
    -- silently, from Buildroot's point of view, because a stale
    `.stamp_patched` from an earlier partial build meant the patch step
    didn't even re-run until `uboot-dirclean` forced it. The patch above
    was regenerated against the actual extracted build-directory source
    (`output-*/build/uboot-u-boot-2024.07-mchp/`) to match what really
    gets built. Lesson for future patches on this board (or sam9x5-cm's
    kernel, also unpinned): diff against the real extracted/cached
    source, and force a clean re-patch (`uboot-dirclean` /
    `linux-dirclean`) after editing the patch queue, rather than trusting
    that Buildroot will notice a defconfig change on its own.
- Kernel patches (1):
  - `../common/patches/linux/0001-scripts-unifdef-avoid-constexpr-keyword.patch`
    (shared with pm9g45 and sama5d3x-cm — see "Shared patches" below) —
    same GCC-15 host-build fix as pm9g45/sama5d3x-cm; needed because this
    board shares pm9g45's kernel repo/branch (`linux4microchip/linux`,
    `linux-6.6-mchp`), which has the same `scripts/unifdef.c` issue.
