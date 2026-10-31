/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "layout.h"

#define SEG(n) (1U << (n))

#define SEGMENTS 13

/*
 * Segments of a digit:
 *   12-----1-----2
 *    |           |
 *   11           3
 *    |           |
 *   10----13-----4
 *    |           |
 *    9           5
 *    |           |
 *    8-----7-----6
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

/* {byte, bit} pairs of the segments of each digit position */
static const uint8_t top_left[SEGMENTS * 2] = {2, 3, 5, 7, 3, 7, 3, 6, 3, 5, 3, 4, 2,
					       1, 2, 0, 2, 4, 2, 5, 2, 6, 2, 7, 2, 2};
static const uint8_t top_middle[SEGMENTS * 2] = {4, 7, 5, 6, 4, 3, 4, 2, 4, 1, 4, 0, 4,
						 5, 4, 4, 3, 0, 3, 1, 3, 2, 3, 3, 4, 6};
static const uint8_t top_right[SEGMENTS * 2] = {0, 6, 17, 3, 17, 2, 17, 1, 17, 0, 5, 5, 0,
						4, 0, 0, 0, 1, 0, 2, 0, 3, 0, 7, 0, 5};
static const uint8_t bottom_left[SEGMENTS * 2] = {6, 7, 8, 3, 6, 3, 6, 2, 6, 1, 6, 0, 6,
						  5, 6, 4, 5, 0, 5, 1, 5, 2, 5, 3, 6, 6};
static const uint8_t bottom_middle[SEGMENTS * 2] = {7, 3, 8, 2, 8, 7, 8, 6, 8, 5, 8, 4, 7,
						    1, 7, 0, 7, 4, 7, 5, 7, 6, 7, 7, 7, 2};
static const uint8_t bottom_right[SEGMENTS * 2] = {9, 3, 17, 7, 10, 7, 10, 6, 10, 5, 10, 4, 9,
						   1, 9, 0, 9, 4, 9, 5, 9, 6, 9, 7, 9, 2};

static void set_digit(uint8_t *buf, uint8_t digit, const uint8_t *segments)
{
	for (int i = 0; i < SEGMENTS; i++) {
		int segment = (int)digits[digit][i] - 1;

		if (segment < 0) {
			break;
		}
		buf[segments[2 * segment]] |= (uint8_t)SEG(segments[1 + 2 * segment]);
	}
}

void iotdrv_cgdk2_layout_big_number_x10(struct iotdrv_cgdk2_frame *f, int16_t number)
{
	uint8_t *buf = f->b;

	buf[0] = 0;
	buf[1] &= (uint8_t)~SEG(0);
	buf[2] = 0;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] &= (uint8_t) ~(SEG(4) | SEG(5) | SEG(6) | SEG(7));
	buf[17] &= (uint8_t) ~(SEG(0) | SEG(1) | SEG(2) | SEG(3));
	if (number > 19995) {
		/* "Hi" */
		buf[2] |= SEG(0) | SEG(2) | SEG(4) | SEG(5) | SEG(6) | SEG(7);
		buf[3] |= SEG(0) | SEG(1) | SEG(3) | SEG(4) | SEG(5) | SEG(6);
		buf[4] |= SEG(4);
	} else if (number < -995) {
		/* "Lo" */
		buf[2] |= SEG(0) | SEG(1) | SEG(4) | SEG(5) | SEG(6) | SEG(7);
		buf[3] |= SEG(0) | SEG(1) | SEG(4);
		buf[4] |= SEG(0) | SEG(1) | SEG(2) | SEG(4) | SEG(5) | SEG(6);
	} else {
		if (number > 1995 || number < -95) {
			/* no point: -99..1999 */
			if (number < 0) {
				number = -number;
				buf[2] |= SEG(2);
			}
			number = (int16_t)((number / 10) + ((number % 10) > 5));
		} else {
			/* -9.9..199.9 */
			buf[5] |= SEG(4);
			if (number < 0) {
				number = -number;
				buf[2] |= SEG(2);
			}
		}
		if (number > 999) {
			buf[1] |= SEG(0);
		}
		if (number > 99) {
			set_digit(buf, (uint8_t)(number / 100 % 10), top_left);
		}
		if (number > 9) {
			set_digit(buf, (uint8_t)(number / 10 % 10), top_middle);
		} else {
			set_digit(buf, 0, top_middle);
		}
		set_digit(buf, (uint8_t)(number % 10), top_right);
	}
}

