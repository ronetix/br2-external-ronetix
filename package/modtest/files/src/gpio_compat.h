/*
 * Thin compatibility layer over libgpiod 1.x and 2.x.
 *
 * Only what the pin test needs: request a single line, flip it between
 * input-with-bias and output, read it back.  One request per line, which
 * keeps multi-gpiochip handling trivial and makes reconfiguration
 * per-line on both API generations.
 */
#ifndef GPIO_COMPAT_H
#define GPIO_COMPAT_H

enum gt_bias {
	GT_BIAS_NONE = 0,
	GT_BIAS_PULL_UP,
	GT_BIAS_PULL_DOWN,
};

typedef struct gt_line gt_line;

/* Returns "1" or "2" - which libgpiod generation we were built against. */
const char *gt_api_version(void);

/*
 * Open and request one line as input with the given bias.
 * chip may be "gpiochip0", "0" or "/dev/gpiochip0".
 * Returns NULL on failure (errno is set).
 */
gt_line *gt_line_open(const char *chip, unsigned int offset,
		      const char *consumer, enum gt_bias bias);

void gt_line_close(gt_line *l);

/* Reconfigure an already requested line. 0 on success, -1 on error. */
int gt_line_set_input(gt_line *l, enum gt_bias bias);
int gt_line_set_output(gt_line *l, int value);

/* Read the line. 0 or 1, -1 on error. */
int gt_line_get(gt_line *l);

/* Kernel-assigned line name, or "" if the line is unnamed. */
const char *gt_line_kname(const gt_line *l);

/* Release cached chip handles. Call once at exit, after closing all lines. */
void gt_cleanup(void);

/*
 * Enumerate /dev/gpiochip* and print chip label, ngpio and every line name
 * to stdout.  Used by "gpiotest --list" to help build the pin map.
 */
int gt_dump_chips(void);

#endif /* GPIO_COMPAT_H */
