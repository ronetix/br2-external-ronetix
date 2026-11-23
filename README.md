# RONETIX – Buildroot External Tree

**Board support, custom configurations, and production test tools for Ronetix AT91-based System-on-Modules.**

This repository provides a `BR2_EXTERNAL` tree for integrating Ronetix hardware support into the official [Microchip Buildroot distribution](https://github.com/linux4microchip/buildroot-mchp).

It currently supports three Ronetix AT91-family SoMs, using the stock `linux4microchip/buildroot-mchp` tree as its foundation.

| Board | Description | Processor Family |
|:---|:---|:---|
| **PM9G45** | Ronetix PM9G45 System-on-Module | AT91SAM9G45 |
| **SAMA5D3X-CM** | Ronetix SAMA5D3X-CM System-on-Module | SAMA5D3 |
| **SAM9X5-CM** | Ronetix SAM9X5-CM System-on-Module | AT91SAM9X5 (G15/G25/G35/X25/X35) |

### Compatibility

| Component | Version |
|:---|:---|
| Microchip Buildroot | `linux4microchip-2026.04` |
| Buildroot | `2025.02.11` |

---

## ✨ Features

- **Three supported Ronetix SoMs** – dedicated Buildroot configurations for each platform.
- **Upstream-based kernel support** – minimal, targeted patches on top of Microchip's Linux kernel.
- **Upstream-based U-Boot support** – small, maintainable patches instead of a separate bootloader fork.
- **Ronetix AT91Bootstrap** – direct integration of the Ronetix fork containing platform-specific low-level initialization.
- **Production test images** – optional `modtest` package for automated hardware functional testing.
- **Global Buildroot patches** – isolated fixes for packages provided by the stock Microchip distribution.

---

## 📁 Repository Structure

```text
br2-external-ronetix/
│
├── external.desc
├── Config.in
├── external.mk
│
├── configs/
│   ├── pm9g45_defconfig
│   ├── pm9g45_test_defconfig
│   ├── sama5d3x_cm_defconfig
│   ├── sama5d3x_cm_test_defconfig
│   └── sam9x5_cm_defconfig
│
├── board/
│   └── ronetix/
│       ├── README.md
│       │
│       ├── common/
│       │   └── patches/linux/
│       │
│       ├── pm9g45/
│       │   └── patches/
│       │       ├── linux/
│       │       └── uboot/
│       │
│       ├── sama5d3x-cm/
│       │   ├── patches/
│       │   │   ├── linux/
│       │   │   └── uboot/
│       │   └── dts/
│       │       └── sama5d35ek-modtest.dts
│       │
│       └── sam9x5-cm/
│           └── patches/uboot/
│
├── global-patches/
│   └── dtc/
│       └── *.patch
│
└── package/
    └── modtest/
        ├── Config.in
        ├── modtest.mk
        └── files/
```

### Directory Overview

| Directory | Purpose |
|:---|:---|
| `configs/` | Buildroot defconfig files for production and test images |
| `board/ronetix/` | Board-specific configurations, patches, and device trees |
| `board/ronetix/common/` | Linux kernel patches shared between multiple boards |
| `global-patches/` | Fixes for packages included in the stock Microchip Buildroot distribution |
| `package/modtest/` | Ronetix production hardware test application |

For additional technical information, patch provenance, and outstanding issues, see **[`board/ronetix/README.md`](board/ronetix/README.md)**.

---

## 🚀 Getting Started

### 1. Clone the repositories

Start with the tested Microchip Buildroot release:

```bash
git clone -b linux4microchip-2026.04 \
    https://github.com/linux4microchip/buildroot-mchp.git

git clone https://github.com/ronetix/br2-external-ronetix.git
```

Expected directory layout:

```text
workspace/
├── buildroot-mchp/
└── br2-external-ronetix/
```

### 2. Build a production image

Select the desired board configuration, create an output directory, and start the build.

**PM9G45**

```bash
cd buildroot-mchp

make BR2_EXTERNAL=../br2-external-ronetix \
    pm9g45_defconfig O=../output-pm9g45

make O=../output-pm9g45
```

**SAMA5D3X-CM**

```bash
make BR2_EXTERNAL=../br2-external-ronetix \
    sama5d3x_cm_defconfig O=../output-sama5d3x-cm

make O=../output-sama5d3x-cm
```

**SAM9X5-CM**

```bash
make BR2_EXTERNAL=../br2-external-ronetix \
    sam9x5_cm_defconfig O=../output-sam9x5-cm

make O=../output-sam9x5-cm
```

### 3. Build production test images

Two configurations include the Ronetix `modtest` package, which provides hardware functional tests and automatic execution at boot.

**PM9G45 – Production Test Image**

```bash
make BR2_EXTERNAL=../br2-external-ronetix \
    pm9g45_test_defconfig O=../output-pm9g45-test

make O=../output-pm9g45-test
```

**SAMA5D3X-CM – Production Test Image**

```bash
make BR2_EXTERNAL=../br2-external-ronetix \
    sama5d3x_cm_test_defconfig O=../output-sama5d3x-cm-test

make O=../output-sama5d3x-cm-test
```

The SAMA5D3X-CM test image additionally uses a dedicated test-only device tree.

> **Note:** The `modtest` application is automatically started during boot. See the [modtest documentation](board/ronetix/README.md#modtest) for details.

---

## 🏗️ Design Philosophy

### Upstream First – Minimal Patches, Maximum Maintainability

The repository deliberately uses different integration strategies for the Linux kernel, U-Boot, and AT91Bootstrap3.

The goal is to keep board-specific changes small, transparent, and easy to maintain across upstream releases.

#### Linux Kernel

Based on the stock Microchip Linux kernel:

`linux4microchip/linux`

Only a small number of targeted patches are maintained, covering board-specific requirements such as:

- NAND ECC configuration.
- Device tree partition layouts.

#### U-Boot

Based on the stock AT91 U-Boot repository:

`linux4sam/u-boot-at91`

Ronetix-specific changes are maintained as small patches rather than a separate fork.

Typical changes include:

- Boot commands and boot arguments.
- PMECC configuration.
- Ethernet PHY and MAC options.

**Why patches instead of a fork?**

A comparison of the `u-boot-2024.07-mchp` branches confirmed that the Ronetix fork contains exactly three additional commits compared with the corresponding upstream branch.

These three commits are represented by the patches maintained in this repository.

This approach makes future U-Boot upgrades easier and avoids maintaining an unnecessary long-term fork.

#### AT91Bootstrap3

Unlike the kernel and U-Boot, AT91Bootstrap3 is referenced directly from the Ronetix repository:

- Repository: `https://github.com/ronetix/at91bootstrap.git`
- Branch: `at91bootstrap-3.10.4_rnx`

The Ronetix fork contains the actual low-level hardware initialization required by the SoMs, including:

- DDR initialization and timing.
- NAND initialization and timing.
- Pin multiplexing.
- Platform-specific hardware bring-up.

These changes are fundamental to the hardware platform and do not lend themselves to the same small-patch approach used for Linux and U-Boot.

**Summary**

| Component | Integration Strategy | Rationale |
|:---|:---|:---|
| Linux | Small patch series | Minimal board-specific changes |
| U-Boot | Small patch series | Avoid maintaining a separate fork |
| AT91Bootstrap3 | Ronetix Git fork | Contains fundamental low-level hardware initialization |

---

## 🔧 Global Patches

Buildroot provides the `BR2_GLOBAL_PATCH_DIR` mechanism to apply additional patches to packages included in the Buildroot distribution.

Each Ronetix defconfig sets:

```makefile
BR2_GLOBAL_PATCH_DIR="$(BR2_EXTERNAL_RONETIX_PATH)/global-patches"
```

This mechanism is used for modifications to stock packages provided by `linux4microchip/buildroot-mchp` that cannot be committed directly to the upstream repository.

Buildroot searches for patches in the following order:

```text
<global-patch-dir>/<package>/<package-version>/*.patch
<global-patch-dir>/<package>/*.patch
```

These patches are applied after the package's own in-tree patches.

The current repository includes a patch for `dtc` (Device Tree Compiler).

See [`global-patches/`](global-patches/) and the [technical documentation](board/ronetix/README.md#global-patches) for details.

---

## 📦 Microchip External Packages (Optional)

Microchip maintains its own companion external tree:

`buildroot-external-microchip`

This repository is **not required** for the standard Ronetix board configurations.

It is only needed if you want additional Microchip packages, such as multimedia, cryptography, or wireless-kit components.

Buildroot supports multiple external trees simultaneously:

```bash
make BR2_EXTERNAL=../buildroot-external-microchip:../br2-external-ronetix \
    pm9g45_defconfig O=../output-pm9g45
```

This allows Ronetix board support and Microchip's additional packages to coexist without merging their repositories.

---

## 📚 Further Documentation

The following document contains additional implementation details:

**[`board/ronetix/README.md`](board/ronetix/README.md)**

It covers:

- Per-board hardware and software details.
- Kernel and U-Boot patch descriptions.
- Patch provenance and upstream references.
- AT91Bootstrap integration.
- Production test application (`modtest`).
- Global patches.
- Known limitations and outstanding items.

> **Production readiness:** The technical documentation also identifies open items requiring confirmation by the Ronetix engineering team before the configurations can be considered production-ready.

---

## 🏢 About Ronetix

**Ronetix GmbH** develops embedded hardware and software solutions, including ARM-based System-on-Modules, development platforms, and embedded debugging and programming tools.

🌐 [www.ronetix.at](https://ronetix.at)

