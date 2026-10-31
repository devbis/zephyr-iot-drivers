/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include "layout.h"

#define B(n) (1U << (n))

#define SEGMENTS 11U

/* {byte, bit} pairs of every segment of a digit, as in the ATC firmware */
struct digit_map {
	uint8_t seg[2U * SEGMENTS];
};

static const struct digit_map c401_top_left = {
	{16, 7, 15, 4, 14, 1, 14, 7, 12, 5, 12, 4, 13, 3, 15, 7, 15, 6, 15, 5, 14, 0}};
static const struct digit_map c401_top_middle = {
	{15, 0, 15, 1, 14, 6, 13, 0, 13, 5, 13, 4, 14, 5, 14, 4, 15, 3, 15, 2, 14, 3}};
static const struct digit_map c401_top_right = {
	{13, 1, 13, 7, 12, 1, 12, 7, 11, 5, 11, 2, 12, 6, 12, 0, 13, 6, 13, 2, 12, 2}};
static const struct digit_map c401_bottom_left = {
	{9, 1, 9, 7, 8, 5, 1, 1, 0, 3, 1, 4, 9, 4, 10, 0, 10, 6, 10, 3, 8, 2}};
static const struct digit_map c401_bottom_right = {
	{7, 7, 6, 5, 2, 0, 2, 3, 0, 2, 1, 7, 2, 6, 7, 4, 7, 1, 8, 6, 6, 2}};

static const struct digit_map c401n_top_left = {
	{8, 7, 9, 2, 9, 1, 9, 0, 8, 5, 8, 0, 8, 1, 8, 2, 8, 3, 8, 4, 8, 6}};
static const struct digit_map c401n_top_middle = {
	{10, 7, 11, 2, 11, 1, 11, 0, 10, 5, 10, 0, 10, 1, 10, 2, 10, 3, 10, 4, 10, 6}};
static const struct digit_map c401n_top_right = {
	{12, 7, 13, 2, 13, 1, 13, 0, 12, 5, 12, 0, 12, 1, 12, 2, 12, 3, 12, 4, 12, 6}};
static const struct digit_map c401n_bottom_left = {
	{1, 7, 2, 2, 2, 1, 2, 0, 1, 5, 1, 0, 1, 1, 1, 2, 1, 3, 1, 4, 1, 6}};
static const struct digit_map c401n_bottom_right = {
	{3, 7, 4, 2, 4, 1, 4, 0, 3, 5, 3, 0, 3, 1, 3, 2, 3, 3, 3, 4, 3, 6}};

/*
 * Segments of every digit 0..F, numbered as follows, 0 ends the list:
 *           1
 *   10 :-----------
 *      |           |
 *    9 |           | 2
 *      |     11    |
 *    8 :-----------: 3
 *      |           |
 *    7 |           | 4
 *      |     5     |
 *    6 :-----------
 */
static const uint8_t digits[16][SEGMENTS] = {
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0},  /* 0 */
	{2, 3, 4, 0, 0, 0, 0, 0, 0, 0, 0},   /* 1 */
	{1, 2, 3, 5, 6, 7, 8, 10, 11, 0, 0}, /* 2 */
	{1, 2, 3, 4, 5, 6, 10, 11, 0, 0, 0}, /* 3 */
	{2, 3, 4, 8, 9, 10, 11, 0, 0, 0, 0}, /* 4 */
	{1, 3, 4, 5, 6, 8, 9, 10, 11, 0, 0}, /* 5 */
	{1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0}, /* 6 */
	{1, 2, 3, 4, 10, 0, 0, 0, 0, 0, 0},  /* 7 */
	{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, /* 8 */
	{1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 0}, /* 9 */
	{1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 0}, /* A */
	{3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 0}, /* b */
	{5, 6, 7, 8, 11, 0, 0, 0, 0, 0, 0},  /* c */
	{2, 3, 4, 5, 6, 7, 8, 11, 0, 0, 0},  /* d */
	{1, 5, 6, 7, 8, 9, 10, 11, 0, 0, 0}, /* E */
	{1, 6, 7, 8, 9, 10, 11, 0, 0, 0, 0}  /* F */
};

