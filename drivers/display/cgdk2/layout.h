/* SPDX-License-Identifier: Apache-2.0 */

/*
 * CGDK2 segment layout: edits a frame of the 18 BU9792AFUV RAM bytes and
 * touches no hardware.
 *
 *   top row (big number): byte 1.0 is "1", the digit groups are in bytes
 *   0, 2, 3, 4, 5 and 17, 5.4 is the point, 17.4..17.6 the unit
 *   bottom row (small number): bytes 5..10 and 17.7, 8.1 is "%", 8.0 the point
 *   byte 1: bit 4 BLE, bits 1, 2, 3, 7, 6 the battery gauge, bit 5 its frame
 */

#ifndef IOTDRV_CGDK2_LAYOUT_H_
#define IOTDRV_CGDK2_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#define IOTDRV_CGDK2_FRAME_SIZE 18U

struct iotdrv_cgdk2_frame {
	uint8_t b[IOTDRV_CGDK2_FRAME_SIZE];
};

enum iotdrv_cgdk2_unit {
	IOTDRV_CGDK2_UNIT_NONE = 0x00,
	IOTDRV_CGDK2_UNIT_F = 0x60,
	IOTDRV_CGDK2_UNIT_C = 0xA0,
};

/* Temperature in 0.1 degrees, -995..19995. Out of range shows Lo or Hi. */
void iotdrv_cgdk2_layout_big_number_x10(struct iotdrv_cgdk2_frame *f, int16_t value);

/* Value in 0.1 units, -995..9995, shown as -99..999 or -9.9..99.9. Out of range shows Lo or Hi. */
void iotdrv_cgdk2_layout_small_number_x10(struct iotdrv_cgdk2_frame *f, int16_t value, bool percent);

/* Whole value, -9..99 and beyond like the x10 variant. */
void iotdrv_cgdk2_layout_small_number(struct iotdrv_cgdk2_frame *f, int16_t value, bool percent);

void iotdrv_cgdk2_layout_unit(struct iotdrv_cgdk2_frame *f, enum iotdrv_cgdk2_unit unit);
void iotdrv_cgdk2_layout_ble(struct iotdrv_cgdk2_frame *f, bool on);

/* Gauge of five steps at 16, 33, 49, 67 and 83 percent; hidden when negative. */
void iotdrv_cgdk2_layout_battery(struct iotdrv_cgdk2_frame *f, int8_t percent);

/* Time as HH:MM; clears the whole frame first. */
void iotdrv_cgdk2_layout_clock(struct iotdrv_cgdk2_frame *f, uint8_t hours, uint8_t minutes);

#endif /* IOTDRV_CGDK2_LAYOUT_H_ */
