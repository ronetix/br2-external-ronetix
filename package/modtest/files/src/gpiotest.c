/*
 * gpiotest - GPIO loopback pair test for module production testing.
 *
 * Assumes a test carrier that shorts the module's GPIO pins together in
 * pairs.  For every pin, drive it high then low in turn while every other
 * pin sits as an input and check who follows. By default that input floats
 * (no bias requested); mark a pair 'intbias=up' or 'intbias=down' in the
 * pin map to have the tool actively request that pull from the gpiochip
 * driver instead (works on native pinctrl and PCAL-series I2C expanders).
 *
 * A real wired connection makes the partner follow the driven level on both
 * polarities, regardless of bias. A net with its own pull - a board
 * resistor ('extbias') or a software-requested one ('intbias') - reads
 * that pull's level on the phase it dominates; the check is skipped on
 * that phase for that pin and relied on only for the opposite phase, where
 * an active driver can out-fight the pull. A pin with neither and no
 * connection is genuinely undefined while floating - not evaluated by
 * design here beyond "does it happen to match the driven level", which is
 * why every net that matters should be biased one way or the other if
 * reliable open detection is needed for it.
 *
 * That sweep catches opens (partner does not follow) and shorts between any
 * two pins (a third pin follows).  There is no idle/stuck-pin check - it
 * would need a controlled, known-good bias to have an expected value to
 * compare against, which floating pins don't have.
 *
 * Output is one record per line, easy to parse from a test station:
 *
 *   INFO   api=2 pins=168 pairs=84 settle=500us
 *   FAIL   CN1_16 open partner=CN1_18
 *   FAIL   CN1_20 short-to=CN1_44 phase=high
 *   PAIR   CN1_12 CN1_14 PASS
 *   SUMMARY pins=168 pairs=84 failed=2
 *   RESULT FAIL
 *
 * Exit code 0 = all pass, 1 = at least one failure, 2 = setup error.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpio_compat.h"

#define MAX_PINS 512
#define CONSUMER_DEFAULT "gpiotest"

#ifdef REAL_BUILD_DATE
    const char *build_date = REAL_BUILD_DATE;
    const char *build_time = REAL_BUILD_TIME;
#else
    const char *build_date = __DATE__;
    const char *build_time = __TIME__;
#endif

enum ext_bias {
	EXTB_NONE = 0,
	EXTB_UP,	/* net has its own external pull-up */
	EXTB_DOWN,	/* net has its own external pull-down */
};

struct pin {
	char		label[32];
	char		chip[32];
	unsigned int	offset;
	int		partner;	/* index into pins[] */
	gt_line		*line;
	int		failed;
	enum ext_bias	ext_bias;	/* direction, and which phase's peer
					   check to skip - see phase_drive().
					   Set by 'extbias=' (board resistor,
					   software stays hands-off) or
					   'intbias=' (software actively
					   requests this pull, see below) */
	int		request_bias;	/* 1 for intbias=: actually request
					   ext_bias's direction via the
					   gpiochip driver's own bias support
					   (native pinctrl or PCAL-series I2C
					   expanders), instead of floating and
					   trusting a board resistor. */
	long		release_settle_us;	/* extra time this pin's own
						   level needs to decay after
						   being driven, beyond the
						   normal -s settle. 0 = none. */
	struct timespec	released_at;
	int		has_released;
	int		no_drive;	/* 1 for 'nodrive=': never make this pin
					   the driver. It is still requested,
					   still floated with its resting bias
					   between steps, and still read as a
					   peer whenever its partner drives -
					   only its own turn as the active
					   driver in phase_drive() is skipped.
					   For a pin sitting on a fixed-enable
					   (OE tied high) auto-direction level
					   translator that has latched onto its
					   OTHER port as the permanent driver:
					   driving this side does not relay
					   through the translator at all - not
					   eventually, not with more precharge,
					   not with more settle - so testing
					   that direction only ever produces a
					   false 'open'. The reverse direction
					   (drive the partner, this pin follows)
					   still works and is the only one this
					   pin is tested in. */
};

