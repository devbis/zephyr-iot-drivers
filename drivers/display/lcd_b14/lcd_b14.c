/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include <iot_drivers/lcd_b14.h>

#define B14_MAX_BYTES 8U

static const uint8_t init_cmds[] = {0x80, 0x3B, 0x80, 0x02, 0x80, 0x0F, 0x80, 0x95,
				    0x80, 0x88, 0x80, 0x88, 0x80, 0x88, 0x80, 0x88,
				    0x80, 0x19, 0x80, 0x28, 0x80, 0xE3, 0x80, 0x11};

int iotdrv_lcd_b14_write(const struct device *i2c, uint8_t addr, const uint8_t *data, size_t len)
{
	/* Control bytes 0x80 0x40 select the data RAM, then every byte follows 0xC0 */
	uint8_t buf[2 + 2 * B14_MAX_BYTES] = {0x80, 0x40};

	if (len > B14_MAX_BYTES) {
		return -EINVAL;
	}

	for (size_t i = 0; i < len; i++) {
		buf[2U + 2U * i] = 0xC0;
		buf[3U + 2U * i] = data[i];
	}

	return i2c_write(i2c, buf, 2U + 2U * len, addr);
}

int iotdrv_lcd_b14_init(const struct device *i2c, uint8_t addr)
{
	static const uint8_t clear[B14_MAX_BYTES];
	int err;

	/* The controller needs time after power-up before it accepts commands */
	k_msleep(50);

	err = i2c_write(i2c, init_cmds, sizeof(init_cmds), addr);
	if (err != 0) {
		return err;
	}

	return iotdrv_lcd_b14_write(i2c, addr, clear, sizeof(clear));
}
