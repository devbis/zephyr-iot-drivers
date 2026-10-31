/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT iotdrv_mho_c401_epd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/climate_display.h>

#include "layout.h"

LOG_MODULE_REGISTER(mho_c401, CONFIG_LOG_DEFAULT_LEVEL);

/* Partial updates between two full ones */
#define REFRESH_COUNT_C401  32U
#define REFRESH_COUNT_C401N 2048U

/* Longest wait for the busy line: a refresh takes about 1.3 s */
#define BUSY_TIMEOUT_MS 5000
#define BUSY_POLL_MS    2

#define RESET_PULSE_US 110U
#define CLOCK_HALF_US  2U

/* MHO-C401 controller commands */
#define CMD_PANEL_SETTING        0x00U
#define CMD_POWER_SETTING        0x01U
#define CMD_POWER_OFF            0x02U
#define CMD_POWER_OFF_SEQUENCE   0x03U
#define CMD_POWER_ON             0x04U
#define CMD_DISPLAY_REFRESH      0x12U
#define CMD_PARTIAL_REFRESH      0x15U
#define CMD_DATA_START_1         0x18U
#define CMD_DATA_START_2         0x1CU
#define CMD_LUT_VCOM             0x20U
#define CMD_LUT_23               0x23U
#define CMD_LUT_24               0x24U
#define CMD_LUT_25               0x25U
#define CMD_LUT_26               0x26U
#define CMD_PLL_CONTROL          0x30U

#define LUT_SIZE 15U

