/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "cgg1_layout.h"

#define SEGMENTS 13U

/* {byte, bit} pairs of each segment of a digit group */
static const uint8_t top_left[SEGMENTS * 2] = {
	9, 4, 8, 5, 8, 6, 8, 7, 7, 0, 7, 1, 9, 2,
	14, 4, 14, 5, 14, 6, 14, 7, 13, 0, 9, 3,
};
static const uint8_t top_middle[SEGMENTS * 2] = {
	6, 3, 5, 7, 4, 0, 4, 1, 4, 6, 4, 7, 5, 4,
	5, 0, 6, 6, 6, 5, 6, 4, 6, 2, 4, 5,
};
static const uint8_t top_right[SEGMENTS * 2] = {
	1, 6, 1, 7, 1, 0, 2, 6, 2, 3, 2, 1, 2, 0,
	3, 7, 2, 2, 2, 4, 2, 7, 1, 1, 2, 5,
};
static const uint8_t bottom_left[SEGMENTS * 2] = {
	13, 1, 13, 2, 14, 2, 14, 0, 15, 0, 16, 6, 16, 5,
	16, 4, 16, 7, 15, 6, 14, 1, 14, 3, 15, 7,
};
static const uint8_t bottom_middle[SEGMENTS * 2] = {
	9, 6, 8, 3, 8, 2, 8, 1, 8, 4, 7, 3, 10, 5,
	10, 4, 10, 6, 9, 5, 9, 0, 10, 7, 9, 7,
};
static const uint8_t bottom_right[SEGMENTS * 2] = {
	5, 1, 5, 2, 7, 7, 3, 2, 3, 4, 0, 1, 0, 0,
	3, 5, 3, 3, 3, 0, 7, 6, 6, 0, 3, 1,
};

static const uint8_t top_left_2022[SEGMENTS * 2] = {
	10, 7, 10, 4, 10, 3, 10, 2, 10, 1, 10, 0, 10, 5,
	11, 0, 11, 1, 11, 2, 11, 3, 11, 4, 10, 6,
};
static const uint8_t top_middle_2022[SEGMENTS * 2] = {
	6, 7, 6, 4, 6, 3, 6, 2, 6, 1, 6, 0, 6, 5,
	7, 0, 7, 1, 7, 2, 7, 3, 7, 4, 6, 6,
};
static const uint8_t top_right_2022[SEGMENTS * 2] = {
	2, 7, 2, 4, 2, 3, 2, 2, 2, 1, 2, 0, 2, 5,
	3, 0, 3, 1, 3, 2, 3, 3, 3, 4, 2, 6,
};
static const uint8_t bottom_left_2022[SEGMENTS * 2] = {
	13, 7, 13, 4, 13, 3, 13, 2, 13, 1, 13, 0, 13, 5,
	14, 0, 14, 1, 14, 2, 14, 3, 14, 4, 13, 6,
};
static const uint8_t bottom_middle_2022[SEGMENTS * 2] = {
	8, 7, 8, 4, 8, 3, 8, 2, 8, 1, 8, 0, 8, 5,
	9, 0, 9, 1, 9, 2, 9, 3, 9, 4, 8, 6,
};
static const uint8_t bottom_right_2022[SEGMENTS * 2] = {
	0, 7, 0, 4, 0, 3, 0, 2, 0, 1, 0, 0, 0, 5,
	1, 0, 1, 1, 1, 2, 1, 3, 1, 4, 0, 6,
};

/*
 * How each digit maps to the segments:
 *           1
 *  12 :-----------: 2
 *     |           |
 *  11 |           | 3
 *     |     13    |
 *  10 :-----------: 4
 *     |           |
 *   9 |           | 5
 *     |     7     |
 *   8 :-----------: 6
 */
