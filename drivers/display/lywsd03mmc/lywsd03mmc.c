/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/i2c.h>

#include <iot_drivers/lcd_b14.h>
#include <iot_drivers/lcd_bu9792.h>
#include <iot_drivers/lywsd03mmc.h>

static uint8_t reverse8(uint8_t v)
{
	v = (uint8_t)(((v & 0xF0U) >> 4) | ((v & 0x0FU) << 4));
	v = (uint8_t)(((v & 0xCCU) >> 2) | ((v & 0x33U) << 2));
	v = (uint8_t)(((v & 0xAAU) >> 1) | ((v & 0x55U) << 1));

	return v;
}

static bool answers(const struct device *i2c, uint8_t addr)
{
	uint8_t none = 0U;

	/* An empty write only addresses the device, so nothing is changed on it */
	return i2c_write(i2c, &none, 0U, addr) == 0;
}

int iotdrv_lywsd03mmc_detect(const struct device *i2c, enum iotdrv_lywsd03mmc_rev *rev)
{
	if (answers(i2c, IOTDRV_LCD_B14_I2C_ADDR)) {
		*rev = IOTDRV_LYWSD03MMC_REV_B14;
		return 0;
	}

	if (answers(i2c, IOTDRV_BU9792_I2C_ADDR)) {
		*rev = IOTDRV_LYWSD03MMC_REV_B19;
		return 0;
	}

	return -ENODEV;
}

int iotdrv_lywsd03mmc_init(struct iotdrv_lywsd03mmc *d, const struct device *i2c,
		       enum iotdrv_lywsd03mmc_rev rev)
{
	memset(d, 0, sizeof(*d));
	d->i2c = i2c;
	d->rev = rev;

	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}

	if (rev == IOTDRV_LYWSD03MMC_REV_B14) {
		return iotdrv_lcd_b14_init(i2c, IOTDRV_LCD_B14_I2C_ADDR);
	}

	return iotdrv_bu9792_init(i2c, IOTDRV_BU9792_I2C_ADDR);
}

/* The panel is wired to the BU9792 in bit-reversed order and spread over RAM pairs 4-5, 8-9 and 12-13 */
static int flush_b19(struct iotdrv_lywsd03mmc *d)
{
	const uint8_t *b = d->frame.b;

	for (size_t pair = 0; pair < 3U; pair++) {
		uint8_t ram[2] = {reverse8(b[2U * pair]), reverse8(b[2U * pair + 1U])};
		int err = iotdrv_bu9792_write_ram(d->i2c, IOTDRV_BU9792_I2C_ADDR, (uint8_t)(4U + 4U * pair),
					      ram, sizeof(ram));

		if (err != 0) {
			return err;
		}
	}

	return 0;
}

int iotdrv_lywsd03mmc_flush(struct iotdrv_lywsd03mmc *d)
{
	int err;

	if (d->shown_valid && (memcmp(&d->frame, &d->shown, sizeof(d->frame)) == 0)) {
		return 0;
	}

	if (d->rev == IOTDRV_LYWSD03MMC_REV_B14) {
		err = iotdrv_lcd_b14_write(d->i2c, IOTDRV_LCD_B14_I2C_ADDR, d->frame.b, sizeof(d->frame.b));
	} else {
		err = flush_b19(d);
	}

	if (err == 0) {
		d->shown = d->frame;
		d->shown_valid = true;
	}

	return err;
}

int iotdrv_lywsd03mmc_off(struct iotdrv_lywsd03mmc *d)
{
	memset(&d->frame, 0, sizeof(d->frame));
	d->shown_valid = false;

	if (d->rev == IOTDRV_LYWSD03MMC_REV_B14) {
		return iotdrv_lcd_b14_write(d->i2c, IOTDRV_LCD_B14_I2C_ADDR, d->frame.b, sizeof(d->frame.b));
	}

	return iotdrv_bu9792_reset(d->i2c, IOTDRV_BU9792_I2C_ADDR);
}