static void set_digit(struct iotdrv_mho_c401_frame *f, uint8_t digit, const struct digit_map *map)
{
	for (size_t i = 0; i < SEGMENTS; i++) {
		uint8_t segment = digits[digit][i];

		if (segment == 0U) {
			break;
		}
		f->b[map->seg[2U * (segment - 1U)]] |= (uint8_t)B(map->seg[2U * (segment - 1U) + 1U]);
	}
}

size_t iotdrv_mho_c401_layout_size(enum iotdrv_mho_c401_layout_variant v)
{
	return (v == IOTDRV_MHO_C401_LAYOUT_C401N) ? 16U : IOTDRV_MHO_C401_FRAME_SIZE;
}

void iotdrv_mho_c401_layout_clear(struct iotdrv_mho_c401_frame *f)
{
	memset(f, 0, sizeof(*f));
}

static void c401_big_number_x10(struct iotdrv_mho_c401_frame *f, int16_t value)
{
	int32_t n = value;

	/* Keep the battery symbol and the unit; the point bit is set below */
	f->b[11] = 0U;
	f->b[12] = 0U;
	f->b[13] = 0U;
	f->b[14] &= (uint8_t)B(2);
	f->b[15] = 0U;
	f->b[16] &= (uint8_t)(B(4) | B(6));

	if (n > 19995) {
		/* "Hi" */
		f->b[12] = (uint8_t)B(4);
		f->b[13] = (uint8_t)(B(0) | B(3));
		f->b[14] |= (uint8_t)(B(0) | B(1) | B(6) | B(7));
		f->b[15] = (uint8_t)(B(4) | B(5) | B(6) | B(7));
		return;
	}

	if (n < -995) {
		/* "Lo" */
		f->b[12] = (uint8_t)(B(4) | B(5));
		f->b[13] = (uint8_t)(B(0) | B(3) | B(4) | B(5));
		f->b[14] |= (uint8_t)(B(3) | B(4) | B(5));
		f->b[15] = (uint8_t)(B(5) | B(6) | B(7));
		return;
	}

	if ((n > 1995) || (n < -95)) {
		/* no point: -99..1999 */
		if (n < 0) {
			n = -n;
			f->b[14] |= (uint8_t)B(0);
		}
		n = (n / 10) + (((n % 10) > 5) ? 1 : 0);
	} else {
		/* with point: -9.9..199.9 */
		f->b[16] |= (uint8_t)B(5);
		if (n < 0) {
			n = -n;
			f->b[14] |= (uint8_t)B(0);
		}
	}

	if (n > 999) {
		f->b[12] |= (uint8_t)B(3);
	}
	if (n > 99) {
		set_digit(f, (uint8_t)((n / 100) % 10), &c401_top_left);
	}
	set_digit(f, (uint8_t)((n > 9) ? ((n / 10) % 10) : 0), &c401_top_middle);
	set_digit(f, (uint8_t)(n % 10), &c401_top_right);
}

static void c401n_big_number_x10(struct iotdrv_mho_c401_frame *f, int16_t value)
{
	int32_t n = value;

	for (size_t i = 8; i < 14U; i++) {
		f->b[i] = 0U;
	}
	f->b[7] &= (uint8_t)~B(4);

	if (n > 19995) {
		/* "Hi" */
		f->b[8] = (uint8_t)B(4);
		f->b[9] = (uint8_t)(B(0) | B(3));
		f->b[10] = (uint8_t)(B(0) | B(1) | B(6) | B(7));
		f->b[11] = (uint8_t)(B(4) | B(5) | B(6) | B(7));
		return;
	}

	if (n < -995) {
		/* "Lo" */
		f->b[8] = (uint8_t)(B(4) | B(5));
		f->b[9] = (uint8_t)(B(0) | B(3) | B(4) | B(5));
		f->b[10] = (uint8_t)(B(3) | B(4) | B(5));
		f->b[11] = (uint8_t)(B(5) | B(6) | B(7));
		return;
	}

	if ((n > 1995) || (n < -95)) {
		/* no point: -99..1999 */
		if (n < 0) {
			n = -n;
			f->b[8] = (uint8_t)B(6);
		}
		n = (n / 10) + (((n % 10) > 5) ? 1 : 0);
	} else {
		/* with point: -9.9..199.9 */
		f->b[11] |= (uint8_t)B(4);
		if (n < 0) {
			n = -n;
			f->b[8] = (uint8_t)B(6);
		}
	}

	if (n > 999) {
		f->b[7] |= (uint8_t)B(4);
	}
	if (n > 99) {
		set_digit(f, (uint8_t)((n / 100) % 10), &c401n_top_left);
	}
	set_digit(f, (uint8_t)((n > 9) ? ((n / 10) % 10) : 0), &c401n_top_middle);
	set_digit(f, (uint8_t)(n % 10), &c401n_top_right);
}

