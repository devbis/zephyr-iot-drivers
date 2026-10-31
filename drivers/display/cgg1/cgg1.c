/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT iotdrv_cgg1_epd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/climate_display.h>

#include "cgg1_layout.h"

LOG_MODULE_REGISTER(cgg1, CONFIG_LOG_DEFAULT_LEVEL);

/* Controller commands of the original CGG1-M */
#define CMD_PANEL_SETTING             0x00U
#define CMD_POWER_SETTING             0x01U
#define CMD_POWER_OFF                 0x02U
#define CMD_POWER_OFF_SEQUENCE        0x03U
#define CMD_POWER_ON                  0x04U
#define CMD_DISPLAY_REFRESH           0x12U
#define CMD_PARTIAL_DISPLAY_REFRESH   0x15U
#define CMD_DATA_START_TRANSMISSION_1 0x18U
#define CMD_LUT_FOR_VCOM              0x20U
#define CMD_LUT_0x23                  0x23U
#define CMD_LUT_0x24                  0x24U
#define CMD_LUT_0x25                  0x25U
#define CMD_LUT_0x26                  0x26U
#define CMD_PLL_CONTROL               0x30U

#define PULSE_US       2U
#define RESET_PULSE_US 110U
/* The busy line takes a moment to drop after a command or a reset */
#define BUSY_SETTLE_MS 2

/* A full refresh (reset and re-initialization) every this many updates */
static const uint8_t refresh_limit[] = {
	[IOTDRV_CGG1_VARIANT_CGG1] = 32,
	[IOTDRV_CGG1_VARIANT_CGG1_2022] = 255,
};

/*
 * LUTV, LUT_KK and LUT_KW values taken from the actual device with a
 * logic analyzer
 */
