# br2-external tree: RONETIX

A `BR2_EXTERNAL` tree carrying Ronetix's board support for three AT91-family
boards, layered on top of stock `linux4microchip/buildroot-mchp`
(tested against tag `linux4microchip-2026.04`, Buildroot 2025.02.11):

- **pm9g45** — Ronetix PM9G45 SoM.
- **sama5d3x-cm** — Ronetix SAMA5D3X-CM SoM.
- **sam9x5-cm** — Ronetix SAM9X5-CM SoM (AT91SAM9x5 family: G15/G25/G35/
  X25/X35).

## Layout

```
external.desc               # declares this tree as "RONETIX"
Config.in                   # extra package Kconfig (currently empty)
external.mk                 # extra package rules (currently empty)
configs/
  pm9g45_defconfig
  sama5d3x_cm_defconfig
  sam9x5_cm_defconfig
board/ronetix/
  README.md                 # architecture notes / open items (read this)
  common/patches/linux/*.patch   # patches shared by more than one board
  pm9g45/patches/{linux,uboot}/*.patch
  sama5d3x-cm/patches/{linux,uboot}/*.patch
  sam9x5-cm/patches/uboot/*.patch
global-patches/
  dtc/*.patch                # fixes for stock (non-Ronetix) packages --
                              # see "Global patches" in board/ronetix/README.md
```

## Using it

```
git clone -b linux4microchip-2026.04 https://github.com/linux4microchip/buildroot-mchp.git
git clone https://github.com/ronetix/br2-external-ronetix.git   # this tree

cd buildroot-mchp
make BR2_EXTERNAL=../br2-external-ronetix pm9g45_defconfig
make

# or
make BR2_EXTERNAL=../br2-external-ronetix sama5d3x_cm_defconfig
make

# or
make BR2_EXTERNAL=../br2-external-ronetix sam9x5_cm_defconfig
make
```

(`buildroot-external-microchip`, Microchip's own companion br2-external
tree, is optional — only needed if you want its extra multimedia/crypto/
wireless-kit packages. Buildroot supports stacking more than one
`BR2_EXTERNAL` tree with `BR2_EXTERNAL=path1:path2` if you want both.)

## Design: patch queue vs. fork reference

This tree deliberately treats the three sources differently, matching how
much genuinely board-specific code each one carries:

- **Linux kernel** — small, targeted patches (NAND ECC strength, one dtsi
  partition layout) applied on top of stock `linux4microchip/linux`.
- **U-Boot** — small, targeted patches (2-4 lines each: boot
  command/args, PMECC cap, PHY/MAC options) applied on top of stock
  `linux4sam/u-boot-at91`, **not** a fork. Verified: `ronetix/u-boot`'s
  `u-boot-2024.07-mchp` branch is exactly 3 commits ahead of
  `linux4sam/u-boot-at91`'s `u-boot-2024.07-mchp` — those 3 commits are
  the 3 patches carried here.
- **AT91Bootstrap3** — the Ronetix fork is referenced directly
  (`https://github.com/ronetix/at91bootstrap.git`, branch
  `at91bootstrap-3.10.4_rnx`), not patched. This is where the real
  low-level SoM bring-up (DDR/NAND timing, pin muxing) lives, and it
  doesn't cleanly decompose into small patches against a public upstream
  base the way the kernel/U-Boot deltas do.

This keeps the kernel and U-Boot deltas small, reviewable, and easy to
rebase forward onto a newer upstream tag; it keeps AT91Bootstrap where the
real bring-up work already lives instead of trying to force it into the
same shape.

## Global patches for stock packages

Each board's defconfig sets `BR2_GLOBAL_PATCH_DIR="$(BR2_EXTERNAL_RONETIX_PATH)/global-patches"`.
This is a stock Buildroot mechanism (`BR2_GLOBAL_PATCH_DIR` in the top
menu's Build options) for patching packages that ship with
`linux4microchip/buildroot-mchp` itself -- not a Ronetix board or a
custom-fetched kernel/U-Boot/AT91Bootstrap -- when you can't commit
directly to that upstream repo. Buildroot looks for
`<dir>/<pkgname>/<pkgversion>/*.patch` first, falling back to
`<dir>/<pkgname>/*.patch`, and applies them after the package's own
in-tree patches. See `global-patches/dtc/` and `board/ronetix/README.md`
for the one patch currently carried this way.

See `board/ronetix/README.md` for per-board detail, provenance of every
patch, and open items that need your team's confirmation before this is
production-ready.