void iotdrv_mho_c401_layout_big_number_x10(struct iotdrv_mho_c401_frame *f,
				       enum iotdrv_mho_c401_layout_variant v, int16_t value)
{
	if (v == IOTDRV_MHO_C401_LAYOUT_C401N) {
		c401n_big_number_x10(f, value);
	} else {
		c401_big_number_x10(f, value);
	}
}

static void c401_small_number(struct iotdrv_mho_c401_frame *f, int16_t value)
{
	f->b[0] = 0U;
	f->b[1] = 0U;
	f->b[2] = 0U;
	for (size_t i = 6; i <= 10U; i++) {
		f->b[i] = 0U;
	}

	if (value > 99) {
		/* "Hi" */
		f->b[1] |= (uint8_t)(B(1) | B(4));
		f->b[2] |= (uint8_t)(B(0) | B(3));
		f->b[8] |= (uint8_t)(B(2) | B(5));
		f->b[9] |= (uint8_t)(B(4) | B(7));
		f->b[10] |= (uint8_t)(B(0) | B(3) | B(6));
		return;
	}

	if (value < -9) {
		/* "Lo" */
		f->b[0] |= (uint8_t)(B(2) | B(3));
		f->b[1] |= (uint8_t)(B(4) | B(7));
		f->b[2] |= (uint8_t)(B(6) | B(3));
		f->b[6] |= (uint8_t)B(2);
		f->b[9] |= (uint8_t)B(4);
		f->b[10] |= (uint8_t)(B(0) | B(3) | B(6));
		return;
	}

	if (value < 0) {
		value = (int16_t)-value;
		f->b[8] |= (uint8_t)B(2);
	}
	if (value > 9) {
		set_digit(f, (uint8_t)((value / 10) % 10), &c401_bottom_left);
	}
	set_digit(f, (uint8_t)(value % 10), &c401_bottom_right);
}

static void c401n_small_number(struct iotdrv_mho_c401_frame *f, int16_t value, bool percent)
{
	for (size_t i = 1; i <= 4U; i++) {
		f->b[i] = 0U;
	}
	f->b[5] &= (uint8_t)~B(0);
	if (percent) {
		f->b[5] |= (uint8_t)B(0);
	}

	if (value > 99) {
		/* "Hi" */
		f->b[1] |= (uint8_t)(B(1) | B(4));
		f->b[2] |= (uint8_t)(B(0) | B(3));
		f->b[3] |= (uint8_t)(B(2) | B(5));
		f->b[4] |= (uint8_t)(B(4) | B(7));
		return;
	}

	if (value < -9) {
		/* "Lo" */
		f->b[1] |= (uint8_t)(B(2) | B(3));
		f->b[2] |= (uint8_t)(B(4) | B(7));
		f->b[3] |= (uint8_t)(B(6) | B(3));
		f->b[4] |= (uint8_t)B(2);
		return;
	}

	if (value < 0) {
		value = (int16_t)-value;
		f->b[1] |= (uint8_t)B(6);
	}
	if (value > 9) {
		set_digit(f, (uint8_t)((value / 10) % 10), &c401n_bottom_left);
	}
	set_digit(f, (uint8_t)(value % 10), &c401n_bottom_right);
}

