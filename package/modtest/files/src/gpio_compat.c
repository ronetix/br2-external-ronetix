/*
 * gpio_compat.c - implementation of the gt_line_* API declared in
 * gpio_compat.h (see that header for the API contract itself).
 *
 * This file is compiled once but built two different ways, chosen at
 * compile time by GPIOD_MAJOR (derived from `pkg-config --modversion
 * libgpiod` by the Makefile, never guessed here): everything from
 * "libgpiod 2.x" down to the matching #else is the libgpiod 1.x path, so
 * exactly one of the two ever exists in the compiled object. Callers
 * (gpiotest.c) never see the difference - they only ever call
 * the gt_line_* functions declared in gpio_compat.h.
 *
 *   - libgpiod 2.x: one gpiod_line_request per line, reconfigured in
 *     place via gpiod_line_request_reconfigure_lines() to flip between
 *     input+bias and output - no release/re-request needed.
 *   - libgpiod 1.x: reconfiguring direction means releasing and
 *     re-requesting the line (gpiod_line_set_config(), added in 1.5, does
 *     this internally when available; GPIOD_HAVE_BIAS gates that path).
 *     Below 1.5 there is no bias support in userspace at all - gt_line_open()
 *     refuses any non-NONE bias up front rather than silently ignoring it.
 *
 * Chip handles are cached in the module-static chips[] table (looked up
 * by normalised device path in chip_get()) so a chip is opened at most
 * once no matter how many lines on it are requested, and closed once via
 * gt_cleanup() at program exit rather than per-line. MAX_CHIPS (16) is a
 * fixed upper bound on distinct gpiochips referenced by one pin map - a
 * production carrier with a couple of SoC banks plus a PCAL expander or
 * two comfortably fits; bump it if a fixture ever needs more.
 *
 * Not thread-safe (chips[]/nchips have no locking) and not meant to be -
 * gpiotest is single-threaded, one request per line, for
 * the life of the process.
 */
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gpiod.h>

#include "gpio_compat.h"

#ifndef GPIOD_MAJOR
#error "GPIOD_MAJOR not defined - the Makefile should derive it from pkg-config"
#endif

#define MAX_CHIPS 16

struct chip_entry {
	char name[144];
	struct gpiod_chip *chip;
};

static struct chip_entry chips[MAX_CHIPS];
static int nchips;

/* Normalise "0" / "gpiochip0" / "/dev/gpiochip0" into a device path. */
static void chip_path(const char *name, char *out, size_t len)
{
	if (name[0] == '/') {
		snprintf(out, len, "%s", name);
	} else if (strncmp(name, "gpiochip", 8) == 0) {
		snprintf(out, len, "/dev/%s", name);
	} else {
		snprintf(out, len, "/dev/gpiochip%s", name);
	}
}

static struct gpiod_chip *chip_get(const char *name)
{
	char path[128];
	int i;

	chip_path(name, path, sizeof(path));

	for (i = 0; i < nchips; i++) {
		if (strcmp(chips[i].name, path) == 0) {
			return chips[i].chip;
		}
	}

	if (nchips == MAX_CHIPS) {
		errno = ENOSPC;
		return NULL;
	}

	/* gpiod_chip_open()'s signature is unchanged between libgpiod 1.x and
	 * 2.x, so this call needs no GPIOD_MAJOR branching. */
	chips[nchips].chip = gpiod_chip_open(path);
	if (!chips[nchips].chip) {
		return NULL;
	}

	snprintf(chips[nchips].name, sizeof(chips[nchips].name), "%s", path);
	return chips[nchips++].chip;
}

void gt_cleanup(void)
{
	int i;

	for (i = 0; i < nchips; i++) {
		gpiod_chip_close(chips[i].chip);
	}
	nchips = 0;
}

const char *gt_api_version(void)
{
#if GPIOD_MAJOR >= 2
	return "2";
#else
	return "1";
#endif
}

/* ================================================================== */
#if GPIOD_MAJOR >= 2
/* ------------------------- libgpiod 2.x --------------------------- */

struct gt_line {
	struct gpiod_line_request *req;
	unsigned int offset;
	char kname[64];
};

static enum gpiod_line_bias bias_to_gpiod(enum gt_bias b)
{
	switch (b) {
	case GT_BIAS_PULL_UP:
		return GPIOD_LINE_BIAS_PULL_UP;
	case GT_BIAS_PULL_DOWN:
		return GPIOD_LINE_BIAS_PULL_DOWN;
	default:
		return GPIOD_LINE_BIAS_DISABLED;
	}
}

