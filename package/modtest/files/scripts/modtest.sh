#!/bin/sh
#
# modtest - production test runner for Ronetix AT91 SoMs
# (pm9g45, sama5d3x-cm, sam9x5-cm).
#
# Runs each subtest and prints one parseable record per result to stdout.
# Exit code 0 = all pass, 1 = at least one failure.
#
# Every record has the form:
#   TEST <name> <PASS|FAIL|SKIP> <detail>
#
# Deliberately POSIX sh so it runs on a minimal Buildroot image with busybox.
#
# Board-specific defaults are sourced from /etc/modtest/modtest.env if
# present (installed by the modtest package for whichever board's defconfig
# built it) before the hardcoded fallbacks below apply - see
# board/ronetix/README.md's "modtest" section for what each board's env
# file sets and, importantly, what it deliberately leaves unset because it
# hasn't been verified on real hardware yet.

set -u

[ -r /etc/modtest/modtest.env ] && . /etc/modtest/modtest.env

# ---- command-line options --------------------------------------------------
ONLY_GPIO=0

usage() {
	cat <<-EOF
	Usage: $(basename "$0") [-g] [-h]

	  -g   run only the GPIO pair test: exec's straight into
	       "gpiotest -c \$PAIRMAP" (native banner/colors), skipping
	       every other subtest and modtest's own TEST/SUMMARY wrapper
	  -h   show this help and exit
	EOF
}

while getopts "gh" opt; do
	case "$opt" in
		g) ONLY_GPIO=1 ;;
		h) usage; exit 0 ;;
		*) usage >&2; exit 1 ;;
	esac
done
shift $((OPTIND - 1))

# ---- console styling -------------------------------------------------------
# Auto-detected from stdout - so color only turns on for a human running
# this by hand at a real terminal. The systemd/init unit, a pipe, or a
# station script all get byte-identical plain output: every colorized line
# below collapses back to the exact original text when COLOR=0, so the
# "TEST <name> <PASS|FAIL|SKIP> <detail>" contract for automated consumers
# is untouched. Override with MODTEST_COLOR=always|never; NO_COLOR (any
# value, see no-color.org) also forces it off unless MODTEST_COLOR=always
# is set.
COLOR=0
[ -t 1 ] && COLOR=1
[ -n "${NO_COLOR:-}" ] && COLOR=0
case "${MODTEST_COLOR:-auto}" in
	always) COLOR=1 ;;
	never)  COLOR=0 ;;
esac

if [ "$COLOR" -eq 1 ]; then
	c_reset=$(printf '\033[0m')
	c_bold=$(printf '\033[1m')
	c_dim=$(printf '\033[2m')
	c_green=$(printf '\033[32m')
	c_red=$(printf '\033[31m')
	c_yellow=$(printf '\033[33m')
else
	c_reset='' c_bold='' c_dim='' c_green='' c_red='' c_yellow=''
fi

status_color() {
	case "$1" in
		PASS) printf '%s' "$c_green" ;;
		FAIL) printf '%s' "$c_red" ;;
		SKIP) printf '%s' "$c_yellow" ;;
		*)    printf '%s' '' ;;
	esac
}

BAR="------------------------------------------------------------------------"

PAIRMAP=${PAIRMAP:-/etc/modtest/pins.pairs}
ETH_IF=${ETH_IF:-eth0}
MMC_DEV=${MMC_DEV:-}
MTD_PART_LABEL=${MTD_PART_LABEL:-rootfs}
DDR_MB=${DDR_MB:-1}

# -g is an interactive shortcut for re-checking just the connector, not
# another automated station record, so it hands the terminal straight to
# gpiotest instead of going through modtest's own capture-and-flatten
# "TEST gpio ..." path below: exec keeps gpiotest attached to the real
# tty (or pipe) modtest itself was given, so its banner, "===="/"----"
# separators and colors come out exactly as they would running gpiotest
# by hand, and its exit code becomes modtest's exit code as-is.
if [ "$ONLY_GPIO" -eq 1 ]; then
	if [ -r "$PAIRMAP" ]; then
		exec gpiotest -c "$PAIRMAP"
	else
		echo "modtest: no pin map at $PAIRMAP" >&2
		exit 1
	fi
