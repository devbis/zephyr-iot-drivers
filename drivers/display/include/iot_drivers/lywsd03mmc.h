/* SPDX-License-Identifier: Apache-2.0 */

/*
 * LYWSD03MMC segment LCD.
 *
 * Add a "iotdrv,lywsd03mmc-lcd" node to the devicetree and the panel is set
 * up at boot, whatever its board revision. Setters edit an in-memory frame;
 * iotdrv_lywsd03mmc_flush() sends it to the panel. Not thread safe: use the
 * panel from one thread.
 */

#ifndef IOTDRV_LYWSD03MMC_H_
#define IOTDRV_LYWSD03MMC_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#include <iot_drivers/lywsd03mmc_layout.h>

/* Temperature in 0.1 degrees, -995..19995. Out of range shows oL or iH. */
void iotdrv_lywsd03mmc_big_number_x10(const struct device *dev, int16_t value);

/* -9..99 with an optional "%"; the battery segment is kept. */
void iotdrv_lywsd03mmc_small_number(const struct device *dev, int16_t value, bool percent);

void iotdrv_lywsd03mmc_unit(const struct device *dev, enum iotdrv_lywsd03mmc_unit unit);
void iotdrv_lywsd03mmc_smiley(const struct device *dev, enum iotdrv_lywsd03mmc_smiley smiley);
void iotdrv_lywsd03mmc_ble(const struct device *dev, bool on);
void iotdrv_lywsd03mmc_battery(const struct device *dev, bool on);
void iotdrv_lywsd03mmc_clock(const struct device *dev, uint8_t hours, uint8_t minutes);

/* Blank all segments in the frame. */
void iotdrv_lywsd03mmc_clear(const struct device *dev);

/* Send the frame to the panel if it changed since the last flush. */
int iotdrv_lywsd03mmc_flush(const struct device *dev);

/* Blank the panel; I2C revisions are put to sleep as well. */
int iotdrv_lywsd03mmc_off(const struct device *dev);

#endif /* IOTDRV_LYWSD03MMC_H_ */
