/* SPDX-License-Identifier: Apache-2.0 */

/*
 * LYWSD03MMC segment LCD.
 *
 * The layout functions edit a frame of six segment bytes and touch no
 * hardware; struct iotdrv_lywsd03mmc adds the transfer to the controller.
 *
 *        --5.4--     --4.4--     --3.4--      BLE 2.4
 *   big number: bytes 5, 4, 3 (hundreds, tens, units), 4.3 is the point
 *   small number: bytes 1, 0 (tens, units), 0.3 is "%", 1.3 is the battery
 *   byte 2: bits 0..2 smiley, bits 5..7 temperature unit
 */

#ifndef IOTDRV_LYWSD03MMC_H_
#define IOTDRV_LYWSD03MMC_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

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
void iotdrv_lywsd03mmc_big_number_x10(struct iotdrv_lywsd03mmc_frame *f, int16_t value);

/* -9..99, optional "%" sign; the battery segment is kept. Out of range shows oL or iH. */
void iotdrv_lywsd03mmc_small_number(struct iotdrv_lywsd03mmc_frame *f, int16_t value, bool percent);

void iotdrv_lywsd03mmc_unit(struct iotdrv_lywsd03mmc_frame *f, enum iotdrv_lywsd03mmc_unit unit);
void iotdrv_lywsd03mmc_smiley(struct iotdrv_lywsd03mmc_frame *f, enum iotdrv_lywsd03mmc_smiley smiley);
void iotdrv_lywsd03mmc_ble(struct iotdrv_lywsd03mmc_frame *f, bool on);
void iotdrv_lywsd03mmc_battery(struct iotdrv_lywsd03mmc_frame *f, bool on);

/* Display time as HH:MM in the big and small digit groups. */
void iotdrv_lywsd03mmc_clock(struct iotdrv_lywsd03mmc_frame *f, uint8_t hours, uint8_t minutes);

enum iotdrv_lywsd03mmc_rev {
	IOTDRV_LYWSD03MMC_REV_B14, /* B1.4, B1.7, B2.0: controller at 0x3C */
	IOTDRV_LYWSD03MMC_REV_B19, /* B1.9: BU9792AFUV at 0x3E */
};

struct iotdrv_lywsd03mmc {
	const struct device *i2c;
	struct iotdrv_lywsd03mmc_frame frame;
	struct iotdrv_lywsd03mmc_frame shown;
	enum iotdrv_lywsd03mmc_rev rev;
	bool shown_valid;
};

/* Find which controller answers on the bus; -ENODEV when neither does. */
int iotdrv_lywsd03mmc_detect(const struct device *i2c, enum iotdrv_lywsd03mmc_rev *rev);

/* Initialize the controller and blank the panel. */
int iotdrv_lywsd03mmc_init(struct iotdrv_lywsd03mmc *d, const struct device *i2c,
		       enum iotdrv_lywsd03mmc_rev rev);

/* Send the frame to the panel if it changed since the last flush. */
int iotdrv_lywsd03mmc_flush(struct iotdrv_lywsd03mmc *d);

/* Blank the panel and put the controller to sleep. */
int iotdrv_lywsd03mmc_off(struct iotdrv_lywsd03mmc *d);

#endif /* IOTDRV_LYWSD03MMC_H_ */
