/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT iotdrv_mho_c122_lcd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/climate_display.h>

#include "layout.h"

LOG_MODULE_REGISTER(mho_c122, CONFIG_LOG_DEFAULT_LEVEL);

/* Unidentified controller, same as the LYWSD03MMC B1.4 */
#define B14_I2C_ADDR 0x3CU

struct lcd_config {
	const struct device *i2c;
};

struct lcd_data {
	struct iotdrv_mho_c122_frame frame;
	struct iotdrv_mho_c122_frame shown;
	bool shown_valid;
};

static const uint8_t init_cmds[] = {0x80, 0x3B, 0x80, 0x02, 0x80, 0x0F, 0x80, 0x95,
				    0x80, 0x88, 0x80, 0x88, 0x80, 0x88, 0x80, 0x88,
				    0x80, 0x19, 0x80, 0x28, 0x80, 0xE3, 0x80, 0x11};

/* Control bytes 0x80 0x40 select the data RAM, then every byte follows 0xC0 */
static int write_bytes(const struct device *i2c, const uint8_t *data, size_t len)
{
	uint8_t buf[2 + 2 * 8] = {0x80, 0x40};

	for (size_t i = 0; i < len; i++) {
		buf[2U + 2U * i] = 0xC0;
		buf[3U + 2U * i] = data[i];
	}

	return i2c_write(i2c, buf, 2U + 2U * len, B14_I2C_ADDR);
}

static int controller_init(const struct device *i2c)
{
	static const uint8_t clear[8];
	int err;

	/* The controller needs time after power-up before it accepts commands */
	k_msleep(50);

	err = i2c_write(i2c, init_cmds, sizeof(init_cmds), B14_I2C_ADDR);
	if (err != 0) {
		return err;
	}

	return write_bytes(i2c, clear, sizeof(clear));
}

static int lcd_flush(const struct device *dev)
{
	const struct lcd_config *cfg = dev->config;
	struct lcd_data *d = dev->data;
	int err;

	if (d->shown_valid && (memcmp(&d->frame, &d->shown, sizeof(d->frame)) == 0)) {
		return 0;
	}

	err = write_bytes(cfg->i2c, d->frame.b, sizeof(d->frame.b));
	if (err == 0) {
		d->shown = d->frame;
		d->shown_valid = true;
	}

	return err;
}

/* The controller has no sleep command, so the panel is blanked */
static int lcd_off(const struct device *dev)
{
	struct lcd_data *d = dev->data;

	memset(&d->frame, 0, sizeof(d->frame));
	d->shown_valid = false;

	return lcd_flush(dev);
}

static void lcd_clear(const struct device *dev)
{
	struct lcd_data *d = dev->data;

	memset(&d->frame, 0, sizeof(d->frame));
}

static void lcd_big_number_x10(const struct device *dev, int16_t value)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_big_number_x10(&d->frame, value);
}

static void lcd_small_number(const struct device *dev, int16_t value, bool percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_small_number(&d->frame, value, percent);
}

static void lcd_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_unit(&d->frame, (unit == IOTDRV_CLIMATE_UNIT_C)   ? IOTDRV_MHO_C122_UNIT_C
					   : (unit == IOTDRV_CLIMATE_UNIT_F) ? IOTDRV_MHO_C122_UNIT_F
									 : IOTDRV_MHO_C122_UNIT_NONE);
}

static void lcd_smiley(const struct device *dev, enum iotdrv_climate_smiley smiley)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_smiley(&d->frame, (smiley == IOTDRV_CLIMATE_SMILEY_HAPPY)
						     ? IOTDRV_MHO_C122_SMILEY_HAPPY
					     : (smiley == IOTDRV_CLIMATE_SMILEY_SAD)
						     ? IOTDRV_MHO_C122_SMILEY_SAD
						     : IOTDRV_MHO_C122_SMILEY_OFF);
}

static void lcd_ble(const struct device *dev, bool on)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_ble(&d->frame, on);
}

static void lcd_battery(const struct device *dev, int8_t percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_battery(&d->frame, percent >= 0);
}

static void lcd_clock(const struct device *dev, uint8_t hours, uint8_t minutes)
{
	struct lcd_data *d = dev->data;

	iotdrv_mho_c122_layout_clock(&d->frame, hours, minutes);
}

static const struct iotdrv_climate_display_api lcd_api = {
	.big_number_x10 = lcd_big_number_x10,
	.small_number = lcd_small_number,
	.unit = lcd_unit,
	.smiley = lcd_smiley,
	.ble = lcd_ble,
	.battery = lcd_battery,
	.clock = lcd_clock,
	.clear = lcd_clear,
	.flush = lcd_flush,
	.off = lcd_off,
};

static int lcd_init(const struct device *dev)
{
	const struct lcd_config *cfg = dev->config;
	uint8_t none = 0U;
	int err;

	if (!device_is_ready(cfg->i2c)) {
		return -ENODEV;
	}

	/* An empty write only addresses the device, so nothing changes on it */
	if (i2c_write(cfg->i2c, &none, 0U, B14_I2C_ADDR) != 0) {
		LOG_ERR("no panel at 0x%02x", B14_I2C_ADDR);
		return -ENODEV;
	}

	err = controller_init(cfg->i2c);
	if (err != 0) {
		LOG_ERR("panel init failed: %d", err);
	}

	return err;
}

#define LCD_INIT(inst)                                                                             \
	static struct lcd_data lcd_data_##inst;                                                    \
	static const struct lcd_config lcd_config_##inst = {                                       \
		.i2c = DEVICE_DT_GET(DT_INST_PHANDLE(inst, i2c)),                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, lcd_init, NULL, &lcd_data_##inst, &lcd_config_##inst,          \
			      POST_KERNEL, CONFIG_IOTDRV_DISPLAY_MHO_C122_INIT_PRIORITY, &lcd_api);

DT_INST_FOREACH_STATUS_OKAY(LCD_INIT)