static const uint8_t digits[16][SEGMENTS + 1] = {
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0},  /* 0 */
	{2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 0},        /* 1 */
	{1, 2, 3, 4, 6, 7, 8, 9, 10, 12, 13, 0, 0, 0},  /* 2 */
	{1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 13, 0, 0, 0},  /* 3 */
	{2, 3, 4, 5, 6, 10, 11, 12, 13, 0, 0, 0, 0, 0}, /* 4 */
	{1, 2, 4, 5, 6, 7, 8, 10, 11, 12, 13, 0, 0, 0}, /* 5 */
	{1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 0}, /* 6 */
	{1, 2, 3, 4, 5, 6, 12, 0, 0, 0, 0, 0, 0, 0},    /* 7 */
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0}, /* 8 */
	{1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 0, 0}, /* 9 */
	{1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 0, 0}, /* A */
	{4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 0, 0, 0}, /* b */
	{1, 2, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0, 0},  /* C */
	{2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0},  /* d */
	{1, 2, 4, 6, 7, 8, 9, 10, 11, 12, 0, 0, 0, 0},  /* E */
	{1, 2, 8, 9, 10, 11, 12, 13, 0, 0, 0, 0, 0, 0}  /* F */
};

enum group {
	TOP_LEFT,
	TOP_MIDDLE,
	TOP_RIGHT,
	BOTTOM_LEFT,
	BOTTOM_MIDDLE,
	BOTTOM_RIGHT,
	GROUPS,
};

struct bit {
	uint8_t byte;
	uint8_t mask;
};

struct map {
	const uint8_t *seg[GROUPS];
	uint8_t size;
	struct bit deg;
	struct bit dash;
	struct bit under;
	struct bit smiley;
	struct bit ble;
	struct bit bat;
	struct bit bat_bar[5];
	struct bit big_minus;
	struct bit big_point;
	struct bit big_one;
	struct bit small_minus;
	struct bit small_point;
	struct bit small_percent;
	/* Per byte: bits owned by the element, and the bits of its Hi and Lo texts */
	uint8_t bat_clear[IOTDRV_CGG1_FRAME_MAX];
	uint8_t big_clear[IOTDRV_CGG1_FRAME_MAX];
	uint8_t big_hi[IOTDRV_CGG1_FRAME_MAX];
	uint8_t big_lo[IOTDRV_CGG1_FRAME_MAX];
	uint8_t small_clear[IOTDRV_CGG1_FRAME_MAX];
	uint8_t small_hi[IOTDRV_CGG1_FRAME_MAX];
	uint8_t small_lo[IOTDRV_CGG1_FRAME_MAX];
};