static struct pin pins[MAX_PINS];
static int npins;
static int npairs;

static int opt_settle_us = 500;
static int opt_verbose;
static int opt_quiet;
static const char *opt_consumer = CONSUMER_DEFAULT;

/*
 * Pre-charge time, microseconds. 0 (default) = off.
 *
 * When set, a pin that has just been driven is actively driven to its
 * RESTING level (the direction its extbias/intbias marking gives) for this
 * long before being released, so the net is left sitting where it belongs
 * instead of at the level it was just driven to. Two things need this:
 *
 *   - A net with no effective bias at all (no board resistor, and a pin
 *     controller whose bias request silently does nothing) holds the
 *     driven level on its own capacitance more or less indefinitely after
 *     release. No settle time fixes that - there is nothing pulling it
 *     back.
 *   - An auto-direction-sensing level translator (NXS/NXB, TXS/TXB and
 *     friends) sitting on the net can latch: having decided which side is
 *     driving, it keeps actively holding that level, and a weak pull-up
 *     cannot out-fight its push-pull output stage. Driving the net to the
 *     opposite level knocks the direction sensing out of that state.
 *
 * Either way the symptom is the same and is easy to misread: the stuck pin
 * is reported as 'short-to' by every pin driven AFTER it in the sweep,
 * pointing at innocent pins instead of the one that failed to recover.
 * See the post-release check in phase_drive(), which names the real
 * culprit directly.
 *
 * This briefly puts this pin's driver in contention with whatever else is
 * holding the net, so keep it short - a few hundred microseconds is
 * plenty. It does NOT mask a hard short: a pin shorted to a rail returns
 * to that rail as soon as the pre-charge ends and is still caught.
 *
 * On by default at 200us - cheap insurance against the latch/no-pull
 * failure mode above on any fixture, not just ones already known to need
 * it. Override with -p (0 disables it).
 */
static int opt_precharge_us = 200;

/*
 * Colors are on automatically when stdout is a terminal, off when piped -
 * so a test station or modtest.sh parsing the records sees the exact plain
 * byte format, while a human at the console gets the readable version.
 * --color / --no-color force it either way.
 */
static int use_color = -1;		/* -1 = auto (isatty) */

#define C_RED	"\033[1;31m"
#define C_GRN	"\033[1;32m"
#define C_YEL	"\033[1;33m"
#define C_CYN	"\033[1;36m"
#define C_BLD	"\033[1m"
#define C_DIM	"\033[2m"
#define C_RST	"\033[0m"

static const char *col(const char *c)
{
	return use_color ? c : "";
}

static void print_rule(char ch)
{
	int i;

	if (!use_color) {
		return;		/* decoration only - keep piped output plain */
	}
	for (i = 0; i < 58; i++) {
		putchar(ch);
	}
	putchar('\n');
}

static int find_pin(const char *label)
{
	int i;

	for (i = 0; i < npins; i++) {
		if (strcmp(pins[i].label, label) == 0) {
			return i;
		}
	}
	return -1;
}

static int add_pin(const char *label, const char *chip, unsigned int offset)
{
	if (npins >= MAX_PINS) {
		fprintf(stderr, "too many pins (max %d)\n", MAX_PINS);
		return -1;
	}
	if (find_pin(label) >= 0) {
		fprintf(stderr, "duplicate pin label '%s'\n", label);
		return -1;
	}

	snprintf(pins[npins].label, sizeof(pins[npins].label), "%s", label);
	snprintf(pins[npins].chip, sizeof(pins[npins].chip), "%s", chip);
	pins[npins].offset = offset;
	pins[npins].partner = -1;
	return npins++;
}

