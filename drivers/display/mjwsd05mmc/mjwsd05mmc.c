/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT iotdrv_mjwsd05mmc_lcd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/climate_display.h>

#include "layout.h"

LOG_MODULE_REGISTER(mjwsd05mmc, CONFIG_LOG_DEFAULT_LEVEL);

/* ROHM BU9792AFUV */
#define LCD_I2C_ADDR 0x3EU

#define BU9792_CMD_ICSET       0xE8U
#define BU9792_CMD_ICSET_RESET 0xEAU
#define BU9792_CMD_ADSET       0x00U
#define BU9792_CMD_DISPLAY_ON  0xC8U

struct lcd_config {
	const struct device *i2c;
};

struct lcd_data {
	struct iotdrv_mjwsd05mmc_frame frame;
	struct iotdrv_mjwsd05mmc_frame shown;
	bool shown_valid;
};

/* Reset, blink off, display off at 1/3 bias, display control, all pixels normal */
static const uint8_t init_cmds[] = {0xEA, 0xF0, 0xC0, 0xA4, 0xAC, 0xBC, 0xFC};

static int transfer(const struct device *dev)
{
	const struct lcd_config *cfg = dev->config;
	struct lcd_data *d = dev->data;
	uint8_t buf[2 + IOTDRV_MJWSD05MMC_FRAME_SIZE] = {BU9792_CMD_ICSET, BU9792_CMD_ADSET};
	uint8_t on = BU9792_CMD_DISPLAY_ON;
	int err;

	memcpy(&buf[2], d->frame.b, sizeof(d->frame.b));
	err = i2c_write(cfg->i2c, buf, sizeof(buf), LCD_I2C_ADDR);
	if (err != 0) {
		return err;
	}

	return i2c_write(cfg->i2c, &on, 1U, LCD_I2C_ADDR);
}

static int lcd_flush(const struct device *dev)
{
	struct lcd_data *d = dev->data;
	int err;

	if (d->shown_valid && (memcmp(&d->frame, &d->shown, sizeof(d->frame)) == 0)) {
		return 0;
	}

	err = transfer(dev);
	if (err == 0) {
		d->shown = d->frame;
		d->shown_valid = true;
	}

	return err;
}

static int lcd_off(const struct device *dev)
{
	const struct lcd_config *cfg = dev->config;
	struct lcd_data *d = dev->data;
	uint8_t reset = BU9792_CMD_ICSET_RESET;
	int err;

	memset(&d->frame, 0, sizeof(d->frame));
	d->shown_valid = false;
	err = transfer(dev);
	if (err == 0) {
		err = i2c_write(cfg->i2c, &reset, 1U, LCD_I2C_ADDR);
	}

	return err;
}

static void lcd_clear(const struct device *dev)
{
	struct lcd_data *d = dev->data;

	memset(&d->frame, 0, sizeof(d->frame));
}

static void lcd_big_number_x10(const struct device *dev, int16_t value)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_big_number_x10(&d->frame, value);
}

static void lcd_small_number(const struct device *dev, int16_t value, bool percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_small_number(&d->frame, value, percent);
}

static void lcd_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_unit(&d->frame, (unit == IOTDRV_CLIMATE_UNIT_C) ? IOTDRV_MJWSD05MMC_UNIT_C
					     : (unit == IOTDRV_CLIMATE_UNIT_F) ? IOTDRV_MJWSD05MMC_UNIT_F
									   : IOTDRV_MJWSD05MMC_UNIT_NONE);
}

static void lcd_smiley(const struct device *dev, enum iotdrv_climate_smiley smiley)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_smiley(&d->frame, (smiley == IOTDRV_CLIMATE_SMILEY_HAPPY)
						       ? IOTDRV_MJWSD05MMC_SMILEY_HAPPY
					       : (smiley == IOTDRV_CLIMATE_SMILEY_SAD)
						       ? IOTDRV_MJWSD05MMC_SMILEY_SAD
						       : IOTDRV_MJWSD05MMC_SMILEY_OFF);
}

static void lcd_ble(const struct device *dev, bool on)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_ble(&d->frame, on);
}

static void lcd_battery(const struct device *dev, int8_t percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_battery(&d->frame, percent >= 0);
}

static void lcd_clock(const struct device *dev, uint8_t hours, uint8_t minutes)
{
	struct lcd_data *d = dev->data;

	iotdrv_mjwsd05mmc_layout_clock(&d->frame, hours, minutes);
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
	if (i2c_write(cfg->i2c, &none, 0U, LCD_I2C_ADDR) != 0) {
		LOG_ERR("no panel found");
		return -ENODEV;
	}

	err = i2c_write(cfg->i2c, init_cmds, sizeof(init_cmds), LCD_I2C_ADDR);
	if (err != 0) {
		LOG_ERR("panel init failed: %d", err);
		return err;
	}

	/* The controller needs a moment before it accepts the first frame */
	k_busy_wait(200);

	return lcd_flush(dev);
}

#define LCD_INIT(inst)                                                                             \
	static struct lcd_data lcd_data_##inst;                                                    \
	static const struct lcd_config lcd_config_##inst = {                                       \
		.i2c = DEVICE_DT_GET(DT_INST_PHANDLE(inst, i2c)),                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, lcd_init, NULL, &lcd_data_##inst, &lcd_config_##inst,          \
			      POST_KERNEL, CONFIG_IOTDRV_DISPLAY_MJWSD05MMC_INIT_PRIORITY, &lcd_api);

DT_INST_FOREACH_STATUS_OKAY(LCD_INIT)