fi

fails=0
RESULTS=""
serial=""
[ -r /sys/devices/soc0/serial_number ] && serial=$(cat /sys/devices/soc0/serial_number 2>/dev/null)
stamp=$(date -u +%Y%m%dT%H%M%SZ)

finish() {
	exit "$1"
}

record() {
	name=$1; result=$2; shift 2
	detail=$*
	RESULTS="$RESULTS
$name|$result|$detail"
	col=$(status_color "$result")
	printf '%sTEST%s %s %s%s%s %s\n' "$c_dim" "$c_reset" "$name" "$col" "$result" "$c_reset" "$detail"
	[ "$result" = FAIL ] && fails=$((fails + 1))
	return 0
}

if [ -n "$serial" ]; then
	printf '%sMODTEST start%s serial=%s time=%s\n' "$c_bold" "$c_reset" "$serial" "$stamp"
else
	printf '%sMODTEST start%s time=%s\n' "$c_bold" "$c_reset" "$stamp"
fi
printf '%sINFO%s kernel=%s machine=%s\n' "$c_dim" "$c_reset" "$(uname -r)" "$(uname -m)"

# ---------------------------------------------------------------- CPU / SoC
# Each of these boards' kernels reports a distinct machine string, and
# Atmel's own SoC bus driver (drivers/soc/atmel/soc.c) populates
# /sys/devices/soc0/* on AT91SAM9/SAMA5, so a plain read of that is enough
# to identify the SoC.
soc_name() {
	fam=""
	[ -r /sys/devices/soc0/family ] && fam=$(cat /sys/devices/soc0/family 2>/dev/null)
	if [ -n "$fam" ]; then
		echo "$fam"
		return
	fi

	if [ -r /proc/device-tree/model ]; then
		tr -d '\0' < /proc/device-tree/model 2>/dev/null
		return
	fi

	echo "unknown SoC"
}

if grep -qi "ARM926EJ-S\|Cortex-A5\|0x926\|0xc05" /proc/cpuinfo 2>/dev/null; then
	record cpu PASS "$(soc_name)"
else
	record cpu FAIL "unexpected /proc/cpuinfo ($(soc_name))"
fi

# ---------------------------------------------------------------- DDR
# Size as reported by the kernel, then a real read/write pass over a chunk of
# it.  memtester is the tool of choice; fall back to dd if it is not installed.
# /proc/meminfo's MemTotal is already KiB; MiB (binary) is what matches how
# DDR capacity is specified (128MB, 256MB, ...), unlike eMMC/NAND's decimal
# GB/MB below.
mem_kb=$(awk '/MemTotal/{print $2}' /proc/meminfo)
mem_mb=$(awk -v kb="$mem_kb" 'BEGIN { printf "%.0f", kb / 1024 }')
record ddr_size PASS "${mem_mb}MB"

if command -v memtester >/dev/null 2>&1; then
	if memtester "${DDR_MB}M" 1 >/tmp/memtester.out 2>&1; then
		record ddr_pattern PASS "${DDR_MB}MB"
	else
		record ddr_pattern FAIL "see /tmp/memtester.out"
	fi
else
	record ddr_pattern SKIP "memtester not installed"
fi

# ---------------------------------------------------------------- NAND
# All three boards (pm9g45, sama5d3x-cm, sam9x5-cm) boot from raw NAND with
# a UBI/UBIFS rootfs, not eMMC - so this checks for the rootfs MTD
# partition by NAME, not a hardcoded device index: a numeric index breaks
# silently the moment anything else on the board probes an MTD device
# before atmel_nand does, since that shifts every later partition's
# global index up. Looking up by the mtdparts label sidesteps that
# entirely, regardless of probe order.
if [ -r /proc/mtd ]; then
	mtd_line=$(grep "\"${MTD_PART_LABEL}\"" /proc/mtd 2>/dev/null)
	if [ -n "$mtd_line" ]; then
		record nand PASS "$mtd_line"
	else
		record nand FAIL "no MTD partition labeled '$MTD_PART_LABEL'"
	fi