/*
 * Pin map format, one loopback pair per line:
 *
 *   # comment
 *   LABEL_A  chip  offset   LABEL_B  chip  offset  [flags...]
 *   CN1_12   gpiochip0 35   CN1_14   gpiochip0 36
 *   CN1_20   gpiochip0 40   CN1_22   gpiochip0 41   extbias         (= extbias=up)
 *   CN1_30   gpiochip0 50   CN1_32   gpiochip0 51   extbias=down
 *   CN1_40   gpiochip1  6   CN1_42   gpiochip1   7   extbias=down settle=1000000
 *   CN1_50   gpiochip1  8   CN1_52   gpiochip1   9   intbias=down
 *
 * Trailing flags, any number, any order, space separated:
 *
 *   extbias | extbias=up | extbias=down
 *       This pair's net has its own external pull resistor (or sits on a
 *       gpiochip driver with no bias support at all - common on I2C
 *       expanders). A weak internal pull, where one even exists, can't
 *       reliably override an external resistor, so software leaves the
 *       line floating and trusts whatever the board resistor holds it to.
 *
 *   intbias=up | intbias=down
 *       Like extbias, but the tool actively *requests* this pull via the
 *       gpiochip driver's own bias support, instead of floating and hoping
 *       a board resistor (or a chip's power-on-reset default) provides it.
 *       Works on native SoC pinctrl and on PCAL-series I2C expanders
 *       (plain PCA95xx/PCA9535 without the 'L' does NOT support this - the
 *       tool will fail to request the line and say so). Use this when a
 *       net has no board resistor at all and needs one, or to make a
 *       PCAL expander pin's pull direction explicit and self-documenting
 *       instead of empirically discovered with a multimeter. Note this
 *       does not necessarily change how long the pin takes to settle after
 *       being driven - if a slow decay is caused by this exact pull already
 *       being active at power-on-reset, requesting it in software writes
 *       the same registers to the same values and timing is unchanged;
 *       re-measure if you were relying on 'settle=' before switching.
 *
 *   (extbias or intbias, whichever is used) direction determines which
 *   drive phase the pin is evaluated on: up is skipped while driving high,
 *   down is skipped while driving low - that phase reads the pin's own
 *   resting level regardless of any real connection, indistinguishable
 *   from a short. The other phase, where an active driver out-fights the
 *   pull, is the reliable check. Either way, the idle/stuck-pin check is
 *   skipped entirely for the pin, and it can still be the *driver* on any
 *   phase - an active output always overrides a pull, nothing is skipped
 *   there.
 *
 *   settle=<microseconds>
 *       Some nets - notably behind slow I2C GPIO expanders, or with a
 *       filter/debounce capacitor - take much longer than the global -s
 *       settle time to actually reach their resting level after being
 *       driven. This sets how long THIS pin's own level needs after being
 *       released before its reads, as a peer in a later step, can be
 *       trusted. Measure it empirically (drive the pin, release it, poll
 *       gpioget in a loop, see how long the transition actually takes) -
 *       do not guess. Applies on top of, not instead of, the global -s.
 *
 *   nodrive=A | nodrive=B
 *       Never make this side of the pair (A or B, by position on the line)
 *       the driver - it is only ever checked as a peer when its partner
 *       drives. It is still requested, still floated with its own resting
 *       bias between steps, so the reverse direction is tested normally.
 *
 *       For a pin sitting on a fixed-enable (OE tied high) auto-direction
 *       level translator (NXB/NXS, TXB/TXS) that has latched onto its
 *       OTHER port as the permanent driver: no pre-charge, no settle time
 *       and no amount of holding the line low will make it relay through -
 *       confirm on a scope before reaching for this flag, since it silently
 *       stops testing one direction entirely rather than just tolerating
 *       it. Cannot be set on both
 *       labels of the same pair - nothing would ever be driven then.
 */
