/* SPDX-License-Identifier: Apache-2.0 */

/*
 * MJWSD05MMC segment layout: edits a frame of 18 segment bytes (the RAM of
 * the BU9792AFUV) and touches no hardware.
 *
 *   big number: bytes 1..3 and 4.4 ("1"), 13.0 is the point
 *   small number: bytes 12..15, 14.0 is the point, 15.3 is "%"
 *   clock: bytes 7..12, 10.0 is the colon
 *   byte 0: bit 0 BLE, bits 4..6 temperature unit
 *   byte 17: bit 4 battery, bits 5..7 smiley
 */

#ifndef MJWSD05MMC_LAYOUT_H_
#define MJWSD05MMC_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#define IOTDRV_MJWSD05MMC_FRAME_SIZE 18U

struct iotdrv_mjwsd05mmc_frame {
	uint8_t b[IOTDRV_MJWSD05MMC_FRAME_SIZE];
};

enum iotdrv_mjwsd05mmc_unit {
	IOTDRV_MJWSD05MMC_UNIT_NONE = 0x00,
	IOTDRV_MJWSD05MMC_UNIT_C = 0x03,
	IOTDRV_MJWSD05MMC_UNIT_F = 0x07,
};

enum iotdrv_mjwsd05mmc_smiley {
	IOTDRV_MJWSD05MMC_SMILEY_OFF = 0,
	IOTDRV_MJWSD05MMC_SMILEY_HAPPY = 5,
	IOTDRV_MJWSD05MMC_SMILEY_SAD = 6,
};

/* Temperature in 0.1 degrees; shows -99, -9.9 .. 199.9, 1999. Out of range shows Lo or Hi. */
void iotdrv_mjwsd05mmc_layout_big_number_x10(struct iotdrv_mjwsd05mmc_frame *f, int16_t value);

/* -9..99 shown as x.0 with an optional "%"; out of range shows Lo or Hi. */
void iotdrv_mjwsd05mmc_layout_small_number(struct iotdrv_mjwsd05mmc_frame *f, int16_t value,
				       bool percent);

void iotdrv_mjwsd05mmc_layout_unit(struct iotdrv_mjwsd05mmc_frame *f, enum iotdrv_mjwsd05mmc_unit unit);
void iotdrv_mjwsd05mmc_layout_smiley(struct iotdrv_mjwsd05mmc_frame *f,
				 enum iotdrv_mjwsd05mmc_smiley smiley);
void iotdrv_mjwsd05mmc_layout_ble(struct iotdrv_mjwsd05mmc_frame *f, bool on);
void iotdrv_mjwsd05mmc_layout_battery(struct iotdrv_mjwsd05mmc_frame *f, bool on);

/* Time as HH:MM in the clock group. */
void iotdrv_mjwsd05mmc_layout_clock(struct iotdrv_mjwsd05mmc_frame *f, uint8_t hours, uint8_t minutes);

#endif /* MJWSD05MMC_LAYOUT_H_ */
