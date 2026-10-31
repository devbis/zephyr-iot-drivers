/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Segment LCD controller found on LYWSD03MMC hardware revisions B1.4, B1.7
 * and B2.0 (I2C 0x3C). The chip is not identified; the command sequences are
 * taken from the vendor firmware.
 */

#ifndef IOTDRV_LCD_B14_H_
#define IOTDRV_LCD_B14_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#define IOTDRV_LCD_B14_I2C_ADDR 0x3CU

int iotdrv_lcd_b14_init(const struct device *i2c, uint8_t addr);

/* Write the segment bytes, @p len of them, from the start of the display RAM. */
int iotdrv_lcd_b14_write(const struct device *i2c, uint8_t addr, const uint8_t *data, size_t len);

#endif /* IOTDRV_LCD_B14_H_ */