static int load_pinmap(const char *path)
{
	char line[256];
	FILE *f;
	int lineno = 0;

	f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot open pin map '%s': %s\n", path, strerror(errno));
		return -1;
	}

	while (fgets(line, sizeof(line), f)) {
		char la[32], ca[32], lb[32], cb[32];
		unsigned int oa, ob;
		char *p = line;
		char *tok, *saveptr;
		int a, b, consumed;
		enum ext_bias eb = EXTB_NONE;
		int req_bias = 0;
		long settle = 0;
		int nodrive_a = 0;
		int nodrive_b = 0;

		lineno++;
		while (isspace((unsigned char)*p)) {
			p++;
		}
		if (*p == '#' || *p == '\0' || *p == '\n') {
			continue;
		}

		if (sscanf(p, "%31s %31s %u %31s %31s %u%n",
			  la, ca, &oa, lb, cb, &ob, &consumed) != 6) {
			fprintf(stderr, "%s:%d: malformed line\n", path, lineno);
			fclose(f);
			return -1;
		}

		for (tok = strtok_r(p + consumed, " \t\r\n", &saveptr); tok;
		     tok = strtok_r(NULL, " \t\r\n", &saveptr)) {
			if (strcmp(tok, "extbias") == 0 || strcmp(tok, "extbias=up") == 0) {
				eb = EXTB_UP;
				req_bias = 0;
			} else if (strcmp(tok, "extbias=down") == 0) {
				eb = EXTB_DOWN;
				req_bias = 0;
			} else if (strcmp(tok, "intbias=up") == 0) {
				eb = EXTB_UP;
				req_bias = 1;
			} else if (strcmp(tok, "intbias=down") == 0) {
				eb = EXTB_DOWN;
				req_bias = 1;
			} else if (strncmp(tok, "settle=", 7) == 0) {
				char *end;

				settle = strtol(tok + 7, &end, 10);
				if (*end != '\0' || settle < 0) {
					fprintf(stderr,
						"%s:%d: bad settle value '%s'\n",
						path, lineno, tok);
					fclose(f);
					return -1;
				}
			} else if (strcmp(tok, "nodrive=A") == 0) {
				nodrive_a = 1;
			} else if (strcmp(tok, "nodrive=B") == 0) {
				nodrive_b = 1;
			} else {
				fprintf(stderr,
					"%s:%d: unknown flag '%s' (expected 'extbias', "
					"'extbias=up', 'extbias=down', 'intbias=up', "
					"'intbias=down', 'settle=<us>', 'nodrive=A' or "
					"'nodrive=B')\n",
					path, lineno, tok);
				fclose(f);
				return -1;
			}
		}

		if (nodrive_a && nodrive_b) {
			fprintf(stderr,
				"%s:%d: 'nodrive=A' and 'nodrive=B' on the same pair - "
				"nothing would ever be driven\n",
				path, lineno);
			fclose(f);
			return -1;
		}

		a = add_pin(la, ca, oa);
		b = add_pin(lb, cb, ob);
		if (a < 0 || b < 0) {
			fclose(f);
			return -1;
		}

		pins[a].partner = b;
		pins[b].partner = a;
		pins[a].ext_bias = eb;
		pins[b].ext_bias = eb;
		pins[a].request_bias = req_bias;
		pins[b].request_bias = req_bias;
		pins[a].release_settle_us = settle;
		pins[b].release_settle_us = settle;
		pins[a].no_drive = nodrive_a;
		pins[b].no_drive = nodrive_b;
		npairs++;
	}

	fclose(f);

	if (npins == 0) {
		fprintf(stderr, "%s: no pairs defined\n", path);
		return -1;
	}
	return 0;
}

/* The bias a pin should sit at when idle/floating between drive steps:
 * GT_BIAS_NONE unless it was marked 'intbias=', in which case its own
 * requested direction. */
static enum gt_bias pin_resting_bias(int idx)
{
	if (!pins[idx].request_bias) {
		return GT_BIAS_NONE;
	}
	return pins[idx].ext_bias == EXTB_DOWN ? GT_BIAS_PULL_DOWN : GT_BIAS_PULL_UP;
}

