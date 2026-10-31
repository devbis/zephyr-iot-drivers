/* SPDX-License-Identifier: Apache-2.0 */

/* ROHM BU9792AFUV segment LCD controller. Knows the chip, not the panel wired to it. */

#ifndef IOTDRV_LCD_BU9792_H_
#define IOTDRV_LCD_BU9792_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#define IOTDRV_BU9792_I2C_ADDR 0x3EU
#define IOTDRV_BU9792_RAM_SIZE 18U

/* Reset, configure for 1/3 bias and clear the display RAM; the display is on afterwards. */
int iotdrv_bu9792_init(const struct device *i2c, uint8_t addr);

/* Write @p len bytes of display RAM starting at @p ram_addr. */
int iotdrv_bu9792_write_ram(const struct device *i2c, uint8_t addr, uint8_t ram_addr,
			const uint8_t *data, size_t len);

/* Reset the chip, which blanks the display. */
int iotdrv_bu9792_reset(const struct device *i2c, uint8_t addr);

/* Blink: 0 off, 1 slow, 2 medium, 3 fast. */
int iotdrv_bu9792_blink(const struct device *i2c, uint8_t addr, uint8_t rate);

#endif /* IOTDRV_LCD_BU9792_H_ */