void iotdrv_cgdk2_layout_small_number_x10(struct iotdrv_cgdk2_frame *f, int16_t number, bool percent)
{
	uint8_t *buf = f->b;

	buf[5] &= (uint8_t) ~(SEG(0) | SEG(1) | SEG(2) | SEG(3));
	buf[6] = 0;
	buf[7] = 0;
	buf[8] = 0;
	buf[9] = 0;
	buf[10] &= (uint8_t) ~(SEG(4) | SEG(5) | SEG(6) | SEG(7));
	buf[17] &= (uint8_t)~SEG(7);
	if (percent) {
		buf[8] |= SEG(1);
	}
	if (number > 9995) {
		/* "Hi" */
		buf[5] |= SEG(0) | SEG(1) | SEG(2) | SEG(3);
		buf[6] |= SEG(0) | SEG(1) | SEG(2) | SEG(4) | SEG(6);
		buf[7] |= SEG(0) | SEG(4) | SEG(5) | SEG(7);
	} else if (number < -995) {
		/* "Lo" */
		buf[5] |= SEG(0) | SEG(1) | SEG(2) | SEG(3);
		buf[6] |= SEG(0) | SEG(4) | SEG(5);
		buf[7] |= SEG(0) | SEG(1) | SEG(2) | SEG(4) | SEG(5);
		buf[8] |= SEG(4) | SEG(5) | SEG(6);
	} else {
		if (number > 995 || number < -95) {
			/* no point: -99..999 */
			if (number < 0) {
				number = -number;
				buf[6] |= SEG(6);
			}
			number = (int16_t)((number / 10) + ((number % 10) > 5));
		} else {
			/* -9.9..99.9 */
			buf[8] |= SEG(0);
			if (number < 0) {
				number = -number;
				buf[6] |= SEG(6);
			}
		}
		if (number > 99) {
			set_digit(buf, (uint8_t)(number / 100 % 10), bottom_left);
		}
		if (number > 9) {
			set_digit(buf, (uint8_t)(number / 10 % 10), bottom_middle);
		} else {
			set_digit(buf, 0, bottom_middle);
		}
		set_digit(buf, (uint8_t)(number % 10), bottom_right);
	}
}

void iotdrv_cgdk2_layout_small_number(struct iotdrv_cgdk2_frame *f, int16_t value, bool percent)
{
	/* Anything beyond the x10 range shows Hi or Lo anyway, so clamp instead of overflowing */
	int32_t x10 = (int32_t)value * 10;

	if (x10 > INT16_MAX) {
		x10 = INT16_MAX;
	} else if (x10 < INT16_MIN) {
		x10 = INT16_MIN;
	}

	iotdrv_cgdk2_layout_small_number_x10(f, (int16_t)x10, percent);
}

void iotdrv_cgdk2_layout_unit(struct iotdrv_cgdk2_frame *f, enum iotdrv_cgdk2_unit unit)
{
	uint8_t symbol = (uint8_t)unit;

	if ((symbol & 0x20U) != 0U) {
		f->b[17] |= SEG(6);
	} else {
		f->b[17] &= (uint8_t)~SEG(6);
	}
	if ((symbol & 0x40U) != 0U) {
		f->b[17] |= SEG(5);
	} else {
		f->b[17] &= (uint8_t)~SEG(5);
	}
	if ((symbol & 0x80U) != 0U) {
		f->b[17] |= SEG(4);
	} else {
		f->b[17] &= (uint8_t)~SEG(4);
	}
}

void iotdrv_cgdk2_layout_ble(struct iotdrv_cgdk2_frame *f, bool on)
{
	if (on) {
		f->b[1] |= SEG(4);
	} else {
		f->b[1] &= (uint8_t)~SEG(4);
	}
}

void iotdrv_cgdk2_layout_battery(struct iotdrv_cgdk2_frame *f, int8_t percent)
{
	f->b[1] &= (uint8_t) ~(SEG(1) | SEG(2) | SEG(3) | SEG(5) | SEG(6) | SEG(7));
	if (percent < 0) {
		return;
	}

	f->b[1] |= SEG(5);
	if (percent >= 16) {
		f->b[1] |= SEG(1);
	}
	if (percent >= 33) {
		f->b[1] |= SEG(2);
	}
	if (percent >= 49) {
		f->b[1] |= SEG(3);
	}
	if (percent >= 67) {
		f->b[1] |= SEG(7);
	}
	if (percent >= 83) {
		f->b[1] |= SEG(6);
	}
}

void iotdrv_cgdk2_layout_clock(struct iotdrv_cgdk2_frame *f, uint8_t hours, uint8_t minutes)
{
	memset(f->b, 0, sizeof(f->b));
	set_digit(f->b, (minutes / 10) % 10, bottom_left);
	set_digit(f->b, minutes % 10, bottom_middle);
	set_digit(f->b, (hours / 10) % 10, top_left);
	set_digit(f->b, hours % 10, top_middle);
}