static int request_all(void)
{
	int i;

	for (i = 0; i < npins; i++) {
		/*
		 * Floating (GT_BIAS_NONE) unless this pin was marked
		 * 'intbias=up|down' in the pin map, in which case its
		 * resting bias is actively requested from the gpiochip
		 * driver - see pin_resting_bias() and the file header.
		 */
		pins[i].line = gt_line_open(pins[i].chip, pins[i].offset,
					    opt_consumer, pin_resting_bias(i));
		if (!pins[i].line) {
			fprintf(stderr,
				"cannot request %s (%s line %u): %s\n"
				"  the line is probably claimed by a kernel driver,\n"
				"  muxed to a peripheral function, or - if this pin\n"
				"  is marked 'intbias=' - the gpiochip driver may not\n"
				"  support bias at all (plain PCA95xx/9535 expanders\n"
				"  don't; use 'extbias=' instead)\n",
				pins[i].label, pins[i].chip, pins[i].offset,
				strerror(errno));
			return -1;
		}
	}
	return 0;
}

static void release_all(void)
{
	int i;

	for (i = 0; i < npins; i++) {
		if (pins[i].line) {
			gt_line_close(pins[i].line);
		}
	}
	gt_cleanup();
}

static int set_all_input(void)
{
	int i;

	for (i = 0; i < npins; i++) {
		if (gt_line_set_input(pins[i].line, pin_resting_bias(i)) < 0) {
			fprintf(stderr, "cannot reconfigure %s: %s\n",
				pins[i].label, strerror(errno));
			return -1;
		}
	}
	return 0;
}

static void mark_fail(int idx)
{
	pins[idx].failed = 1;
	if (pins[idx].partner >= 0) {
		pins[pins[idx].partner].failed = 1;
	}
}

/*
 * Drive each pin to 'level' in turn; every other pin is left floating (no
 * internal bias requested at all - see file header). A pin with a real
 * wired connection follows the driver's low-impedance push regardless of
 * bias; a pin with its own external resistor reads whatever that resistor
 * holds it to, which is only meaningful to compare against 'level' on the
 * phase that resistor does *not* already dominate (see the ext_bias skip
 * below). A pin with no external resistor and no real connection is
 * genuinely undefined while floating and may occasionally read either way -
 * that is an inherent limitation of not using any bias at all, not a bug.
 *
 * A pin's own release_settle_us (set via 'settle=<us>' in the pin map) is
 * on top of the global -s: it is how long THIS pin needs after being
 * released from being driven before ITS reads, as a peer, are trustworthy.
 * Some nets - slow I2C GPIO expanders, or ones with a filter capacitor -
 * take far longer than -s to actually reach their resting level, and would
 * otherwise read a stale "still driven" value and look like a false short.
 */
static long elapsed_us(const struct timespec *from)
{
	struct timespec now;
	long sec, nsec;

	clock_gettime(CLOCK_MONOTONIC, &now);
	sec = now.tv_sec - from->tv_sec;
	nsec = now.tv_nsec - from->tv_nsec;
	return sec * 1000000L + nsec / 1000L;
}

/* Block, if needed, until pin j's own post-release settle time has passed. */
static void wait_for_release_settle(int j)
{
	long remaining;

	if (!pins[j].has_released || pins[j].release_settle_us <= 0) {
		return;
	}

	remaining = pins[j].release_settle_us - elapsed_us(&pins[j].released_at);
	if (remaining > 0) {
		usleep((useconds_t)remaining);
	}
}