static const struct map maps[] = {
	[IOTDRV_CGG1_VARIANT_CGG1] = {
		.seg = {top_left, top_middle, top_right, bottom_left, bottom_middle, bottom_right},
		.size = 18,
		.deg = {1, 0x0C},
		.dash = {1, 0x10},
		.under = {1, 0x20},
		.smiley = {7, 0x04},
		.ble = {9, 0x02},
		.bat = {7, 0x10},
		.bat_bar = {{5, 0x40}, {5, 0x20}, {6, 0x80}, {6, 0x02}, {7, 0x20}},
		.big_minus = {9, 0x08},
		.big_point = {3, 0x40},
		.big_one = {15, 0x3E},
		.small_minus = {15, 0x80},
		.small_point = {0, 0x04},
		.small_percent = {5, 0x08},
		.bat_clear = {[5] = 0x60, [6] = 0x82, [7] = 0x30},
		.big_clear = {[1] = 0xC3, [2] = 0xFF, [3] = 0xC0, [4] = 0xFF, [5] = 0x91,
			      [6] = 0x7C,
			      [7] = 0x03, [8] = 0xE0, [9] = 0x1C, [13] = 0x01, [14] = 0xF0,
			      [15] = 0x3E},
		.big_hi = {[5] = 0x01, [6] = 0x60, [7] = 0x03, [8] = 0xE0, [9] = 0x08, [13] = 0x01,
			   [14] = 0xF0},
		.big_lo = {[4] = 0xE2, [5] = 0x11, [6] = 0x60, [7] = 0x02, [9] = 0x04, [13] = 0x01,
			   [14] = 0xF0},
		.small_clear = {[0] = 0x07, [3] = 0x3F, [5] = 0x0E, [6] = 0x01, [7] = 0xC8,
				[8] = 0x1E, [9] = 0xE1, [10] = 0xF0, [13] = 0x06, [14] = 0x0F,
				[15] = 0xC1, [16] = 0xF0},
		.small_hi = {[9] = 0x20, [10] = 0x50, [13] = 0x04, [14] = 0x0F, [15] = 0xC1,
			     [16] = 0xD0},
		.small_lo = {[7] = 0x08, [8] = 0x12, [9] = 0xA0, [10] = 0x70, [14] = 0x0A,
			     [15] = 0x40, [16] = 0xF0},
	},
	[IOTDRV_CGG1_VARIANT_CGG1_2022] = {
		.seg = {top_left_2022, top_middle_2022, top_right_2022, bottom_left_2022,
			bottom_middle_2022, bottom_right_2022},
		.size = 16,
		.deg = {4, 0x0C},
		.dash = {4, 0x02},
		.under = {4, 0x01},
		.smiley = {4, 0x10},
		.ble = {9, 0x40},
		.bat = {5, 0x01},
		.bat_bar = {{5, 0x02}, {5, 0x04}, {5, 0x08}, {5, 0x10}, {5, 0x20}},
		.big_minus = {10, 0x40},
		.big_point = {4, 0x20},
		.big_one = {12, 0x1F},
		.small_minus = {13, 0x40},
		.small_point = {4, 0x40},
		.small_percent = {1, 0x40},
		.bat_clear = {[5] = 0xFF},
		.big_clear = {[2] = 0xFF, [3] = 0xFF, [4] = 0x20, [6] = 0xFF, [7] = 0xBF,
			      [10] = 0xFF, [11] = 0xFF, [12] = 0xFF},
		.big_hi = {[7] = 0x17, [10] = 0x5F, [11] = 0x1F},
		.big_lo = {[6] = 0x67, [7] = 0x07, [10] = 0x21, [11] = 0x1F},
		.small_clear = {[0] = 0xFF, [1] = 0xFF, [4] = 0x40, [8] = 0xFF, [9] = 0xBF,
				[13] = 0xFF, [14] = 0xFF},
		.small_hi = {[9] = 0x17, [13] = 0x5F, [14] = 0x1F},
		.small_lo = {[8] = 0x67, [9] = 0x07, [13] = 0x21, [14] = 0x1F},
	},
};

static const struct map *map_of(const struct iotdrv_cgg1_frame *f)
{
	return &maps[f->variant];
}

static void bit_set(struct iotdrv_cgg1_frame *f, struct bit bit, bool on)
{
	if (on) {
		f->b[bit.byte] |= bit.mask;
	} else {
		f->b[bit.byte] &= (uint8_t)~bit.mask;
	}
}

static void apply(struct iotdrv_cgg1_frame *f, const uint8_t *masks, bool on)
{
	for (size_t i = 0; i < f->size; i++) {
		if (on) {
			f->b[i] |= masks[i];
		} else {
			f->b[i] &= (uint8_t)~masks[i];
		}
	}
}

static void set_digit(struct iotdrv_cgg1_frame *f, uint8_t digit, enum group group)
{
	const uint8_t *segments = map_of(f)->seg[group];

	for (size_t i = 0; i < SEGMENTS; i++) {
		int segment = (int)digits[digit][i] - 1;

		if (segment < 0) {
			break;
		}
		f->b[segments[2 * segment]] |= (uint8_t)(1U << segments[1 + 2 * segment]);
	}
}

void iotdrv_cgg1_layout_init(struct iotdrv_cgg1_frame *f, enum iotdrv_cgg1_variant variant)
{
	memset(f, 0, sizeof(*f));
	f->variant = variant;
	f->size = maps[variant].size;
}

void iotdrv_cgg1_layout_clear(struct iotdrv_cgg1_frame *f)
{
	memset(f->b, 0, sizeof(f->b));
}

void iotdrv_cgg1_layout_unit(struct iotdrv_cgg1_frame *f, enum iotdrv_cgg1_unit unit)
{
	const struct map *m = map_of(f);

	bit_set(f, m->deg, ((uint8_t)unit & 0x20U) != 0U);
	bit_set(f, m->dash, ((uint8_t)unit & 0x40U) != 0U);
	bit_set(f, m->under, ((uint8_t)unit & 0x80U) != 0U);
}