/* LUTs of the MHO-C401, taken from the original device with a logic analyzer */
static const uint8_t lut_v_init[LUT_SIZE] = {0x47, 0x47, 0x01, 0x87, 0x87, 0x01, 0x47, 0x47,
					     0x01, 0x87, 0x87, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kk_init[LUT_SIZE] = {0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x47, 0x47,
					      0x01, 0x47, 0x47, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kw_init[LUT_SIZE] = {0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x87, 0x87,
					      0x01, 0x87, 0x87, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kk_update[LUT_SIZE] = {0x87, 0x87, 0x01, 0x87, 0x87, 0x01, 0x87, 0x87,
						0x01, 0x87, 0x87, 0x01, 0x81, 0x81, 0x01};
static const uint8_t lut_kw_update[LUT_SIZE] = {0x47, 0x47, 0x01, 0x47, 0x47, 0x01, 0x47, 0x47,
						0x01, 0x47, 0x47, 0x01, 0x81, 0x81, 0x01};

/* MHO-C401N command sequences, all sent in command mode */
static const uint8_t n_lut_ping[] = {0x7B, 0x81, 0xE4, 0xE7, 0x08};
static const uint8_t n_lut_init[] = {0x82, 0x68, 0x50, 0xE8, 0xD0, 0xA8, 0x65,
				     0x7B, 0x81, 0xE4, 0xE7, 0x08, 0xAC, 0x2B};
static const uint8_t n_lut_work[] = {0x82, 0x80, 0x00, 0xC0, 0x80, 0x80, 0x62, 0xAC, 0x2B};
static const uint8_t n_data_prefix[] = {0x40, 0xA9, 0xA8};
static const uint8_t n_refresh[] = {0xAB, 0xAA, 0xAF};
static const uint8_t n_finish[] = {0xAE, 0x28, 0xAD};

struct epd_config {
	struct gpio_dt_spec cs;
	struct gpio_dt_spec clk;
	struct gpio_dt_spec mosi;
	struct gpio_dt_spec reset;
	struct gpio_dt_spec busy;
	struct gpio_dt_spec enable;
	enum iotdrv_mho_c401_layout_variant variant;
};

struct epd_data {
	struct iotdrv_mho_c401_frame frame;
	struct iotdrv_mho_c401_frame shown;
	uint16_t refresh_left;
	bool shown_valid;
	bool awake;
};

static void pin_set(const struct gpio_dt_spec *pin, int value)
{
	(void)gpio_pin_set_dt(pin, value);
}

static int wait_ready(const struct epd_config *cfg)
{
	int64_t deadline = k_uptime_get() + BUSY_TIMEOUT_MS;

	while (true) {
		int busy = gpio_pin_get_dt(&cfg->busy);

		if (busy < 0) {
			return busy;
		}
		if (busy == 0) {
			return 0;
		}
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
		k_msleep(BUSY_POLL_MS);
	}
}

/* One 9-bit word: a command/data bit, then the byte, MSB first */
static void transmit(const struct epd_config *cfg, bool data, uint8_t byte)
{
	pin_set(&cfg->clk, 0);
	pin_set(&cfg->cs, 1);
	k_busy_wait(CLOCK_HALF_US);

	pin_set(&cfg->mosi, data ? 1 : 0);
	k_busy_wait(CLOCK_HALF_US);
	pin_set(&cfg->clk, 1);
	k_busy_wait(CLOCK_HALF_US);

	for (size_t i = 0; i < 8U; i++) {
		pin_set(&cfg->clk, 0);
		pin_set(&cfg->mosi, ((byte & 0x80U) != 0U) ? 1 : 0);
		byte = (uint8_t)(byte << 1);
		k_busy_wait(CLOCK_HALF_US);
		pin_set(&cfg->clk, 1);
		k_busy_wait(CLOCK_HALF_US);
	}

	pin_set(&cfg->clk, 0);
	k_busy_wait(CLOCK_HALF_US);
	pin_set(&cfg->cs, 0);
	k_busy_wait(CLOCK_HALF_US);
}

static void send_block(const struct epd_config *cfg, bool data, const uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		transmit(cfg, data, buf[i]);
	}
}

static void send_fill(const struct epd_config *cfg, uint8_t value, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		transmit(cfg, true, value);
	}
}

/*
 * One MHO-C401 update cycle: charge pump on, load the data, refresh, charge pump off.
 * Before the controller was initialised (no `updated`), pass 2 and 1 clear it to black
 * and white to remove ghosting, pass 0 loads the settings and the image.
 */
static int c401_cycle(const struct epd_config *cfg, const struct iotdrv_mho_c401_frame *f,
		      bool updated, uint8_t pass)
{
	size_t size = iotdrv_mho_c401_layout_size(cfg->variant);
	int err;

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	transmit(cfg, false, CMD_POWER_ON);

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}

	if (!updated) {
		transmit(cfg, false, CMD_PANEL_SETTING);
		transmit(cfg, true, 0x0B);
		transmit(cfg, false, CMD_POWER_SETTING);
		transmit(cfg, true, 0x46);
		transmit(cfg, true, 0x46);
		transmit(cfg, false, CMD_POWER_OFF_SEQUENCE);
		transmit(cfg, true, (pass != 0U) ? 0x00 : 0x06);
		transmit(cfg, false, CMD_PLL_CONTROL);
		if (pass != 0U) {
			transmit(cfg, true, 0x03);
		} else {
			transmit(cfg, true, 0x07);
			transmit(cfg, false, CMD_PARTIAL_REFRESH);
			transmit(cfg, true, 0x00);
			transmit(cfg, true, 0x87);
			transmit(cfg, true, 0x01);
		}
		transmit(cfg, false, CMD_LUT_VCOM);
		send_block(cfg, true, lut_v_init, LUT_SIZE);

		if (pass == 1U) {
			transmit(cfg, false, CMD_LUT_23);
			send_block(cfg, true, lut_kw_update, LUT_SIZE);
			transmit(cfg, false, CMD_LUT_26);
			send_block(cfg, true, lut_kk_update, LUT_SIZE);
			transmit(cfg, false, CMD_DATA_START_1);
			send_fill(cfg, 0x00, size);
			transmit(cfg, false, CMD_DATA_START_2);
			send_fill(cfg, 0x00, size);
		} else if (pass != 0U) {
			transmit(cfg, false, CMD_LUT_23);
			send_block(cfg, true, lut_kk_init, LUT_SIZE);
			transmit(cfg, false, CMD_LUT_26);
			send_block(cfg, true, lut_kw_init, LUT_SIZE);
			transmit(cfg, false, CMD_DATA_START_1);
			send_fill(cfg, 0xFF, size);
			transmit(cfg, false, CMD_DATA_START_2);
			send_fill(cfg, 0xFF, size);
		} else {
			transmit(cfg, false, CMD_LUT_23);
			send_block(cfg, true, lut_v_init, LUT_SIZE);
			transmit(cfg, false, CMD_LUT_24);
			send_block(cfg, true, lut_kk_update, LUT_SIZE);
			transmit(cfg, false, CMD_LUT_25);
			send_block(cfg, true, lut_kw_update, LUT_SIZE);
			transmit(cfg, false, CMD_LUT_26);
			send_block(cfg, true, lut_v_init, LUT_SIZE);
			transmit(cfg, false, CMD_DATA_START_1);
			send_block(cfg, true, f->b, size);
		}
	} else {
		transmit(cfg, false, CMD_DATA_START_1);
		send_block(cfg, true, f->b, size);
	}

	transmit(cfg, false, CMD_DISPLAY_REFRESH);
	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	transmit(cfg, false, CMD_POWER_OFF);
	transmit(cfg, true, 0x03);

	return wait_ready(cfg);
}