static int phase_drive(int level)
{
	const char *pname = level ? "high" : "low";
	int i, j, fails = 0;
	int lw;

	/*
	 * Establish the resting baseline once, up front. Touching every pin
	 * on every outer iteration here used to be O(n^2) reconfigure calls
	 * for no reason: each driven pin is already put back to its resting
	 * input+bias state at the end of ITS OWN iteration below, and no
	 * other pin is ever touched in between - so nothing but the pin
	 * about to be driven needs reconfiguring at the top of the loop.
	 * On a chip where "set input" is a real register write (I2C GPIO
	 * expanders in particular, but also SoC port registers) that
	 * redundant churn - hundreds of extra writes across the sweep -
	 * lands right inside the settle window on pins that didn't need it,
	 * which can plausibly show up as spurious reads on unrelated pins
	 * once enough pins share a chip/port. Reducing this to one baseline
	 * pass (2N reconfigures total per phase instead of N^2+N) removes
	 * that source of churn entirely.
	 */
	if (set_all_input() < 0) {
		return -1;
	}

	for (i = 0; i < npins; i++) {
		int partner = pins[i].partner;

		/*
		 * 'nodrive=': this pin is never the driver - see the pin map
		 * format comment above load_pinmap() and the field comment on
		 * struct pin. It stays exactly as set_all_input() left it
		 * (floating with its resting bias) and is still read normally
		 * as a peer whenever its partner takes ITS turn below.
		 */
		if (pins[i].no_drive) {
			continue;
		}

		if (gt_line_set_output(pins[i].line, level) < 0) {
			fprintf(stderr, "cannot drive %s: %s\n",
				pins[i].label, strerror(errno));
			return -1;
		}
		usleep(opt_settle_us);

		for (j = 0; j < npins; j++) {
			int v;

			if (j == i) {
				continue;	/* the driver itself */
			}

			/*
			 * A pin with its own external resistor reads its
			 * resting level on the phase that resistor dominates,
			 * regardless of any real connection - not evaluable
			 * on that phase. The opposite phase, where an active
			 * driver out-fights the resistor, still checks it.
			 */
			if ((pins[j].ext_bias == EXTB_UP && level == 1) ||
			    (pins[j].ext_bias == EXTB_DOWN && level == 0)) {
				continue;
			}

			wait_for_release_settle(j);

			v = gt_line_get(pins[j].line);
			if (v < 0) {
				printf("%sFAIL%s   %s read-error\n",
				       col(C_RED), col(C_RST), pins[j].label);
				mark_fail(j);
				fails++;
				continue;
			}

			/* column alignment only for humans; piped output keeps
			 * the original single-space format for parsers */
			lw = use_color ? 18 : 0;

			if (j == partner) {
				if (v != level) {
					printf("%sFAIL%s   %-*s open partner=%s phase=%s\n",
					       col(C_RED), col(C_RST),
					       lw, pins[i].label, pins[j].label, pname);
					mark_fail(i);
					fails++;
				} else if (opt_verbose) {
					printf("%sOK%s     %-*s -> %-*s phase=%s\n",
					       col(C_GRN), col(C_RST),
					       lw, pins[i].label, lw, pins[j].label, pname);
				}
			} else if (v == level) {
				printf("%sFAIL%s   %-*s short-to=%s phase=%s\n",
				       col(C_RED), col(C_RST),
				       lw, pins[i].label, pins[j].label, pname);
				mark_fail(i);
				mark_fail(j);
				fails++;
			}
		}

		/*
		 * Put the driver back to its resting bias before moving on,
		 * pre-charging the net to its resting level first if asked
		 * to - see opt_precharge_us for why that is ever needed.
		 */
		if (opt_precharge_us > 0) {
			/*
			 * Drive to this pin's RESTING level - the direction its
			 * extbias/intbias marking says the net sits at when
			 * nobody drives it - not merely the opposite of what was
			 * just driven. Those are the same thing on the phase
			 * that drives against the pull, and opposites on the
			 * phase that drives with it: pre-charging 'the other
			 * way' after driving high would leave a pull-up net
			 * parked low, which is the very state this is here to
			 * avoid. A pin with no bias direction has no resting
			 * level to aim for, so for that one just undo the drive.
			 */
			int rest = pins[i].ext_bias == EXTB_UP   ? 1 :
				   pins[i].ext_bias == EXTB_DOWN ? 0 : !level;

			if (gt_line_set_output(pins[i].line, rest) < 0) {
				fprintf(stderr, "cannot pre-charge %s: %s\n",
					pins[i].label, strerror(errno));
				return -1;
			}
			usleep(opt_precharge_us);
		}
		gt_line_set_input(pins[i].line, pin_resting_bias(i));
		clock_gettime(CLOCK_MONOTONIC, &pins[i].released_at);
		pins[i].has_released = 1;

		/*
		 * Now check the pin actually came back to where it is meant
		 * to rest. A pin that stays at the level it was just driven
		 * to - no board resistor, a bias request the pin controller
		 * silently ignored, or an auto-sensing translator latched on
		 * the net - would otherwise be silently reported as a short
		 * against every pin driven after it, blaming the wrong pins
		 * entirely. Name it here instead, once, where it happens.
		 *
		 * Only meaningful for a pin with a known resting level: an
		 * ext_bias/intbias direction says what to expect. A pin with
		 * neither is undefined while floating by design and has
		 * nothing to be compared against.
		 */
		if (pins[i].ext_bias != EXTB_NONE) {
			int expect = pins[i].ext_bias == EXTB_UP ? 1 : 0;
			int v;

			usleep(opt_settle_us);
			wait_for_release_settle(i);

			v = gt_line_get(pins[i].line);
			if (v >= 0 && v != expect) {
				printf("%sFAIL%s   %-*s stuck-at=%d after release "
				       "(expected %d - no effective pull-%s? "
				       "translator latched?)\n",
				       col(C_RED), col(C_RST),
				       use_color ? 18 : 0, pins[i].label, v, expect,
				       expect ? "up" : "down");
				mark_fail(i);
				fails++;
			}
		}
	}

	return fails;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-c pinmap] [-s settle_us] [-p precharge_us] [-n consumer] [-v] [-q]\n"
		"       %s [--color|--no-color]\n"
		"       %s --list\n"
		"\n"
		"  -c FILE     pin map file (loopback pairs)\n"
		"  -s US       settle time per step in microseconds (default 500)\n"
		"  -p US       pre-charge: before releasing a driven pin, drive it to its\n"
		"              resting level (per its extbias/intbias direction) for US\n"
		"              microseconds (default 200; 0 disables it). Needed on nets with no\n"
		"              effective bias, and to break auto-sensing level translators\n"
		"              (NXS/NXB, TXS/TXB) out of a latched direction. A few hundred\n"
		"              microseconds is plenty; it briefly drives against whatever\n"
		"              else holds the net, so do not set it larger than needed.\n"
		"  -n NAME     consumer name shown in gpioinfo (default %s)\n"
		"  -v          also print passing pin pairs\n"
		"  -q          quiet: suppress the INFO banner and SUMMARY line - PAIR/FAIL\n"
		"              lines and the final RESULT still print\n"
		"  --color     force colored/aligned output (default: only on a terminal)\n"
		"  --no-color  force plain parseable output\n"
		"  --list      dump all gpiochips and line names, then exit\n"
		"\n"
		"By default every line floats (no bias requested) and its\n"
		"resting level, if any, comes from a board resistor - mark such\n"
		"a pair 'extbias' or 'extbias=down' so the drive-phase check\n"
		"knows which phase to trust for it. Use 'intbias=up' or\n"
		"'intbias=down' instead to have the tool actively request that\n"
		"pull from the gpiochip driver (native pinctrl or PCAL-series\n"
		"I2C expanders) rather than float and rely on the board. Add\n"
		"'settle=<us>' to a pair if it needs longer than -s to actually\n"
		"reach its resting level after being driven (measure it, don't\n"
		"guess) - see comments in the pin map format for full details.\n"
		"\n"
		"After releasing each driven pin the tool checks it actually\n"
		"returned to its resting level, and reports 'stuck-at=' naming\n"
		"that pin. Without that check such a pin is instead reported as\n"
		"'short-to' by every pin driven after it, which points at the\n"
		"wrong pins entirely. If you see 'stuck-at=', that net has no\n"
		"effective pull in the marked direction, or something on it (an\n"
		"auto-sensing level translator, typically) is actively holding\n"
		"it - try -p to pre-charge past it.\n"
		"\n"
		"If a pin still reports 'open' against its partner even with -p\n"
		"raised and its bias direction confirmed correct, and a scope shows\n"
		"the level genuinely never reaches the partner no matter how long\n"
		"it is held, that side is probably a fixed-enable (OE tied high)\n"
		"auto-sensing translator latched onto its OTHER port as the\n"
		"permanent driver - it will not relay in this direction at all.\n"
		"Mark that pin 'nodrive=A' or 'nodrive=B' (by its position on the\n"
		"line) to test the pair in the working direction only, instead of\n"
		"reporting a false open every run.\n",
		argv0, argv0, argv0, CONSUMER_DEFAULT);
}