void iotdrv_cgg1_layout_smiley(struct iotdrv_cgg1_frame *f, bool on)
{
	bit_set(f, map_of(f)->smiley, on);
}

void iotdrv_cgg1_layout_ble(struct iotdrv_cgg1_frame *f, bool on)
{
	bit_set(f, map_of(f)->ble, on);
}

void iotdrv_cgg1_layout_battery(struct iotdrv_cgg1_frame *f, int8_t percent)
{
	static const int8_t thresholds[] = {16, 33, 49, 67, 83};
	const struct map *m = map_of(f);

	apply(f, m->bat_clear, false);
	if (percent < 0) {
		return;
	}

	bit_set(f, m->bat, true);
	for (size_t i = 0; i < sizeof(thresholds); i++) {
		if (percent < thresholds[i]) {
			break;
		}
		bit_set(f, m->bat_bar[i], true);
	}
}

/* The numbers share the rounding: above the point range the tenths are dropped */
static int16_t number_digits(struct iotdrv_cgg1_frame *f, int16_t number, int16_t point_max,
			     int16_t point_min, struct bit point, struct bit minus)
{
	int32_t n = number;

	if ((n > point_max) || (n < point_min)) {
		if (n < 0) {
			n = -n;
			bit_set(f, minus, true);
		}
		n = (n / 10) + (((n % 10) > 5) ? 1 : 0);
	} else {
		bit_set(f, point, true);
		if (n < 0) {
			n = -n;
			bit_set(f, minus, true);
		}
	}

	return (int16_t)n;
}

void iotdrv_cgg1_layout_big_number_x10(struct iotdrv_cgg1_frame *f, int16_t value)
{
	const struct map *m = map_of(f);
	int16_t n;

	apply(f, m->big_clear, false);
	if (value > 19995) {
		apply(f, m->big_hi, true);
		return;
	}
	if (value < -995) {
		apply(f, m->big_lo, true);
		return;
	}

	n = number_digits(f, value, 1995, -95, m->big_point, m->big_minus);
	if (n > 999) {
		bit_set(f, m->big_one, true);
	}
	if (n > 99) {
		set_digit(f, (uint8_t)((n / 100) % 10), TOP_LEFT);
	}
	set_digit(f, (n > 9) ? (uint8_t)((n / 10) % 10) : 0U, TOP_MIDDLE);
	set_digit(f, (uint8_t)(n % 10), TOP_RIGHT);
}

void iotdrv_cgg1_layout_small_number_x10(struct iotdrv_cgg1_frame *f, int16_t value, bool percent)
{
	const struct map *m = map_of(f);
	int16_t n;

	apply(f, m->small_clear, false);
	if (percent) {
		bit_set(f, m->small_percent, true);
	}
	if (value > 9995) {
		apply(f, m->small_hi, true);
		return;
	}
	if (value < -995) {
		apply(f, m->small_lo, true);
		return;
	}

	n = number_digits(f, value, 995, -95, m->small_point, m->small_minus);
	if (n > 99) {
		set_digit(f, (uint8_t)((n / 100) % 10), BOTTOM_LEFT);
	}
	set_digit(f, (n > 9) ? (uint8_t)((n / 10) % 10) : 0U, BOTTOM_MIDDLE);
	set_digit(f, (uint8_t)(n % 10), BOTTOM_RIGHT);
}

void iotdrv_cgg1_layout_small_number(struct iotdrv_cgg1_frame *f, int16_t value, bool percent)
{
	if (value > 99) {
		value = 10000;
	} else if (value < -9) {
		value = -1000;
	} else {
		value = (int16_t)(value * 10);
	}

	iotdrv_cgg1_layout_small_number_x10(f, value, percent);
}

void iotdrv_cgg1_layout_clock(struct iotdrv_cgg1_frame *f, uint8_t hours, uint8_t minutes)
{
	iotdrv_cgg1_layout_clear(f);
	set_digit(f, (minutes / 10U) % 10U, BOTTOM_LEFT);
	set_digit(f, minutes % 10U, BOTTOM_MIDDLE);
	set_digit(f, (hours / 10U) % 10U, TOP_LEFT);
	set_digit(f, hours % 10U, TOP_MIDDLE);
}
