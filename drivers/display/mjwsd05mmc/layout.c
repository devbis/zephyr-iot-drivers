/* SPDX-License-Identifier: Apache-2.0 */

#include "layout.h"

#define BIT(n) (1U << (n))

#define SEGMENTS 7

/*
 *     -----1-----
 *    |           |
 *    6           2
 *    |           |
 *     -----7-----
 *    |           |
 *    5           3
 *    |           |
 *     -----4-----
 */
static const uint8_t digits[16][SEGMENTS + 1] = {
	{1, 2, 3, 4, 5, 6, 0, 0}, /* 0 */
	{2, 3, 0, 0, 0, 0, 0, 0}, /* 1 */
	{1, 2, 4, 5, 7, 0, 0, 0}, /* 2 */
	{1, 2, 3, 4, 7, 0, 0, 0}, /* 3 */
	{2, 3, 6, 7, 0, 0, 0, 0}, /* 4 */
	{1, 3, 4, 6, 7, 0, 0, 0}, /* 5 */
	{1, 3, 4, 5, 6, 7, 0, 0}, /* 6 */
	{1, 2, 3, 0, 0, 0, 0, 0}, /* 7 */
	{1, 2, 3, 4, 5, 6, 7, 0}, /* 8 */
	{1, 2, 3, 4, 6, 7, 0, 0}, /* 9 */
	{1, 2, 3, 5, 6, 7, 0, 0}, /* A */
	{3, 4, 5, 6, 7, 0, 0, 0}, /* b */
	{1, 4, 5, 6, 0, 0, 0, 0}, /* C */
	{2, 3, 4, 5, 7, 0, 0, 0}, /* d */
	{1, 4, 5, 6, 7, 0, 0, 0}, /* E */
	{1, 5, 6, 7, 0, 0, 0, 0}  /* F */
};

/* {byte, bit mask} of each segment of a digit */
static const uint8_t sb_s1[4][SEGMENTS * 2] = {
	{3, 0x10, 3, 0x20, 3, 0x40, 3, 0x80, 3, 0x08, 3, 0x02, 3, 0x04},
	{2, 0x10, 2, 0x20, 2, 0x40, 2, 0x80, 2, 0x08, 2, 0x02, 2, 0x04},
	{1, 0x10, 1, 0x20, 1, 0x40, 1, 0x80, 1, 0x08, 1, 0x02, 1, 0x04},
	{17, 0x01, 17, 0x02, 17, 0x04, 17, 0x08, 0, 0x08, 0, 0x02, 0, 0x04}
};

static const uint8_t sb_s3[4][SEGMENTS * 2] = {
	{9, 0x10, 9, 0x20, 9, 0x40, 9, 0x80, 8, 0x08, 8, 0x02, 8, 0x04},
	{10, 0x10, 10, 0x20, 10, 0x40, 10, 0x80, 9, 0x08, 9, 0x02, 9, 0x04},
	{11, 0x10, 11, 0x20, 11, 0x40, 11, 0x80, 10, 0x08, 10, 0x02, 10, 0x04},
	{12, 0x10, 12, 0x20, 12, 0x40, 12, 0x80, 11, 0x08, 11, 0x02, 11, 0x04}
};

static const uint8_t sb_s4[4][SEGMENTS * 2] = {
	{13, 0x10, 13, 0x20, 13, 0x40, 13, 0x80, 12, 0x08, 12, 0x02, 12, 0x04},
	{14, 0x10, 14, 0x20, 14, 0x40, 14, 0x80, 13, 0x08, 13, 0x02, 13, 0x04},
	{15, 0x10, 15, 0x20, 15, 0x40, 15, 0x80, 14, 0x08, 14, 0x02, 14, 0x04}
};

static void set_digit(struct iotdrv_mjwsd05mmc_frame *f, uint8_t digit, const uint8_t *segments)
{
	for (uint8_t i = 0U; i < SEGMENTS; i++) {
		int32_t segment = (int32_t)digits[digit][i] - 1;

		if (segment < 0) {
			break;
		}
		f->b[segments[2 * segment]] |= segments[1 + 2 * segment];
	}
}

/* Blank the digits of the big group but keep its unit, BLE, battery and smiley bits */
static void clear_s1(struct iotdrv_mjwsd05mmc_frame *f)
{
	f->b[0] &= (uint8_t)~0x0EU;
	f->b[1] = 0U;
	f->b[2] &= BIT(0);
	f->b[3] &= BIT(0);
	f->b[4] &= (uint8_t)~BIT(4);
	f->b[13] &= (uint8_t)~BIT(0);
	f->b[17] &= (uint8_t)~0x0FU;
}

/* Blank the clock group, including its unit symbols */
static void clear_s3(struct iotdrv_mjwsd05mmc_frame *f)
{
	f->b[7] &= (uint8_t)~BIT(0);
	f->b[8] &= (uint8_t)~0x0FU;
	f->b[9] = 0U;
	f->b[10] = 0U;
	f->b[11] = 0U;
	f->b[12] &= (uint8_t)~0xF0U;
}

static void clear_s4(struct iotdrv_mjwsd05mmc_frame *f)
{
	f->b[12] &= (uint8_t)~0x0FU;
	f->b[13] &= BIT(0);
	f->b[14] = 0U;
	f->b[15] = 0U;
}

