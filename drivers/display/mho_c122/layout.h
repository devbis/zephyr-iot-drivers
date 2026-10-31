/* SPDX-License-Identifier: Apache-2.0 */

/*
 * MHO-C122 segment layout: edits a frame of six segment bytes and touches
 * no hardware.
 *
 *   big number: bytes 0, 1, 2 (hundreds, tens, units), 2.3 is the point,
 *   0.3 the leading "1", 0.1 the minus
 *   small number: bytes 5, 4 (tens, units), 3.4 is "%", 5.7 the battery
 *   byte 3: bits 0..2 temperature unit, bits 5, 6 smiley mouth
 *   1.3 is the smiley contour, 4.7 is BLE
 */

#ifndef IOTDRV_MHO_C122_LAYOUT_H_
#define IOTDRV_MHO_C122_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#define IOTDRV_MHO_C122_FRAME_SIZE 6U

struct iotdrv_mho_c122_frame {
	uint8_t b[IOTDRV_MHO_C122_FRAME_SIZE];
};

enum iotdrv_mho_c122_unit {
	IOTDRV_MHO_C122_UNIT_NONE = 0x00,
	IOTDRV_MHO_C122_UNIT_F = 0x60,
	IOTDRV_MHO_C122_UNIT_C = 0xA0,
};

enum iotdrv_mho_c122_smiley {
	IOTDRV_MHO_C122_SMILEY_OFF = 0,
	IOTDRV_MHO_C122_SMILEY_HAPPY = 5,
	IOTDRV_MHO_C122_SMILEY_SAD = 6,
};

/* Temperature in 0.1 degrees, -995..19995. Out of range shows Lo or Hi. */
void iotdrv_mho_c122_layout_big_number_x10(struct iotdrv_mho_c122_frame *f, int16_t value);

/* -9..99, optional "%" sign; BLE and battery segments are kept. Out of range shows Lo or Hi. */
void iotdrv_mho_c122_layout_small_number(struct iotdrv_mho_c122_frame *f, int16_t value, bool percent);

void iotdrv_mho_c122_layout_unit(struct iotdrv_mho_c122_frame *f, enum iotdrv_mho_c122_unit unit);
void iotdrv_mho_c122_layout_smiley(struct iotdrv_mho_c122_frame *f, enum iotdrv_mho_c122_smiley smiley);
void iotdrv_mho_c122_layout_ble(struct iotdrv_mho_c122_frame *f, bool on);
void iotdrv_mho_c122_layout_battery(struct iotdrv_mho_c122_frame *f, bool on);

/* Time as HH:MM; clears the unit, "%", point and smiley segments. */
void iotdrv_mho_c122_layout_clock(struct iotdrv_mho_c122_frame *f, uint8_t hours, uint8_t minutes);

#endif /* IOTDRV_MHO_C122_LAYOUT_H_ */