void iotdrv_mho_c401_layout_small_number(struct iotdrv_mho_c401_frame *f,
				     enum iotdrv_mho_c401_layout_variant v, int16_t value,
				     bool percent)
{
	if (v == IOTDRV_MHO_C401_LAYOUT_C401N) {
		c401n_small_number(f, value, percent);
	} else {
		c401_small_number(f, value);
	}
}

static void put_bit(struct iotdrv_mho_c401_frame *f, size_t byte, unsigned int bit, bool on)
{
	if (on) {
		f->b[byte] |= (uint8_t)B(bit);
	} else {
		f->b[byte] &= (uint8_t)~B(bit);
	}
}

void iotdrv_mho_c401_layout_unit(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			     enum iotdrv_mho_c401_layout_unit unit)
{
	bool deg = (unit != IOTDRV_MHO_C401_LAYOUT_UNIT_NONE);
	bool bar = (unit == IOTDRV_MHO_C401_LAYOUT_UNIT_F);
	bool low = (unit == IOTDRV_MHO_C401_LAYOUT_UNIT_C);

	if (v == IOTDRV_MHO_C401_LAYOUT_C401N) {
		put_bit(f, 14, 4, deg);
		put_bit(f, 14, 2, bar);
		put_bit(f, 14, 0, low);
	} else {
		/* The degree bit is the point bit, owned by the big number */
		put_bit(f, 16, 6, bar);
		put_bit(f, 14, 2, low);
	}
}

static void c401_smiley(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_smiley smiley)
{
	f->b[3] = 0U;
	f->b[4] = 0U;
	f->b[5] &= (uint8_t)B(0);

	if (smiley == IOTDRV_MHO_C401_LAYOUT_SMILEY_HAPPY) {
		f->b[3] |= (uint8_t)B(2);
		f->b[4] |= (uint8_t)B(1);
	} else if (smiley == IOTDRV_MHO_C401_LAYOUT_SMILEY_SAD) {
		f->b[4] |= (uint8_t)B(7);
		f->b[5] |= (uint8_t)B(6);
	}
}

static void c401n_smiley(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_smiley smiley)
{
	f->b[5] &= (uint8_t)~(B(2) | B(4) | B(6));
	f->b[6] = 0U;
	put_bit(f, 7, 0, smiley != IOTDRV_MHO_C401_LAYOUT_SMILEY_OFF);

	if (smiley == IOTDRV_MHO_C401_LAYOUT_SMILEY_HAPPY) {
		f->b[5] |= (uint8_t)(B(4) | B(6));
		f->b[6] |= (uint8_t)(B(4) | B(6));
	} else if (smiley == IOTDRV_MHO_C401_LAYOUT_SMILEY_SAD) {
		f->b[6] |= (uint8_t)(B(0) | B(4) | B(6));
	}
}

void iotdrv_mho_c401_layout_smiley(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			       enum iotdrv_mho_c401_layout_smiley smiley)
{
	if (v == IOTDRV_MHO_C401_LAYOUT_C401N) {
		c401n_smiley(f, smiley);
	} else {
		c401_smiley(f, smiley);
	}
}

void iotdrv_mho_c401_layout_ble(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			    bool on)
{
	if (v == IOTDRV_MHO_C401_LAYOUT_C401N) {
		put_bit(f, 0, 4, on);
	} else {
		put_bit(f, 5, 0, on);
	}
}

void iotdrv_mho_c401_layout_battery(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
				bool on)
{
	if (v == IOTDRV_MHO_C401_LAYOUT_C401N) {
		put_bit(f, 14, 6, on);
	} else {
		put_bit(f, 16, 4, on);
	}
}

void iotdrv_mho_c401_layout_clock(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			      uint8_t hours, uint8_t minutes)
{
	bool n = (v == IOTDRV_MHO_C401_LAYOUT_C401N);

	iotdrv_mho_c401_layout_clear(f);
	set_digit(f, (minutes / 10U) % 10U, n ? &c401n_bottom_left : &c401_bottom_left);
	set_digit(f, minutes % 10U, n ? &c401n_bottom_right : &c401_bottom_right);
	set_digit(f, (hours / 10U) % 10U, n ? &c401n_top_left : &c401_top_left);
	set_digit(f, hours % 10U, n ? &c401n_top_middle : &c401_top_middle);
}
