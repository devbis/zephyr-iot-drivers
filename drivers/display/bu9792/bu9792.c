/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/i2c.h>

#include <iot_drivers/lcd_bu9792.h>

#define CMD_ICSET_RESET  0xEAU
#define CMD_DISCTL       0xA4U
#define CMD_DISCTL_PS1   0xACU
#define CMD_DISCTL_PS3   0xBCU
#define CMD_BLKCTL_BASE  0xF0U
#define CMD_APCTL_NORMAL 0xFCU
#define CMD_MODE_ON_1_3  0xC8U
#define CMD_ADSET        0x00U

int iotdrv_bu9792_init(const struct device *i2c, uint8_t addr)
{
	uint8_t buf[8 + IOTDRV_BU9792_RAM_SIZE] = {
		CMD_ICSET_RESET,  CMD_DISCTL,       CMD_DISCTL_PS1, CMD_DISCTL_PS3,
		CMD_BLKCTL_BASE, CMD_APCTL_NORMAL, CMD_MODE_ON_1_3, CMD_ADSET,
	};

	return i2c_write(i2c, buf, sizeof(buf), addr);
}

int iotdrv_bu9792_write_ram(const struct device *i2c, uint8_t addr, uint8_t ram_addr,
			const uint8_t *data, size_t len)
{
	uint8_t buf[1 + IOTDRV_BU9792_RAM_SIZE];

	if ((ram_addr >= IOTDRV_BU9792_RAM_SIZE) || (len > (IOTDRV_BU9792_RAM_SIZE - ram_addr))) {
		return -EINVAL;
	}

	buf[0] = CMD_ADSET | ram_addr;
	memcpy(&buf[1], data, len);

	return i2c_write(i2c, buf, 1U + len, addr);
}

int iotdrv_bu9792_reset(const struct device *i2c, uint8_t addr)
{
	uint8_t cmd = CMD_ICSET_RESET;

	return i2c_write(i2c, &cmd, 1U, addr);
}

int iotdrv_bu9792_blink(const struct device *i2c, uint8_t addr, uint8_t rate)
{
	uint8_t cmd = CMD_BLKCTL_BASE | (rate & 0x03U);

	return i2c_write(i2c, &cmd, 1U, addr);
}