else
	record nand FAIL "/proc/mtd missing"
fi

# ---------------------------------------------------------------- MMC / SD
# MMC_DEV is unset by default (see the per-board env files under
# /etc/modtest/modtest.env) on any board or carrier with no MMC/SD to
# test. Rather than record a SKIP for a check that isn't meaningful
# there, an unset MMC_DEV is silently not checked at all - no TEST line
# at all, not even SKIP.
if [ -z "$MMC_DEV" ]; then
	:
elif [ -b "$MMC_DEV" ]; then
	size=$(blockdev --getsize64 "$MMC_DEV" 2>/dev/null)
	if [ -n "$size" ] && [ "$size" -gt 0 ]; then
		# Decimal GB (1000^3), matching how eMMC/SD parts are
		# marketed/labeled, not the binary GiB a kernel MemTotal-style
		# figure would use.
		size_gb=$(awk -v b="$size" 'BEGIN { printf "%.2f", b / 1000 / 1000 / 1000 }')
		if dd if="$MMC_DEV" of=/dev/null bs=1M count=32 2>/dev/null; then
			record mmc PASS "size=${size_gb}GB"
		else
			record mmc FAIL "read error"
		fi
	else
		record mmc FAIL "no size"
	fi
else
	record mmc FAIL "$MMC_DEV missing"
fi

# ---------------------------------------------------------------- Ethernet
# Link up is not enough on its own - require an actual, DHCP-leased IPv4
# address too. If none is already assigned (e.g. no networking service has
# run yet), make one bounded DHCP attempt with udhcpc before giving up.
#
# A 169.254.0.0/16 address is the kernel/networking-stack's own zeroconf
# fallback, handed out when DHCP never got a reply - it is not a lease, so
# it must not read as success just because it's a non-empty address.
ETH_DHCP_TIMEOUT=${ETH_DHCP_TIMEOUT:-3}
ETH_DHCP_RETRIES=${ETH_DHCP_RETRIES:-3}

# How long to wait for the link itself to come up before giving up.
# Gigabit autonegotiation (e.g. this board's KSZ9131) doesn't always
# finish within a short fixed delay, so this polls operstate instead of
# sleeping a fixed amount: a link that's already up (or comes up fast)
# returns immediately, and a slow one gets up to ETH_LINK_TIMEOUT
# seconds instead of failing on a race against an arbitrary sleep.
ETH_LINK_TIMEOUT=${ETH_LINK_TIMEOUT:-8}

eth_ipv4() {
	ip -4 -o addr show dev "$1" 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n1
}

eth_is_link_local() {
	case "$1" in
		169.254.*) return 0 ;;
		*) return 1 ;;
	esac
}

if [ -d "/sys/class/net/$ETH_IF" ]; then
	ip link set "$ETH_IF" up 2>/dev/null
	waited=0
	while :; do
		link=$(cat "/sys/class/net/$ETH_IF/operstate" 2>/dev/null)
		[ "$link" = up ] && break
		[ "$waited" -ge "$ETH_LINK_TIMEOUT" ] && break
		sleep 1
		waited=$((waited + 1))
	done
	speed=""
	if command -v ethtool >/dev/null 2>&1; then
		speed=$(ethtool "$ETH_IF" 2>/dev/null | awk '/Speed/{print $2}')
	fi

	ip4=""
	ip4_src="none"
	if [ "$link" = up ]; then
		ip4=$(eth_ipv4 "$ETH_IF")
		[ -n "$ip4" ] && ip4_src="existing"
		# Retry via DHCP if there's no address yet, or what's there is only
		# the zeroconf fallback - a real lease should replace it.
		if { [ -z "$ip4" ] || eth_is_link_local "$ip4"; } && command -v udhcpc >/dev/null 2>&1; then
			udhcpc -i "$ETH_IF" -n -q -t "$ETH_DHCP_RETRIES" -T "$ETH_DHCP_TIMEOUT" >/dev/null 2>&1
			new_ip4=$(eth_ipv4 "$ETH_IF")
			if [ -n "$new_ip4" ]; then
				ip4="$new_ip4"
				ip4_src="dhcp"
			fi
		fi
	fi

	if [ "$link" = up ] && [ -n "$ip4" ] && eth_is_link_local "$ip4"; then
		record eth FAIL "link=up no DHCP lease, link-local ip=$ip4"
	elif [ "$link" = up ] && [ -n "$ip4" ]; then
		record eth PASS "speed=${speed:-?} ip=$ip4 src=$ip4_src"
	elif [ "$link" = up ]; then
		record eth FAIL "link=up no IP address"
	else
		record eth FAIL "link=$link"
	fi
