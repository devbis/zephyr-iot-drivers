/* SPDX-License-Identifier: Apache-2.0 */

#include <iot_drivers/lywsd03mmc.h>

#define SYM_H    0x67U
#define SYM_I    0x40U
#define SYM_L    0xE0U
#define SYM_O    0xC6U
#define SYM_MINUS 0x02U
#define SYM_POINT 0x08U
#define SYM_ONE_HUNDREDS 0x08U
#define SYM_BLE  0x10U
#define SYM_BAT  0x08U
#define SYM_PERCENT 0x08U

/* 0..9 */
static const uint8_t digits[] = {0xF5, 0x05, 0xD3, 0x97, 0x27, 0xB6, 0xF6, 0x15, 0xF7, 0xB7};

void iotdrv_lywsd03mmc_big_number_x10(struct iotdrv_lywsd03mmc_frame *f, int16_t value)
{
	int32_t n = value;

	if (n > 19995) {
		f->b[3] = 0U;
		f->b[4] = SYM_I;
		f->b[5] = SYM_H;
		return;
	}

	if (n < -995) {
		f->b[3] = 0U;
		f->b[4] = SYM_O;
		f->b[5] = SYM_L;
		return;
	}

	f->b[5] = 0U;
	if ((n > 1995) || (n < -95)) {
		/* no point: -99..1999 */
		f->b[4] = 0U;
		if (n < 0) {
			n = -n;
			f->b[5] = SYM_MINUS;
		}
		n = (n / 10) + (((n % 10) > 5) ? 1 : 0);
	} else {
		/* with point: -9.9..199.9 */
		f->b[4] = SYM_POINT;
		if (n < 0) {
			n = -n;
			f->b[5] = SYM_MINUS;
		}
	}

	if (n > 999) {
		f->b[5] |= SYM_ONE_HUNDREDS;
	}
	if (n > 99) {
		f->b[5] |= digits[(n / 100) % 10];
	}
	if (n > 9) {
		f->b[4] |= digits[(n / 10) % 10];
	} else {
		f->b[4] |= digits[0];
	}
	f->b[3] = digits[n % 10];
}

void iotdrv_lywsd03mmc_small_number(struct iotdrv_lywsd03mmc_frame *f, int16_t value, bool percent)
{
	uint8_t battery = f->b[1] & SYM_BAT;

	f->b[0] = percent ? SYM_PERCENT : 0U;
	f->b[1] = battery;

	if (value > 99) {
		f->b[0] |= SYM_I;
		f->b[1] |= SYM_H;
	} else if (value < -9) {
		f->b[0] |= SYM_O;
		f->b[1] |= SYM_L;
	} else {
		if (value < 0) {
			value = -value;
			f->b[1] |= SYM_MINUS;
		}
		if (value > 9) {
			f->b[1] |= digits[(value / 10) % 10];
		}
		f->b[0] |= digits[value % 10];
	}
}

void iotdrv_lywsd03mmc_unit(struct iotdrv_lywsd03mmc_frame *f, enum iotdrv_lywsd03mmc_unit unit)
{
	f->b[2] = (f->b[2] & ~0xE0U) | ((uint8_t)unit & 0xE0U);
}

void iotdrv_lywsd03mmc_smiley(struct iotdrv_lywsd03mmc_frame *f, enum iotdrv_lywsd03mmc_smiley smiley)
{
	f->b[2] = (f->b[2] & ~0x07U) | ((uint8_t)smiley & 0x07U);
}

void iotdrv_lywsd03mmc_ble(struct iotdrv_lywsd03mmc_frame *f, bool on)
{
	f->b[2] = on ? (f->b[2] | SYM_BLE) : (f->b[2] & ~SYM_BLE);
}

void iotdrv_lywsd03mmc_battery(struct iotdrv_lywsd03mmc_frame *f, bool on)
{
	f->b[1] = on ? (f->b[1] | SYM_BAT) : (f->b[1] & ~SYM_BAT);
}

void iotdrv_lywsd03mmc_clock(struct iotdrv_lywsd03mmc_frame *f, uint8_t hours, uint8_t minutes)
{
	f->b[0] = digits[minutes % 10];
	f->b[1] = digits[(minutes / 10) % 10];
	f->b[2] = 0U;
	f->b[3] = digits[hours % 10];
	f->b[4] = digits[(hours / 10) % 10];
	f->b[5] = 0U;
}
