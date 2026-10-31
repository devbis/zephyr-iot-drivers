/* SPDX-License-Identifier: Apache-2.0 */

#include "layout.h"

#define SEG(n) (1U << (n))

#define SYM_H   0x67U
#define SYM_I   0x04U
#define SYM_L   0x85U
#define SYM_O   0xC6U
#define SYM_BLE SEG(7)
#define SYM_BAT SEG(7)

/* 0..9, A, b, C, d, E, F */
static const uint8_t display_numbers[] = {0xF5, 0x60, 0xB6, 0xF2, 0x63, 0xD3, 0xD7, 0x70,
					  0xF7, 0xF3, 0x77, 0xC7, 0x95, 0xE6, 0x97, 0x17};
static const uint8_t display_small_numbers[] = {0x5F, 0x06, 0x3D, 0x2F, 0x66, 0x6B, 0x7B, 0x0E,
						0x7F, 0x6F, 0x7E, 0x73, 0x59, 0x37, 0x79, 0x78};

void iotdrv_mho_c122_layout_unit(struct iotdrv_mho_c122_frame *f, enum iotdrv_mho_c122_unit unit)
{
	uint8_t symbol = (uint8_t)unit;

	f->b[3] &= (uint8_t) ~(SEG(0) | SEG(1) | SEG(2));
	if ((symbol & 0x20U) != 0U) {
		f->b[3] |= SEG(0);
	}
	if ((symbol & 0x40U) != 0U) {
		f->b[3] |= SEG(1);
	}
	if ((symbol & 0x80U) != 0U) {
		f->b[3] |= SEG(2);
	}
}

void iotdrv_mho_c122_layout_smiley(struct iotdrv_mho_c122_frame *f, enum iotdrv_mho_c122_smiley smiley)
{
	uint8_t state = (uint8_t)smiley;

	f->b[1] &= (uint8_t)~SEG(3);
	f->b[3] &= (uint8_t) ~(SEG(5) | SEG(6));

	if ((state & 1U) != 0U) {
		f->b[3] |= SEG(5);
	}
	if ((state & 2U) != 0U) {
		f->b[3] |= SEG(6);
	}
	if ((state & 4U) != 0U) {
		f->b[1] |= SEG(3);
	}
}

void iotdrv_mho_c122_layout_ble(struct iotdrv_mho_c122_frame *f, bool on)
{
	if (on) {
		f->b[4] |= SYM_BLE;
	} else {
		f->b[4] &= (uint8_t)~SYM_BLE;
	}
}

void iotdrv_mho_c122_layout_battery(struct iotdrv_mho_c122_frame *f, bool on)
{
	if (on) {
		f->b[5] |= SYM_BAT;
	} else {
		f->b[5] &= (uint8_t)~SYM_BAT;
	}
}

void iotdrv_mho_c122_layout_big_number_x10(struct iotdrv_mho_c122_frame *f, int16_t number)
{
	uint8_t *buf = f->b;

	buf[0] = 0;
	buf[1] &= SEG(3); /* keep the smiley contour */
	buf[2] = 0;

	if (number > 19995) {
		buf[0] |= SYM_H;
		buf[1] |= SYM_I;
	} else if (number < -995) {
		buf[0] |= SYM_L;
		buf[1] |= SYM_O;
	} else {
		if (number > 1995 || number < -95) {
			/* no point: -99..1999 */
			if (number < 0) {
				number = -number;
				buf[0] |= SEG(1);
			}
			number = (int16_t)((number / 10) + ((number % 10) > 5));
		} else {
			/* -9.9..199.9 */
			buf[2] = SEG(3);
			if (number < 0) {
				number = -number;
				buf[0] |= SEG(1);
			}
		}
		if (number > 999) {
			buf[0] |= SEG(3);
		}
		if (number > 99) {
			buf[0] |= display_numbers[number / 100 % 10];
		}
		if (number > 9) {
			buf[1] |= display_numbers[number / 10 % 10];
		} else {
			buf[1] |= display_numbers[0];
		}
		buf[2] |= display_numbers[number % 10];
	}
}

void iotdrv_mho_c122_layout_small_number(struct iotdrv_mho_c122_frame *f, int16_t number, bool percent)
{
	uint8_t *buf = f->b;

	buf[4] &= SYM_BLE;
	buf[5] &= SYM_BAT;

	if (percent) {
		buf[3] |= SEG(4);
	} else {
		buf[3] &= (uint8_t)~SEG(4);
	}

	if (number > 99) {
		buf[5] |= SEG(1) | SEG(2) | SEG(4) | SEG(5) | SEG(6);
		buf[4] |= SEG(4);
	} else if (number < -9) {
		buf[5] |= SEG(0) | SEG(4) | SEG(6);
		buf[4] |= SEG(0) | SEG(1) | SEG(4) | SEG(5);
	} else {
		if (number < 0) {
			number = -number;
			buf[5] |= SEG(5);
		}
		if (number > 9) {
			buf[5] |= display_small_numbers[number / 10 % 10];
		}
		buf[4] |= display_small_numbers[number % 10];
	}
}

void iotdrv_mho_c122_layout_clock(struct iotdrv_mho_c122_frame *f, uint8_t hours, uint8_t minutes)
{
	uint8_t *buf = f->b;

	buf[0] = display_numbers[(hours / 10) % 10];
	buf[1] = display_numbers[hours % 10];
	buf[2] = 0;
	buf[3] = 0;

	buf[4] &= SYM_BLE;
	buf[5] &= SYM_BAT;

	buf[4] |= display_small_numbers[minutes % 10];
	buf[5] |= display_small_numbers[(minutes / 10) % 10];
}