static struct gpiod_line_settings *settings_make(int output, enum gt_bias bias,
						 int value)
{
	struct gpiod_line_settings *s = gpiod_line_settings_new();

	if (!s) {
		return NULL;
	}

	if (output) {
		gpiod_line_settings_set_direction(s, GPIOD_LINE_DIRECTION_OUTPUT);
		gpiod_line_settings_set_output_value(s, value ? GPIOD_LINE_VALUE_ACTIVE
							      : GPIOD_LINE_VALUE_INACTIVE);
	} else {
		gpiod_line_settings_set_direction(s, GPIOD_LINE_DIRECTION_INPUT);
		gpiod_line_settings_set_bias(s, bias_to_gpiod(bias));
	}

	return s;
}

static int line_apply(gt_line *l, int output, enum gt_bias bias, int value)
{
	struct gpiod_line_settings *s;
	struct gpiod_line_config *lc;
	int rc = -1;

	s = settings_make(output, bias, value);
	if (!s) {
		return -1;
	}

	lc = gpiod_line_config_new();
	if (!lc) {
		gpiod_line_settings_free(s);
		return -1;
	}

	if (gpiod_line_config_add_line_settings(lc, &l->offset, 1, s) == 0) {
		rc = gpiod_line_request_reconfigure_lines(l->req, lc);
	}

	gpiod_line_config_free(lc);
	gpiod_line_settings_free(s);
	return rc;
}

gt_line *gt_line_open(const char *chipname, unsigned int offset,
		      const char *consumer, enum gt_bias bias)
{
	struct gpiod_chip *chip;
	struct gpiod_line_settings *s = NULL;
	struct gpiod_line_config *lc = NULL;
	struct gpiod_request_config *rq = NULL;
	struct gpiod_line_info *info;
	gt_line *l;

	chip = chip_get(chipname);
	if (!chip) {
		return NULL;
	}

	l = calloc(1, sizeof(*l));
	if (!l) {
		return NULL;
	}
	l->offset = offset;

	info = gpiod_chip_get_line_info(chip, offset);
	if (info) {
		const char *n = gpiod_line_info_get_name(info);

		snprintf(l->kname, sizeof(l->kname), "%s", n ? n : "");
		gpiod_line_info_free(info);
	}

	s = settings_make(0, bias, 0);
	lc = gpiod_line_config_new();
	rq = gpiod_request_config_new();
	if (!s || !lc || !rq) {
		goto fail;
	}

	gpiod_request_config_set_consumer(rq, consumer);
	if (gpiod_line_config_add_line_settings(lc, &offset, 1, s) != 0) {
		goto fail;
	}

	l->req = gpiod_chip_request_lines(chip, rq, lc);
	if (!l->req) {
		goto fail;
	}

	gpiod_request_config_free(rq);
	gpiod_line_config_free(lc);
	gpiod_line_settings_free(s);
	return l;

fail:
	if (rq) {
		gpiod_request_config_free(rq);
	}
	if (lc) {
		gpiod_line_config_free(lc);
	}
	if (s) {
		gpiod_line_settings_free(s);
	}
	free(l);
	return NULL;
}

void gt_line_close(gt_line *l)
{
	if (!l) {
		return;
	}
	if (l->req) {
		gpiod_line_request_release(l->req);
	}
	free(l);
}

int gt_line_set_input(gt_line *l, enum gt_bias bias)
{
	return line_apply(l, 0, bias, 0);
}

int gt_line_set_output(gt_line *l, int value)
{
	return line_apply(l, 1, GT_BIAS_NONE, value);
}

int gt_line_get(gt_line *l)
{
	enum gpiod_line_value v = gpiod_line_request_get_value(l->req, l->offset);

	if (v == GPIOD_LINE_VALUE_ERROR) {
		return -1;
	}
	return v == GPIOD_LINE_VALUE_ACTIVE ? 1 : 0;
}

int gt_dump_chips(void)
{
	glob_t g;
	size_t i;

	if (glob("/dev/gpiochip*", 0, NULL, &g) != 0) {
		fprintf(stderr, "no gpiochips found\n");
		return -1;
	}

	for (i = 0; i < g.gl_pathc; i++) {
		struct gpiod_chip *c = gpiod_chip_open(g.gl_pathv[i]);
		struct gpiod_chip_info *ci;
		unsigned int n, off;

		if (!c) {
			continue;
		}

		ci = gpiod_chip_get_info(c);
		if (!ci) {
			gpiod_chip_close(c);
			continue;
		}

		n = gpiod_chip_info_get_num_lines(ci);
		printf("%s  label=%s  ngpio=%u\n", g.gl_pathv[i],
		       gpiod_chip_info_get_label(ci), n);

		for (off = 0; off < n; off++) {
			struct gpiod_line_info *li = gpiod_chip_get_line_info(c, off);
			const char *name, *cons;

			if (!li) {
				continue;
			}
			name = gpiod_line_info_get_name(li);
			cons = gpiod_line_info_get_consumer(li);
			printf("  %3u  %-28s %s\n", off, name ? name : "-",
			       cons ? cons : "");
			gpiod_line_info_free(li);
		}

		gpiod_chip_info_free(ci);
		gpiod_chip_close(c);
	}

	globfree(&g);
	return 0;
}