int main(int argc, char **argv)
{
	const char *pinmap = NULL;
	int failed_pairs = 0;
	int fails = 0;
	int i, rc;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--list") == 0) {
			return gt_dump_chips() == 0 ? 0 : 2;
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			pinmap = argv[++i];
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			opt_settle_us = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			opt_precharge_us = atoi(argv[++i]);
			if (opt_precharge_us < 0) {
				fprintf(stderr, "bad pre-charge time '%s'\n", argv[i]);
				return 2;
			}
		} else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			opt_consumer = argv[++i];
		} else if (strcmp(argv[i], "-v") == 0) {
			opt_verbose = 1;
		} else if (strcmp(argv[i], "-q") == 0) {
			opt_quiet = 1;
		} else if (strcmp(argv[i], "--color") == 0) {
			use_color = 1;
		} else if (strcmp(argv[i], "--no-color") == 0) {
			use_color = 0;
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (use_color < 0) {
		use_color = isatty(STDOUT_FILENO);
	}

	if (!pinmap) {
		usage(argv[0]);
		return 2;
	}

	if (load_pinmap(pinmap) < 0) {
		return 2;
	}

	if (!opt_quiet) {
		print_rule('=');
		if (use_color) {
			printf("%s gpiotest%s - GPIO loopback pair test (built %s %s)\n",
			       col(C_BLD), col(C_RST), build_date, build_time);
		}
		printf("%sINFO%s   api=%s pins=%d pairs=%d settle=%dus precharge=%dus\n",
		       col(C_CYN), col(C_RST),
		       gt_api_version(), npins, npairs, opt_settle_us, opt_precharge_us);
		print_rule('=');
	}

	if (request_all() < 0) {
		release_all();
		return 2;
	}

	rc = phase_drive(1);
	if (rc < 0) {
		goto err;
	}
	fails += rc;

	rc = phase_drive(0);
	if (rc < 0) {
		goto err;
	}
	fails += rc;

	/* leave every pin as a plain input before handing the board back */
	set_all_input();
	release_all();

	print_rule('-');
	for (i = 0; i < npins; i++) {
		if (pins[i].partner > i) {
			int bad = pins[i].failed || pins[pins[i].partner].failed;
			int lw = use_color ? 18 : 0;

			if (bad) {
				failed_pairs++;
			}
			if (bad || opt_verbose) {
				printf("PAIR   %-*s %-*s %s%s%s\n",
				       lw, pins[i].label,
				       lw, pins[pins[i].partner].label,
				       col(bad ? C_RED : C_GRN),
				       bad ? "FAIL" : "PASS",
				       col(C_RST));
			}
		}
	}

	if (!opt_quiet) {
		printf("%sSUMMARY%s pins=%d pairs=%d failed_pairs=%s%d%s errors=%s%d%s\n",
		       col(C_CYN), col(C_RST), npins, npairs,
		       col(failed_pairs ? C_RED : C_GRN), failed_pairs, col(C_RST),
		       col(fails ? C_RED : C_GRN), fails, col(C_RST));
	}
	print_rule('=');
	printf("%sRESULT %s%s\n",
	       col(failed_pairs ? C_RED : C_GRN),
	       failed_pairs ? "FAIL" : "PASS",
	       col(C_RST));
	print_rule('=');

	return failed_pairs ? 1 : 0;

err:
	release_all();
	return 2;
}
