/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT iotdrv_lywsd03mmc_lcd

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/climate_display.h>
#include <iot_drivers/lywsd03mmc_layout.h>

LOG_MODULE_REGISTER(lywsd03mmc, CONFIG_LOG_DEFAULT_LEVEL);

/* B1.4, B1.7 and B2.0: unidentified controller */
#define B14_I2C_ADDR 0x3CU
/* B1.9: ROHM BU9792AFUV */
#define B19_I2C_ADDR 0x3EU

#define BU9792_CMD_ICSET_RESET  0xEAU
#define BU9792_RAM_SIZE         18U
#define B14_MAX_BYTES           8U

#define UART_FRAME_START 0xAAU
#define UART_FRAME_END   0x55U

enum panel_bus {
	BUS_B14,
	BUS_B19,
	BUS_UART,
};

struct lcd_config {
	const struct device *i2c;
	const struct device *uart;
};

struct lcd_data {
	struct iotdrv_lywsd03mmc_frame frame;
	struct iotdrv_lywsd03mmc_frame shown;
	enum panel_bus bus;
	bool shown_valid;
};

static const uint8_t b14_init_cmds[] = {0x80, 0x3B, 0x80, 0x02, 0x80, 0x0F, 0x80, 0x95,
					0x80, 0x88, 0x80, 0x88, 0x80, 0x88, 0x80, 0x88,
					0x80, 0x19, 0x80, 0x28, 0x80, 0xE3, 0x80, 0x11};

static bool i2c_answers(const struct device *i2c, uint8_t addr)
{
	uint8_t none = 0U;

	/* An empty write only addresses the device, so nothing changes on it */
	return device_is_ready(i2c) && (i2c_write(i2c, &none, 0U, addr) == 0);
}

static uint8_t reverse8(uint8_t v)
{
	v = (uint8_t)(((v & 0xF0U) >> 4) | ((v & 0x0FU) << 4));
	v = (uint8_t)(((v & 0xCCU) >> 2) | ((v & 0x33U) << 2));
	v = (uint8_t)(((v & 0xAAU) >> 1) | ((v & 0x55U) << 1));

	return v;
}

static int b14_write(const struct device *i2c, const uint8_t *data, size_t len)
{
	/* Control bytes 0x80 0x40 select the data RAM, then every byte follows 0xC0 */
	uint8_t buf[2 + 2 * B14_MAX_BYTES] = {0x80, 0x40};

	for (size_t i = 0; i < len; i++) {
		buf[2U + 2U * i] = 0xC0;
		buf[3U + 2U * i] = data[i];
	}

	return i2c_write(i2c, buf, 2U + 2U * len, B14_I2C_ADDR);
}

static int b14_init(const struct device *i2c)
{
	static const uint8_t clear[B14_MAX_BYTES];
	int err;

	/* The controller needs time after power-up before it accepts commands */
	k_msleep(50);

	err = i2c_write(i2c, b14_init_cmds, sizeof(b14_init_cmds), B14_I2C_ADDR);
	if (err != 0) {
		return err;
	}

	return b14_write(i2c, clear, sizeof(clear));
}

static int b19_init(const struct device *i2c)
{
	/* Reset, display control, blink off, all pixels normal, display on at 1/3 bias,
	 * address 0, then clear the RAM
	 */
	uint8_t buf[8 + BU9792_RAM_SIZE] = {0xEA, 0xA4, 0xAC, 0xBC, 0xF0, 0xFC, 0xC8, 0x00};

	return i2c_write(i2c, buf, sizeof(buf), B19_I2C_ADDR);
}

/* The panel is wired to the BU9792 bit-reversed, in RAM pairs 4-5, 8-9 and 12-13 */
static int b19_write(const struct device *i2c, const struct iotdrv_lywsd03mmc_frame *f)
{
	for (size_t pair = 0; pair < 3U; pair++) {
		uint8_t buf[3] = {(uint8_t)(4U + 4U * pair), reverse8(f->b[2U * pair]),
				  reverse8(f->b[2U * pair + 1U])};
		int err = i2c_write(i2c, buf, sizeof(buf), B19_I2C_ADDR);

		if (err != 0) {
			return err;
		}
	}

	return 0;
}