/* ================================================================== */
#else
/* ------------------------- libgpiod 1.x --------------------------- */

struct gt_line {
	struct gpiod_line *line;
	char kname[64];
};

#if GPIOD_HAVE_BIAS

static int bias_flags(enum gt_bias b)
{
	switch (b) {
	case GT_BIAS_PULL_UP:
		return GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
	case GT_BIAS_PULL_DOWN:
		return GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
	default:
		return GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
	}
}

#else /* !GPIOD_HAVE_BIAS - libgpiod < 1.5, no bias support at all */

static int bias_flags(enum gt_bias b)
{
	(void)b;
	return 0;
}

#endif

gt_line *gt_line_open(const char *chipname, unsigned int offset,
		      const char *consumer, enum gt_bias bias)
{
	struct gpiod_chip *chip;
	const char *n;
	gt_line *l;

#if !GPIOD_HAVE_BIAS
	if (bias != GT_BIAS_NONE) {
		fprintf(stderr,
			"libgpiod build has no bias support (needs >= 1.5) - "
			"cannot request pull-up/pull-down on '%s'\n",
			consumer);
		errno = ENOTSUP;
		return NULL;
	}
#endif

	chip = chip_get(chipname);
	if (!chip) {
		return NULL;
	}

	l = calloc(1, sizeof(*l));
	if (!l) {
		return NULL;
	}

	l->line = gpiod_chip_get_line(chip, offset);
	if (!l->line) {
		free(l);
		return NULL;
	}

	n = gpiod_line_name(l->line);
	snprintf(l->kname, sizeof(l->kname), "%s", n ? n : "");

	if (gpiod_line_request_input_flags(l->line, consumer, bias_flags(bias)) < 0) {
		free(l);
		return NULL;
	}

	return l;
}

void gt_line_close(gt_line *l)
{
	if (!l) {
		return;
	}
	if (l->line) {
		gpiod_line_release(l->line);
	}
	free(l);
}

#if GPIOD_HAVE_BIAS

int gt_line_set_input(gt_line *l, enum gt_bias bias)
{
	return gpiod_line_set_config(l->line, GPIOD_LINE_REQUEST_DIRECTION_INPUT,
				     bias_flags(bias), 0);
}

int gt_line_set_output(gt_line *l, int value)
{
	return gpiod_line_set_config(l->line, GPIOD_LINE_REQUEST_DIRECTION_OUTPUT,
				     0, value);
}

#else /* !GPIOD_HAVE_BIAS */

/*
 * gpiod_line_set_config() does not exist before 1.5 either. Reconfiguring
 * direction means releasing and re-requesting the line. Bias is silently
 * ignored - gt_line_open() already refused any non-NONE bias, so this only
 * ever gets called with GT_BIAS_NONE from the "release" path in main.c.
 */
int gt_line_set_input(gt_line *l, enum gt_bias bias)
{
	(void)bias;
	gpiod_line_release(l->line);
	return gpiod_line_request_input(l->line, "gpiotest");
}

int gt_line_set_output(gt_line *l, int value)
{
	gpiod_line_release(l->line);
	return gpiod_line_request_output(l->line, "gpiotest", value);
}

#endif

int gt_line_get(gt_line *l)
{
	return gpiod_line_get_value(l->line);
}

int gt_dump_chips(void)
{
	glob_t g;
	size_t i;

	if (glob("/dev/gpiochip*", 0, NULL, &g) != 0) {
		fprintf(stderr, "no gpiochips found\n");
		return -1;
	}

	for (i = 0; i < g.gl_pathc; i++) {
		struct gpiod_chip *c = gpiod_chip_open(g.gl_pathv[i]);
		unsigned int n, off;

		if (!c) {
			continue;
		}

		n = gpiod_chip_num_lines(c);
		printf("%s  label=%s  ngpio=%u\n", g.gl_pathv[i],
		       gpiod_chip_label(c), n);

		for (off = 0; off < n; off++) {
			struct gpiod_line *li = gpiod_chip_get_line(c, off);
			const char *name, *cons;

			if (!li) {
				continue;
			}
			name = gpiod_line_name(li);
			cons = gpiod_line_consumer(li);
			printf("  %3u  %-28s %s\n", off, name ? name : "-",
			       cons ? cons : "");
		}

		gpiod_chip_close(c);
	}

	globfree(&g);
	return 0;
}

#endif

const char *gt_line_kname(const gt_line *l)
{
	return l->kname;
}