void iotdrv_mjwsd05mmc_layout_big_number_x10(struct iotdrv_mjwsd05mmc_frame *f, int16_t value)
{
	int32_t number = value;

	clear_s1(f);

	/* The panel shows -99..1999 without the point, so the rounded value must fit it */
	if (number >= 19995) {
		/* "Hi" */
		f->b[1] |= BIT(3);
		f->b[2] |= BIT(1) | BIT(2) | BIT(3) | BIT(5) | BIT(6);
		return;
	}

	if (number <= -995) {
		/* "Lo" */
		f->b[1] |= BIT(2) | BIT(3) | BIT(6) | BIT(7);
		f->b[2] |= BIT(1) | BIT(3) | BIT(7);
		return;
	}

	if ((number > 1999) || (number < -99)) {
		/* no point, show: -99..1999 */
		if (number < 0) {
			number = -number;
			f->b[3] |= BIT(2); /* "-" */
		}
		number = (number / 10) + (((number % 10) >= 5) ? 1 : 0);
	} else {
		/* show: -9.9..199.9 */
		f->b[13] |= BIT(0); /* point */
		if (number < 0) {
			number = -number;
			f->b[3] |= BIT(2); /* "-" */
		}
	}

	if (number > 999) {
		f->b[4] |= BIT(4); /* "1" */
	}
	if (number > 99) {
		set_digit(f, (uint8_t)((number / 100) % 10), sb_s1[0]);
	}
	if (number > 9) {
		set_digit(f, (uint8_t)((number / 10) % 10), sb_s1[1]);
	} else {
		set_digit(f, 0U, sb_s1[1]);
	}
	set_digit(f, (uint8_t)(number % 10), sb_s1[2]);
}

void iotdrv_mjwsd05mmc_layout_small_number(struct iotdrv_mjwsd05mmc_frame *f, int16_t value,
				       bool percent)
{
	/* Shown in 0.1 steps like the vendor firmware: 45 reads 45.0 */
	int32_t number = (int32_t)value * 10;

	clear_s4(f);

	if (value > 99) {
		/* "Hi" */
		f->b[13] |= BIT(1) | BIT(2) | BIT(3);
		f->b[14] |= BIT(3) | BIT(5) | BIT(6);
	} else if (value < -9) {
		/* "Lo" */
		f->b[13] |= BIT(1) | BIT(3) | BIT(7);
		f->b[14] |= BIT(2) | BIT(3);
		f->b[15] |= BIT(6) | BIT(7);
	} else {
		f->b[14] |= BIT(0); /* point */
		if (number < 0) {
			number = -number;
			f->b[12] |= BIT(2); /* "-" */
		}
		if (number > 99) {
			set_digit(f, (uint8_t)((number / 100) % 10), sb_s4[0]);
		}
		if (number > 9) {
			set_digit(f, (uint8_t)((number / 10) % 10), sb_s4[1]);
		} else {
			set_digit(f, 0U, sb_s4[1]);
		}
		set_digit(f, (uint8_t)(number % 10), sb_s4[2]);
	}

	if (percent) {
		f->b[15] |= BIT(3);
	}
}

void iotdrv_mjwsd05mmc_layout_unit(struct iotdrv_mjwsd05mmc_frame *f, enum iotdrv_mjwsd05mmc_unit unit)
{
	/* bits 4..6 of byte 0: degree sign, "-" and "|" */
	f->b[0] = (uint8_t)((f->b[0] & ~0x70U) | (((uint32_t)unit & 0x07U) << 4));
}

void iotdrv_mjwsd05mmc_layout_smiley(struct iotdrv_mjwsd05mmc_frame *f, enum iotdrv_mjwsd05mmc_smiley smiley)
{
	uint8_t state = (uint8_t)smiley;

	f->b[17] &= (uint8_t)~(BIT(5) | BIT(6) | BIT(7));
	if ((state & 1U) != 0U) {
		f->b[17] |= BIT(6);
	}
	if ((state & 2U) != 0U) {
		f->b[17] |= BIT(7);
	}
	if ((state & 4U) != 0U) {
		f->b[17] |= BIT(5);
	}
}

void iotdrv_mjwsd05mmc_layout_ble(struct iotdrv_mjwsd05mmc_frame *f, bool on)
{
	if (on) {
		f->b[0] |= BIT(0);
	} else {
		f->b[0] &= (uint8_t)~BIT(0);
	}
}

void iotdrv_mjwsd05mmc_layout_battery(struct iotdrv_mjwsd05mmc_frame *f, bool on)
{
	if (on) {
		f->b[17] |= BIT(4);
	} else {
		f->b[17] &= (uint8_t)~BIT(4);
	}
}

void iotdrv_mjwsd05mmc_layout_clock(struct iotdrv_mjwsd05mmc_frame *f, uint8_t hours, uint8_t minutes)
{
	clear_s3(f);
	f->b[10] = BIT(0); /* ":" */
	set_digit(f, (hours / 10U) % 10U, sb_s3[0]);
	set_digit(f, hours % 10U, sb_s3[1]);
	set_digit(f, (minutes / 10U) % 10U, sb_s3[2]);
	set_digit(f, minutes % 10U, sb_s3[3]);
}