else
	record eth FAIL "$ETH_IF missing"
fi

# ---------------------------------------------------------------- I2C
# Bus-presence check: a bus is OK as soon as ONE device answers on it -
# this is not a per-address inventory, just "is the bus alive at all".
#
# I2C_BUSES is empty by default - unlike ETH_IF/MTD_PART_LABEL above,
# which of a board's I2C buses actually has a device on it depends on the
# specific production test carrier, which hasn't been characterized on
# real hardware yet for any of these three boards. Set it in
# /etc/modtest/modtest.env (or the environment) once known; until then
# this reports SKIP rather than guessing a bus number that might not
# exist, or worse, silently PASSing against the wrong bus.
#
# I2C_BUSES=0 is a different state from unset: it means a carrier has
# been confirmed to have no I2C device to test at all (not "not yet
# characterized" but "definitely none"), so there's nothing to configure
# and nothing worth reporting - no TEST line at all, not even SKIP.
I2C_BUSES=${I2C_BUSES:-}

i2c_bus_has_device() {
	bus=$1

	for dev in /sys/bus/i2c/devices/"$bus"-*; do
		[ -e "$dev" ] && return 0
	done

	command -v i2cdetect >/dev/null 2>&1 || return 1
	# -r: read-byte probing, safer on controllers that misbehave on the
	# default zero-length "quick write" probe against reserved addresses.
	i2cdetect -y -r "$bus" 2>/dev/null | awk '
		NR == 1 { next }
		{ for (i = 2; i <= NF; i++) if ($i != "--") found = 1 }
		END { exit !found }
	'
}

if [ -z "$I2C_BUSES" ]; then
	record i2c SKIP "I2C_BUSES not configured for this board"
elif [ "$I2C_BUSES" = "0" ]; then
	:
else
	for bus in $I2C_BUSES; do
		if [ ! -e "/sys/bus/i2c/devices/i2c-$bus" ]; then
			record "i2c${bus}" FAIL "adapter i2c-$bus not present"
			continue
		fi
		if i2c_bus_has_device "$bus"; then
			record "i2c${bus}" PASS "device detected"
		else
			record "i2c${bus}" FAIL "no device detected"
		fi
	done
fi

# ---------------------------------------------------------------- USB storage
# USB_STORAGE_COUNT defaults to 0, meaning "not configured": rather than
# fail against a guessed fixture-specific count, this just reports how
# many USB-attached block devices it finds as INFO and does not
# PASS/FAIL on the count. Set USB_STORAGE_COUNT to a real number, once a
# fixture's exact expected count is known, to turn this into a real
# pass/fail check.
#
# A device counts as USB storage when its sysfs realpath runs through a
# usbN node - that's the block layer's own view, so it also covers a
# device bound via some driver path other than usb-storage/uas.
USB_STORAGE_COUNT=${USB_STORAGE_COUNT:-0}

usb_storage_list() {
	for dev in /sys/block/sd* /sys/block/sr*; do
		[ -e "$dev" ] || continue
		case "$(readlink -f "$dev")" in
			*/usb[0-9]*/*) echo "${dev##*/}" ;;
		esac
	done
}

