/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT iotdrv_cgdk2_lcd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/climate_display.h>

#include "layout.h"

LOG_MODULE_REGISTER(cgdk2, CONFIG_LOG_DEFAULT_LEVEL);

/* ROHM BU9792AFUV */
#define CGDK2_I2C_ADDR 0x3EU

#define BU9792_CMD_ICSET_RESET 0xEAU
#define BU9792_CMD_ICSET       0xE8U
#define BU9792_CMD_ADSET       0x00U
#define BU9792_CMD_MODE_ON     0xC8U

struct lcd_config {
	const struct device *i2c;
};

struct lcd_data {
	struct iotdrv_cgdk2_frame frame;
	struct iotdrv_cgdk2_frame shown;
	bool shown_valid;
	bool awake;
};

/* Software reset, blink off, display mode, power save mode 3 */
static const uint8_t init_cmds[] = {BU9792_CMD_ICSET_RESET, 0xF0, 0xC0, 0xBC};

static int controller_init(const struct device *i2c)
{
	int err = i2c_write(i2c, init_cmds, sizeof(init_cmds), CGDK2_I2C_ADDR);

	/* The controller needs a moment after the reset before it takes data */
	k_busy_wait(200);

	return err;
}

static int send_frame(const struct device *i2c, const struct iotdrv_cgdk2_frame *f)
{
	uint8_t buf[2 + IOTDRV_CGDK2_FRAME_SIZE] = {BU9792_CMD_ICSET, BU9792_CMD_ADSET};
	uint8_t on = BU9792_CMD_MODE_ON;
	int err;

	memcpy(&buf[2], f->b, IOTDRV_CGDK2_FRAME_SIZE);
	err = i2c_write(i2c, buf, sizeof(buf), CGDK2_I2C_ADDR);
	if (err != 0) {
		return err;
	}

	return i2c_write(i2c, &on, 1U, CGDK2_I2C_ADDR);
}

static int lcd_flush(const struct device *dev)
{
	const struct lcd_config *cfg = dev->config;
	struct lcd_data *d = dev->data;
	int err;

	if (d->awake && d->shown_valid && (memcmp(&d->frame, &d->shown, sizeof(d->frame)) == 0)) {
		return 0;
	}

	if (!d->awake) {
		err = controller_init(cfg->i2c);
		if (err != 0) {
			return err;
		}
		d->awake = true;
	}

	err = send_frame(cfg->i2c, &d->frame);
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
	d->awake = false;
	err = i2c_write(cfg->i2c, &reset, 1U, CGDK2_I2C_ADDR);

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

	iotdrv_cgdk2_layout_big_number_x10(&d->frame, value);
}

static void lcd_small_number(const struct device *dev, int16_t value, bool percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_cgdk2_layout_small_number(&d->frame, value, percent);
}

static void lcd_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	struct lcd_data *d = dev->data;

	iotdrv_cgdk2_layout_unit(&d->frame, (unit == IOTDRV_CLIMATE_UNIT_C)   ? IOTDRV_CGDK2_UNIT_C
					: (unit == IOTDRV_CLIMATE_UNIT_F) ? IOTDRV_CGDK2_UNIT_F
								      : IOTDRV_CGDK2_UNIT_NONE);
}

static void lcd_smiley(const struct device *dev, enum iotdrv_climate_smiley smiley)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(smiley);
}

static void lcd_ble(const struct device *dev, bool on)
{
	struct lcd_data *d = dev->data;

	iotdrv_cgdk2_layout_ble(&d->frame, on);
}

static void lcd_battery(const struct device *dev, int8_t percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_cgdk2_layout_battery(&d->frame, percent);
}

static void lcd_clock(const struct device *dev, uint8_t hours, uint8_t minutes)
{
	struct lcd_data *d = dev->data;

	iotdrv_cgdk2_layout_clock(&d->frame, hours, minutes);
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
	struct lcd_data *d = dev->data;
	uint8_t none = 0U;
	int err;

	if (!device_is_ready(cfg->i2c)) {
		return -ENODEV;
	}

	/* An empty write only addresses the device, so nothing changes on it */
	if (i2c_write(cfg->i2c, &none, 0U, CGDK2_I2C_ADDR) != 0) {
		LOG_ERR("no panel at 0x%02x", CGDK2_I2C_ADDR);
		return -ENODEV;
	}

	err = controller_init(cfg->i2c);
	if (err != 0) {
		LOG_ERR("panel init failed: %d", err);
		return err;
	}
	d->awake = true;

	return 0;
}

#define LCD_INIT(inst)                                                                             \
	static struct lcd_data lcd_data_##inst;                                                    \
	static const struct lcd_config lcd_config_##inst = {                                       \
		.i2c = DEVICE_DT_GET(DT_INST_PHANDLE(inst, i2c)),                                  \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, lcd_init, NULL, &lcd_data_##inst, &lcd_config_##inst,          \
			      POST_KERNEL, CONFIG_IOTDRV_DISPLAY_CGDK2_INIT_PRIORITY, &lcd_api);

DT_INST_FOREACH_STATUS_OKAY(LCD_INIT)