/* Frame: four zero bytes, 0xAA, the six segment bytes in reverse, their XOR, 0x55 */
static int uart_write(const struct device *uart, const struct iotdrv_lywsd03mmc_frame *f)
{
	uint8_t buf[13] = {0, 0, 0, 0, UART_FRAME_START};
	uint8_t chk = 0U;

	for (size_t i = 0; i < IOTDRV_LYWSD03MMC_FRAME_SIZE; i++) {
		uint8_t v = f->b[IOTDRV_LYWSD03MMC_FRAME_SIZE - 1U - i];

		buf[5U + i] = v;
		chk ^= v;
	}
	buf[11] = chk;
	buf[12] = UART_FRAME_END;

	for (size_t i = 0; i < sizeof(buf); i++) {
		uart_poll_out(uart, buf[i]);
	}

	return 0;
}

static int transfer(const struct device *dev)
{
	const struct lcd_config *cfg = dev->config;
	struct lcd_data *d = dev->data;

	switch (d->bus) {
	case BUS_B14:
		return b14_write(cfg->i2c, d->frame.b, sizeof(d->frame.b));
	case BUS_B19:
		return b19_write(cfg->i2c, &d->frame);
	default:
		return uart_write(cfg->uart, &d->frame);
	}
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
	if ((err == 0) && (d->bus == BUS_B19)) {
		err = i2c_write(cfg->i2c, &reset, 1U, B19_I2C_ADDR);
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

	iotdrv_lywsd03mmc_layout_big_number_x10(&d->frame, value);
}

static void lcd_small_number(const struct device *dev, int16_t value, bool percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_lywsd03mmc_layout_small_number(&d->frame, value, percent);
}

static void lcd_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	struct lcd_data *d = dev->data;

	iotdrv_lywsd03mmc_layout_unit(&d->frame, (unit == IOTDRV_CLIMATE_UNIT_C) ? IOTDRV_LYWSD03MMC_UNIT_C
					     : (unit == IOTDRV_CLIMATE_UNIT_F) ? IOTDRV_LYWSD03MMC_UNIT_F
									   : IOTDRV_LYWSD03MMC_UNIT_NONE);
}

static void lcd_smiley(const struct device *dev, enum iotdrv_climate_smiley smiley)
{
	struct lcd_data *d = dev->data;

	iotdrv_lywsd03mmc_layout_smiley(&d->frame, (smiley == IOTDRV_CLIMATE_SMILEY_HAPPY)
						       ? IOTDRV_LYWSD03MMC_SMILEY_HAPPY
					       : (smiley == IOTDRV_CLIMATE_SMILEY_SAD)
						       ? IOTDRV_LYWSD03MMC_SMILEY_SAD
						       : IOTDRV_LYWSD03MMC_SMILEY_OFF);
}

static void lcd_ble(const struct device *dev, bool on)
{
	struct lcd_data *d = dev->data;

	iotdrv_lywsd03mmc_layout_ble(&d->frame, on);
}

static void lcd_battery(const struct device *dev, int8_t percent)
{
	struct lcd_data *d = dev->data;

	iotdrv_lywsd03mmc_layout_battery(&d->frame, percent >= 0);
}

static void lcd_clock(const struct device *dev, uint8_t hours, uint8_t minutes)
{
	struct lcd_data *d = dev->data;

	iotdrv_lywsd03mmc_layout_clock(&d->frame, hours, minutes);
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
	int err;

	if ((cfg->i2c != NULL) && i2c_answers(cfg->i2c, B14_I2C_ADDR)) {
		d->bus = BUS_B14;
		err = b14_init(cfg->i2c);
	} else if ((cfg->i2c != NULL) && i2c_answers(cfg->i2c, B19_I2C_ADDR)) {
		d->bus = BUS_B19;
		err = b19_init(cfg->i2c);
	} else if ((cfg->uart != NULL) && device_is_ready(cfg->uart)) {
		d->bus = BUS_UART;
		err = lcd_flush(dev);
	} else {
		LOG_ERR("no panel found");
		return -ENODEV;
	}

	if (err != 0) {
		LOG_ERR("panel init failed: %d", err);
	}

	return err;
}

#define LCD_PHANDLE(inst, prop)                                                                    \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, prop),                                             \
		    (DEVICE_DT_GET(DT_INST_PHANDLE(inst, prop))), (NULL))

#define LCD_INIT(inst)                                                                             \
	static struct lcd_data lcd_data_##inst;                                                    \
	static const struct lcd_config lcd_config_##inst = {                                       \
		.i2c = LCD_PHANDLE(inst, i2c),                                                     \
		.uart = LCD_PHANDLE(inst, uart),                                                   \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, lcd_init, NULL, &lcd_data_##inst, &lcd_config_##inst,          \
			      POST_KERNEL, CONFIG_IOTDRV_DISPLAY_LYWSD03MMC_INIT_PRIORITY, &lcd_api);

DT_INST_FOREACH_STATUS_OKAY(LCD_INIT)