usb_devs=$(usb_storage_list)
usb_n=0
[ -n "$usb_devs" ] && usb_n=$(printf '%s\n' "$usb_devs" | wc -l)
usb_names=$(printf '%s' "$usb_devs" | tr '\n' ',' | sed 's/,$//')
usb_names=${usb_names:-none}

if [ "$USB_STORAGE_COUNT" -eq 0 ]; then
	record usb PASS "count=$usb_n devices=$usb_names (USB_STORAGE_COUNT not configured, not enforced)"
elif [ "$usb_n" -eq "$USB_STORAGE_COUNT" ]; then
	record usb PASS "count=$usb_n devices=$usb_names"
else
	record usb FAIL "expected $USB_STORAGE_COUNT, found $usb_n devices=$usb_names"
fi

# ---------------------------------------------------------------- GPIO pairs
# Reached only for a full run (-g already exec'd gpiotest and exited
# above). No pin map is shipped by default for any of these three boards -
# the production test carrier's shorted-pair mapping hasn't been defined
# yet (see board/ronetix/README.md's "modtest" section) - so this SKIPs
# until a real pins.pairs is installed at /etc/modtest/pins.pairs (or
# PAIRMAP points elsewhere).
#
# Captured rather than let stream live: piping it also puts gpiotest's own
# tty auto-detection onto a non-terminal, so its "----"/"====" rules and
# banner stay off regardless of whether modtest itself is running at a
# console - and it lets the one-line "TEST gpio ..." record land right
# after the other TEST lines, with gpiotest's own detail (PAIR/FAIL lines
# on a failure, its RESULT line always) printed after that, not before it.
# That flattened, always-plain form is what keeps the automated
# "TEST <name> <PASS|FAIL|SKIP> <detail>" contract byte-identical
# regardless of console vs. station script.
if [ -r "$PAIRMAP" ]; then
	gpio_out=$(gpiotest -q -c "$PAIRMAP" 2>&1)
	gpio_rc=$?
	if [ "$gpio_rc" -eq 0 ]; then
		record gpio PASS "$PAIRMAP"
	else
		record gpio FAIL "see detail below"
	fi
	[ -n "$gpio_out" ] && printf '%s\n' "$gpio_out"
else
	record gpio SKIP "no pin map at $PAIRMAP"
fi

# ---------------------------------------------------------------- Verdict
# Recap table - purely a visual extra for a human at the console, so it
# only appears when COLOR is on; nothing is added to the automated/log
# output path.
if [ "$COLOR" -eq 1 ]; then
	printf '\n%s%s%s\n' "$c_dim" "$BAR" "$c_reset"
	printf '%s\n' "$RESULTS" | while IFS='|' read -r rname rresult rdetail; do
		[ -z "$rname" ] && continue
		col=$(status_color "$rresult")
		printf '  %s%-16s%s %s%-4s%s  %s\n' "$c_bold" "$rname" "$c_reset" "$col" "$rresult" "$c_reset" "$rdetail"
	done
	printf '%s%s%s\n' "$c_dim" "$BAR" "$c_reset"
fi

if [ -n "$serial" ]; then
	printf '%sSUMMARY%s serial=%s failed=%s\n' "$c_bold" "$c_reset" "$serial" "$fails"
else
	printf '%sSUMMARY%s failed=%s\n' "$c_bold" "$c_reset" "$fails"
fi

show_result() {
	res=$1
	if [ "$res" = PASS ]; then bc=$c_green; else bc=$c_red; fi
	if [ "$COLOR" -eq 1 ]; then
		printf '%s%s%s\n' "$bc" "$BAR" "$c_reset"
		printf '%s%sRESULT %s%s\n' "$bc" "$c_bold" "$res" "$c_reset"
		printf '%s%s%s\n' "$bc" "$BAR" "$c_reset"
	else
		echo "RESULT $res"
	fi
}

if [ "$fails" -eq 0 ]; then
	show_result PASS
	finish 0
fi
show_result FAIL
finish 1
