#!/bin/bash
set -e

# =====================================================================
# Ronetix Buildroot build script
#
# Expected directory structure:
#
# buildroot-ronetix/
# ├── buildroot-mchp/
# │   ├── Makefile
# │   ├── Config.in
# │   └── ...
# │
# ├── br2-external-ronetix/
# │   ├── build.sh
# │   ├── Config.in
# │   ├── external.desc
# │   ├── external.mk
# │   ├── configs/
# │   │   ├── sama5d3x_cm_test_defconfig
# │   │   ├── ...
# │   └── board/
# │       └── ronetix/
# │           └── ...
# │
# ├── output-sama5d3x_cm_test/
# ├── output-sama5d3x_cm/
# └── ...
#
# Output directories are generated automatically, one per defconfig.
#
# Usage:
#   ./build.sh
#   ./build.sh sama5d3x_cm_defconfig
#   ./build.sh sama5d3x_cm_test_defconfig
# =====================================================================


# ---------------------------------------------------------------------
# Determine the directory containing this script.
# This allows build.sh to be executed from any working directory.
# ---------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"


# ---------------------------------------------------------------------
# Select the Buildroot defconfig.
# The first command-line argument overrides the default configuration.
# ---------------------------------------------------------------------
DEFCONFIG="${1:-sama5d3x_cm_test_defconfig}"


# ---------------------------------------------------------------------
# Extract the board name by removing the "_defconfig" suffix.
# Example: sama5d3x_cm_test_defconfig -> sama5d3x_cm_test
# ---------------------------------------------------------------------
BOARD="${DEFCONFIG%_defconfig}"


# ---------------------------------------------------------------------
# Configure the BR2_EXTERNAL tree.
# The external tree is located in the same directory as this script.
# ---------------------------------------------------------------------
export BR2_EXTERNAL="$SCRIPT_DIR"


# ---------------------------------------------------------------------
# Define the Buildroot source directory.
# It is located alongside the BR2_EXTERNAL directory.
# ---------------------------------------------------------------------
BUILDROOT_DIR="$(cd "$SCRIPT_DIR/../buildroot-mchp" && pwd)"


# ---------------------------------------------------------------------
# Define a separate output directory for each board configuration.
# Output directories are created alongside the Buildroot source tree.
# Example: output-sama5d3x_cm_test
# ---------------------------------------------------------------------
O="$(dirname "$SCRIPT_DIR")/output-${BOARD}"


# ---------------------------------------------------------------------
# Verify that the Buildroot source directory exists and is valid.
# ---------------------------------------------------------------------
if [ ! -f "$BUILDROOT_DIR/Makefile" ]; then
    echo "Error: Buildroot Makefile not found in $BUILDROOT_DIR"
    exit 1
fi


# ---------------------------------------------------------------------
# Display build configuration and paths.
# ---------------------------------------------------------------------
echo "============================================================"
echo "Ronetix Buildroot Build"
echo "============================================================"
echo "Defconfig:    $DEFCONFIG"
echo "BR2_EXTERNAL: $BR2_EXTERNAL"
echo "Buildroot:    $BUILDROOT_DIR"
echo "Output:       $O"
echo "============================================================"


# ---------------------------------------------------------------------
# Initialize the Buildroot configuration if this output directory
# has not been configured yet.
#
# Existing configurations are preserved on subsequent builds.
# To reapply the defconfig after changing it, run the defconfig
# target manually.
# ---------------------------------------------------------------------
if [ ! -f "$O/.config" ]; then
    echo "Initializing Buildroot configuration: $DEFCONFIG"
    make -C "$BUILDROOT_DIR" O="$O" "$DEFCONFIG"
else
    echo "Using existing Buildroot configuration: $O/.config"
fi


# ---------------------------------------------------------------------
# Build the complete system or execute specific Make targets.
#
# Additional command-line arguments (starting from the second
# argument) are passed directly to Buildroot's make command.
#
# Examples:
#   ./build.sh
#   ./build.sh sama5d3x_prod_defconfig
#   ./build.sh sama5d3x_cm_test_defconfig linux-rebuild
#   ./build.sh sama5d3x_cm_test_defconfig busybox-rebuild
# ---------------------------------------------------------------------
make -C "$BUILDROOT_DIR" O="$O" "${@:2}"