static int c401_update(const struct epd_config *cfg, const struct iotdrv_mho_c401_frame *f, bool full)
{
	int err;

	if (!full) {
		return c401_cycle(cfg, f, true, 0U);
	}

	err = c401_cycle(cfg, f, false, 2U);
	if (err == 0) {
		err = c401_cycle(cfg, f, false, 1U);
	}
	if (err == 0) {
		err = c401_cycle(cfg, f, false, 0U);
	}

	return err;
}

static int n_load(const struct epd_config *cfg, const struct iotdrv_mho_c401_frame *f,
		  const uint8_t *lut, size_t lut_len)
{
	int err = wait_ready(cfg);

	if (err != 0) {
		return err;
	}
	send_block(cfg, false, lut, lut_len);

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	send_block(cfg, false, n_data_prefix, sizeof(n_data_prefix));
	send_block(cfg, true, f->b, iotdrv_mho_c401_layout_size(cfg->variant));
	send_block(cfg, false, n_refresh, sizeof(n_refresh));

	return 0;
}

static int c401n_update(const struct epd_config *cfg, const struct iotdrv_mho_c401_frame *f,
			bool full)
{
	int err;

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	send_block(cfg, false, n_lut_ping, sizeof(n_lut_ping));

	if (full) {
		err = n_load(cfg, f, n_lut_init, sizeof(n_lut_init));
		if (err != 0) {
			return err;
		}
	}

	err = n_load(cfg, f, n_lut_work, sizeof(n_lut_work));
	if (err != 0) {
		return err;
	}

	err = wait_ready(cfg);
	if (err != 0) {
		return err;
	}
	send_block(cfg, false, n_finish, sizeof(n_finish));

	return wait_ready(cfg);
}

static void controller_reset(const struct epd_config *cfg)
{
	pin_set(&cfg->reset, 1);
	k_busy_wait(RESET_PULSE_US);
	pin_set(&cfg->reset, 0);
}

static int refresh(const struct device *dev, const struct iotdrv_mho_c401_frame *f)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;
	bool c401n = (cfg->variant == IOTDRV_MHO_C401_LAYOUT_C401N);
	bool full = !d->awake || (d->refresh_left == 0U);
	int err;

	if (full) {
		controller_reset(cfg);
		d->refresh_left = c401n ? REFRESH_COUNT_C401N : REFRESH_COUNT_C401;
		d->awake = true;
	} else {
		d->refresh_left--;
	}

	err = c401n ? c401n_update(cfg, f, full) : c401_update(cfg, f, full);
	if (err != 0) {
		d->awake = false;
	}

	return err;
}

static int epd_flush(const struct device *dev)
{
	struct epd_data *d = dev->data;
	int err;

	if (d->shown_valid && (memcmp(&d->frame, &d->shown, sizeof(d->frame)) == 0)) {
		return 0;
	}

	d->shown_valid = false;
	err = refresh(dev, &d->frame);
	if (err == 0) {
		d->shown = d->frame;
		d->shown_valid = true;
	}

	return err;
}

static int epd_off(const struct device *dev)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;
	int err;

	iotdrv_mho_c401_layout_clear(&d->frame);
	d->shown_valid = false;
	/* The panel content is unknown, so clear it with a full cycle */
	d->awake = false;
	err = refresh(dev, &d->frame);
	if (err == 0) {
		pin_set(&cfg->reset, 1);
		d->awake = false;
	}

	return err;
}