static const uint8_t lutv_init[15] = {
	0x47, 0x47, 0x01, 0x87, 0x87, 0x01, 0x47, 0x47, 0x01, 0x87, 0x87, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kk_init[15] = {
	0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kw_init[15] = {
	0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kk_update[15] = {
	0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kw_update[15] = {
	0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x81, 0x81, 0x01};

/* 2022 board: these bytes are all sent as commands */
static const uint8_t lut_ping[5] = {0x7B, 0x81, 0xE4, 0xE7, 0x08};
static const uint8_t lut_init_2022[14] = {
	0x82, 0x68, 0x50, 0xE8, 0xD0, 0xA8, 0x65, 0x7B, 0x81, 0xE4, 0xE7, 0x08, 0xAC, 0x2B};
static const uint8_t lut_work[9] = {0x82, 0x80, 0x00, 0xC0, 0x80, 0x80, 0x62, 0xAC, 0x2B};

struct cgg1_config {
	struct gpio_dt_spec cs;
	struct gpio_dt_spec clk;
	struct gpio_dt_spec mosi;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec busy;
	struct gpio_dt_spec enable;
	enum iotdrv_cgg1_variant variant;
};

struct cgg1_data {
	struct iotdrv_cgg1_frame frame;
	struct iotdrv_cgg1_frame shown;
	uint8_t refresh_cnt;
	bool shown_valid;
	/* The controller must be reset and initialized before the next refresh */
	bool needs_init;
	/* The controller holds its initialization (ATC epd_updated) */
	bool updated;
};

/* Serial link; the first error sticks and turns the following transfers into no-ops */
struct link {
	const struct cgg1_config *cfg;
	int err;
};

static void pin(struct link *l, const struct gpio_dt_spec *spec, int value)
{
	if (l->err == 0) {
		l->err = gpio_pin_set_dt(spec, value);
	}
}

static void transmit(struct link *l, bool data, uint8_t byte)
{
	const struct cgg1_config *cfg = l->cfg;

	pin(l, &cfg->clk, 0);
	pin(l, &cfg->cs, 1);
	k_busy_wait(PULSE_US);

	/* The first bit tells a command from data */
	pin(l, &cfg->mosi, data ? 1 : 0);
	k_busy_wait(PULSE_US);
	pin(l, &cfg->clk, 1);
	k_busy_wait(PULSE_US);

	for (uint8_t i = 0; i < 8U; i++) {
		pin(l, &cfg->clk, 0);
		pin(l, &cfg->mosi, ((byte & 0x80U) != 0U) ? 1 : 0);
		byte = (uint8_t)(byte << 1);
		k_busy_wait(PULSE_US);
		pin(l, &cfg->clk, 1);
		k_busy_wait(PULSE_US);
	}

	pin(l, &cfg->clk, 0);
	k_busy_wait(PULSE_US);
	pin(l, &cfg->cs, 0);
	k_busy_wait(PULSE_US);
}

static void command(struct link *l, uint8_t cmd)
{
	transmit(l, false, cmd);
}

static void param(struct link *l, uint8_t value)
{
	transmit(l, true, value);
}

static void block(struct link *l, bool data, const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		transmit(l, data, buf[i]);
	}
}

static void lut(struct link *l, uint8_t cmd, const uint8_t *buf, size_t len)
{
	command(l, cmd);
	block(l, true, buf, len);
}

static int wait_ready(const struct cgg1_config *cfg)
{
	int64_t end = k_uptime_get() + CONFIG_IOTDRV_DISPLAY_CGG1_BUSY_TIMEOUT_MS;

	k_msleep(BUSY_SETTLE_MS);
	while (true) {
		int busy = gpio_pin_get_dt(&cfg->busy);

		if (busy < 0) {
			return busy;
		}
		if (busy == 0) {
			return 0;
		}
		if (k_uptime_get() >= end) {
			return -ETIMEDOUT;
		}
		k_msleep(1);
	}
}

static int pulse_reset(const struct cgg1_config *cfg)
{
	int err = gpio_pin_set_dt(&cfg->reset, 1);

	if (err != 0) {
		return err;
	}
	k_busy_wait(RESET_PULSE_US);

	return gpio_pin_set_dt(&cfg->reset, 0);
}

static int refresh_cgg1(struct cgg1_data *d, struct link *l, bool init)
{
	const struct cgg1_config *cfg = l->cfg;
	bool lut_init = init;
	bool again;
	int err;

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	command(l, CMD_POWER_ON);

	do {
		again = false;
		err = wait_ready(cfg);
		if (err != 0) {
			return err;
		}

		if (!d->updated) {
			command(l, CMD_PANEL_SETTING);
			param(l, 0x0F);
			command(l, CMD_POWER_SETTING);
			param(l, 0x32);
			param(l, 0x32);
			command(l, CMD_POWER_OFF_SEQUENCE);
			param(l, 0x00);
			command(l, CMD_PLL_CONTROL);
			if (lut_init) {
				param(l, 0x03);
			} else {
				param(l, 0x07);
				command(l, CMD_PARTIAL_DISPLAY_REFRESH);
				param(l, 0x00);
				param(l, 0x87);
				param(l, 0x01);
				command(l, CMD_POWER_OFF_SEQUENCE);
				param(l, 0x06);
			}

			lut(l, CMD_LUT_FOR_VCOM, lutv_init, sizeof(lutv_init));
			if (lut_init) {
				lut_init = false;
				lut(l, CMD_LUT_0x23, lut_kk_init, sizeof(lut_kk_init));
				lut(l, CMD_LUT_0x26, lut_kw_init, sizeof(lut_kw_init));
				/* The initialization pass is followed by an update pass */
				again = true;
			} else {
				lut(l, CMD_LUT_0x23, lutv_init, sizeof(lutv_init));
				lut(l, CMD_LUT_0x24, lut_kk_update, sizeof(lut_kk_update));
				lut(l, CMD_LUT_0x25, lut_kw_update, sizeof(lut_kw_update));
				lut(l, CMD_LUT_0x26, lutv_init, sizeof(lutv_init));
			}
		}

		command(l, CMD_DATA_START_TRANSMISSION_1);
		for (size_t i = 0; i < d->frame.size; i++) {
			param(l, (uint8_t)(d->frame.b[i] ^ 0xFFU));
		}
		command(l, CMD_DISPLAY_REFRESH);
	} while (again && (l->err == 0));

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	command(l, CMD_POWER_OFF);
	param(l, 0x03);
	d->updated = true;

	return l->err;
}

static int refresh_2022(struct cgg1_data *d, struct link *l)
{
	const struct cgg1_config *cfg = l->cfg;
	int err;

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	block(l, false, lut_ping, sizeof(lut_ping));

	while (l->err == 0) {
		err = wait_ready(cfg);
		if (err != 0) {
			return err;
		}
		if (!d->updated) {
			block(l, false, lut_init_2022, sizeof(lut_init_2022));
		} else {
			block(l, false, lut_work, sizeof(lut_work));
		}

		err = wait_ready(cfg);
		if (err != 0) {
			return err;
		}
		command(l, 0x40);
		command(l, 0xA9);
		command(l, 0xA8);
		block(l, true, d->frame.b, d->frame.size);
		command(l, 0xAB);
		command(l, 0xAA);
		command(l, 0xAF);

		if (d->updated) {
			break;
		}
		/* The initialization pass is followed by an update pass */
		d->updated = true;
	}

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	command(l, 0xAE);
	command(l, 0x28);
	command(l, 0xAD);

	return l->err;
}

static int cgg1_flush(const struct device *dev)
{
	const struct cgg1_config *cfg = dev->config;
	struct cgg1_data *d = dev->data;
	struct link l = {.cfg = cfg};
	bool init = d->needs_init;
	int err;

	if (d->shown_valid && (memcmp(&d->frame, &d->shown, sizeof(d->frame)) == 0)) {
		return 0;
	}

	if (!init) {
		if (d->refresh_cnt > 0U) {
			d->refresh_cnt--;
		} else {
			init = true;
		}
	}

	if (init) {
		err = pulse_reset(cfg);
		if (err != 0) {
			return err;
		}
		d->refresh_cnt = refresh_limit[cfg->variant];
		d->updated = false;
	}

	if (cfg->variant == IOTDRV_CGG1_VARIANT_CGG1_2022) {
		err = refresh_2022(d, &l);
	} else {
		err = refresh_cgg1(d, &l, init);
	}
	if (err == 0) {
		/* Leave the panel idle */
		err = wait_ready(cfg);
	}

	if (err != 0) {
		d->needs_init = true;
		d->shown_valid = false;
		LOG_ERR("refresh failed: %d", err);

		return err;
	}

	d->needs_init = false;
	d->shown = d->frame;
	d->shown_valid = true;

	return 0;
}

static int cgg1_off(const struct device *dev)
{
	const struct cgg1_config *cfg = dev->config;
	struct cgg1_data *d = dev->data;
	int err;

	iotdrv_cgg1_layout_clear(&d->frame);
	d->shown_valid = false;
	d->needs_init = true;
	err = cgg1_flush(dev);
	if (err != 0) {
		return err;
	}

	/* The refresh ends with the controller's power-off command; keep it in reset */
	d->needs_init = true;

	return gpio_pin_set_dt(&cfg->reset, 1);
}

static void cgg1_clear(const struct device *dev)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_clear(&d->frame);
}

static void cgg1_big_number_x10(const struct device *dev, int16_t value)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_big_number_x10(&d->frame, value);
}

static void cgg1_small_number(const struct device *dev, int16_t value, bool percent)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_small_number(&d->frame, value, percent);
}

