/* SPDX-License-Identifier: Apache-2.0 */

/*
 * LYWSD03MMC segment layout: edits a frame of six segment bytes and touches
 * no hardware. Most users want <iot_drivers/lywsd03mmc.h> instead.
 *
 *   big number: bytes 5, 4, 3 (hundreds, tens, units), 4.3 is the point
 *   small number: bytes 1, 0 (tens, units), 0.3 is "%", 1.3 is the battery
 *   byte 2: bits 0..2 smiley, bit 4 BLE, bits 5..7 temperature unit
 */

#ifndef IOTDRV_LYWSD03MMC_LAYOUT_H_
#define IOTDRV_LYWSD03MMC_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#define IOTDRV_LYWSD03MMC_FRAME_SIZE 6U

struct iotdrv_lywsd03mmc_frame {
	uint8_t b[IOTDRV_LYWSD03MMC_FRAME_SIZE];
};

enum iotdrv_lywsd03mmc_unit {
	IOTDRV_LYWSD03MMC_UNIT_NONE = 0x00,
	IOTDRV_LYWSD03MMC_UNIT_F = 0x60,
	IOTDRV_LYWSD03MMC_UNIT_C = 0xA0,
};

enum iotdrv_lywsd03mmc_smiley {
	IOTDRV_LYWSD03MMC_SMILEY_OFF = 0,
	IOTDRV_LYWSD03MMC_SMILEY_HAPPY = 5,
	IOTDRV_LYWSD03MMC_SMILEY_SAD = 6,
};

/* Temperature in 0.1 degrees, -995..19995; shows -99, -9.9 .. 199.9, 1999. Out of range shows oL or iH. */
void iotdrv_lywsd03mmc_layout_big_number_x10(struct iotdrv_lywsd03mmc_frame *f, int16_t value);

/* -9..99, optional "%" sign; the battery segment is kept. Out of range shows oL or iH. */
void iotdrv_lywsd03mmc_layout_small_number(struct iotdrv_lywsd03mmc_frame *f, int16_t value, bool percent);

void iotdrv_lywsd03mmc_layout_unit(struct iotdrv_lywsd03mmc_frame *f, enum iotdrv_lywsd03mmc_unit unit);
void iotdrv_lywsd03mmc_layout_smiley(struct iotdrv_lywsd03mmc_frame *f, enum iotdrv_lywsd03mmc_smiley smiley);
void iotdrv_lywsd03mmc_layout_ble(struct iotdrv_lywsd03mmc_frame *f, bool on);
void iotdrv_lywsd03mmc_layout_battery(struct iotdrv_lywsd03mmc_frame *f, bool on);

/* Display time as HH:MM in the big and small digit groups. */
void iotdrv_lywsd03mmc_layout_clock(struct iotdrv_lywsd03mmc_frame *f, uint8_t hours, uint8_t minutes);

#endif /* IOTDRV_LYWSD03MMC_LAYOUT_H_ */