static void epd_clear(const struct device *dev)
{
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_clear(&d->frame);
}

static void epd_big_number_x10(const struct device *dev, int16_t value)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_big_number_x10(&d->frame, cfg->variant, value);
}

static void epd_small_number(const struct device *dev, int16_t value, bool percent)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_small_number(&d->frame, cfg->variant, value, percent);
}

static void epd_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_unit(&d->frame, cfg->variant,
				(unit == IOTDRV_CLIMATE_UNIT_C)   ? IOTDRV_MHO_C401_LAYOUT_UNIT_C
				: (unit == IOTDRV_CLIMATE_UNIT_F) ? IOTDRV_MHO_C401_LAYOUT_UNIT_F
							      : IOTDRV_MHO_C401_LAYOUT_UNIT_NONE);
}

static void epd_smiley(const struct device *dev, enum iotdrv_climate_smiley smiley)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_smiley(&d->frame, cfg->variant,
				  (smiley == IOTDRV_CLIMATE_SMILEY_HAPPY)
					  ? IOTDRV_MHO_C401_LAYOUT_SMILEY_HAPPY
				  : (smiley == IOTDRV_CLIMATE_SMILEY_SAD)
					  ? IOTDRV_MHO_C401_LAYOUT_SMILEY_SAD
					  : IOTDRV_MHO_C401_LAYOUT_SMILEY_OFF);
}

static void epd_ble(const struct device *dev, bool on)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_ble(&d->frame, cfg->variant, on);
}

static void epd_battery(const struct device *dev, int8_t percent)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_battery(&d->frame, cfg->variant, percent >= 0);
}

static void epd_clock(const struct device *dev, uint8_t hours, uint8_t minutes)
{
	const struct epd_config *cfg = dev->config;
	struct epd_data *d = dev->data;

	iotdrv_mho_c401_layout_clock(&d->frame, cfg->variant, hours, minutes);
}

static const struct iotdrv_climate_display_api epd_api = {
	.big_number_x10 = epd_big_number_x10,
	.small_number = epd_small_number,
	.unit = epd_unit,
	.smiley = epd_smiley,
	.ble = epd_ble,
	.battery = epd_battery,
	.clock = epd_clock,
	.clear = epd_clear,
	.flush = epd_flush,
	.off = epd_off,
};

static int epd_init(const struct device *dev)
{
	const struct epd_config *cfg = dev->config;
	const struct gpio_dt_spec *outputs[] = {&cfg->cs, &cfg->clk, &cfg->mosi, &cfg->reset};
	int err;

	if (!gpio_is_ready_dt(&cfg->busy)) {
		return -ENODEV;
	}
	for (size_t i = 0; i < ARRAY_SIZE(outputs); i++) {
		if (!gpio_is_ready_dt(outputs[i])) {
			return -ENODEV;
		}
		err = gpio_pin_configure_dt(outputs[i], GPIO_OUTPUT_INACTIVE);
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

	return 0;
}

#define EPD_INIT(inst)                                                                             \
	static struct epd_data epd_data_##inst;                                                    \
	static const struct epd_config epd_config_##inst = {                                       \
		.cs = GPIO_DT_SPEC_INST_GET(inst, cs_gpios),                                       \
		.clk = GPIO_DT_SPEC_INST_GET(inst, clk_gpios),                                     \
		.mosi = GPIO_DT_SPEC_INST_GET(inst, mosi_gpios),                                   \
		.reset = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                                 \
		.busy = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                                   \
		.enable = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}),                       \
		.variant = (DT_INST_ENUM_IDX(inst, variant) == 1) ? IOTDRV_MHO_C401_LAYOUT_C401N       \
								    : IOTDRV_MHO_C401_LAYOUT_C401,     \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, epd_init, NULL, &epd_data_##inst, &epd_config_##inst,          \
			      POST_KERNEL, CONFIG_IOTDRV_DISPLAY_MHO_C401_INIT_PRIORITY, &epd_api);

DT_INST_FOREACH_STATUS_OKAY(EPD_INIT)