static void cgg1_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_unit(&d->frame, (unit == IOTDRV_CLIMATE_UNIT_C)   ? IOTDRV_CGG1_UNIT_C
				       : (unit == IOTDRV_CLIMATE_UNIT_F) ? IOTDRV_CGG1_UNIT_F
								     : IOTDRV_CGG1_UNIT_NONE);
}

/* The panel has no smiley: the "---" symbol stands for a happy one */
static void cgg1_smiley(const struct device *dev, enum iotdrv_climate_smiley smiley)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_smiley(&d->frame, smiley == IOTDRV_CLIMATE_SMILEY_HAPPY);
}

static void cgg1_ble(const struct device *dev, bool on)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_ble(&d->frame, on);
}

static void cgg1_battery(const struct device *dev, int8_t percent)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_battery(&d->frame, percent);
}

static void cgg1_clock(const struct device *dev, uint8_t hours, uint8_t minutes)
{
	struct cgg1_data *d = dev->data;

	iotdrv_cgg1_layout_clock(&d->frame, hours, minutes);
}

static const struct iotdrv_climate_display_api cgg1_api = {
	.big_number_x10 = cgg1_big_number_x10,
	.small_number = cgg1_small_number,
	.unit = cgg1_unit,
	.smiley = cgg1_smiley,
	.ble = cgg1_ble,
	.battery = cgg1_battery,
	.clock = cgg1_clock,
	.clear = cgg1_clear,
	.flush = cgg1_flush,
	.off = cgg1_off,
};

static int cgg1_init(const struct device *dev)
{
	const struct cgg1_config *cfg = dev->config;
	struct cgg1_data *d = dev->data;
	const struct gpio_dt_spec *outputs[] = {&cfg->cs, &cfg->clk, &cfg->mosi, &cfg->reset};
	int err;

	if (!gpio_is_ready_dt(&cfg->busy)) {
		return -ENODEV;
	}

	/* Idle: chip select and reset released, clock low, data high */
	for (size_t i = 0; i < ARRAY_SIZE(outputs); i++) {
		if (!gpio_is_ready_dt(outputs[i])) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(outputs[i], (outputs[i] == &cfg->mosi)
							       ? GPIO_OUTPUT_ACTIVE
							       : GPIO_OUTPUT_INACTIVE);
		if (err != 0) {
			return err;
		}
	}

	err = gpio_pin_configure_dt(&cfg->busy, GPIO_INPUT);
	if (err != 0) {
		return err;
	}

	if (cfg->enable.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->enable)) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(&cfg->enable, GPIO_OUTPUT_ACTIVE);
		if (err != 0) {
			return err;
		}
	}

	iotdrv_cgg1_layout_init(&d->frame, cfg->variant);
	d->shown = d->frame;
	d->needs_init = true;

	return 0;
}

#define CGG1_INIT(inst)                                                                            \
	static struct cgg1_data cgg1_data_##inst;                                                  \
	static const struct cgg1_config cgg1_config_##inst = {                                     \
		.cs = GPIO_DT_SPEC_INST_GET(inst, cs_gpios),                                       \
		.clk = GPIO_DT_SPEC_INST_GET(inst, clk_gpios),                                     \
		.mosi = GPIO_DT_SPEC_INST_GET(inst, mosi_gpios),                                   \
		.reset = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                                 \
		.busy = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                                   \
		.enable = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}),                       \
		.variant = DT_INST_ENUM_IDX(inst, variant),                                        \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, cgg1_init, NULL, &cgg1_data_##inst, &cgg1_config_##inst,       \
			      POST_KERNEL, CONFIG_IOTDRV_DISPLAY_CGG1_INIT_PRIORITY, &cgg1_api);

DT_INST_FOREACH_STATUS_OKAY(CGG1_INIT)
