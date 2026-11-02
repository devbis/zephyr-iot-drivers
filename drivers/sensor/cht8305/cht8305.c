/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Sensylink CHT8305: temperature and humidity in one conversion.
 *
 * A measurement is started by pointing the register pointer at the
 * temperature register; after 13 ms the next four bytes read are the raw
 * temperature and humidity (16 bits each, big endian), the chip being in
 * sequential mode:
 *   temperature = raw * 165 / 65536 - 40  degC
 *   humidity    = raw * 100 / 65536       %
 */

#define DT_DRV_COMPAT sensylink_cht8305

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(cht8305, CONFIG_SENSOR_LOG_LEVEL);

#define REG_TEMPERATURE 0x00
#define REG_CONFIG      0x02
#define REG_MANUF_ID    0xFE
#define REG_DEVICE_ID   0xFF

#define MANUF_ID 0x5959
#define DEVICE_ID 0x8305

#define CFG_SOFT_RESET 0x8000
#define CFG_MODE_BOTH  0x1000 /* temperature and humidity in one conversion */

#define RESET_MS   5
#define MEASURE_MS 13
#define RETRIES    3

struct cht8305_config {
	struct i2c_dt_spec i2c;
};

struct cht8305_data {
	uint16_t raw_temperature;
	uint16_t raw_humidity;
};

static int read_register16(const struct i2c_dt_spec *i2c, uint8_t reg, uint16_t *value)
{
	uint8_t buf[2];
	int err = i2c_write_read_dt(i2c, &reg, sizeof(reg), buf, sizeof(buf));

	if (err == 0) {
		*value = sys_get_be16(buf);
	}
	return err;
}

static int write_config(const struct i2c_dt_spec *i2c, uint16_t value)
{
	uint8_t buf[3] = {REG_CONFIG};

	sys_put_be16(value, &buf[1]);
	return i2c_write_dt(i2c, buf, sizeof(buf));
}

static int cht8305_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct cht8305_config *cfg = dev->config;
	struct cht8305_data *data = dev->data;
	const uint8_t reg = REG_TEMPERATURE;
	uint8_t buf[4];
	int err = -EIO;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_AMBIENT_TEMP &&
	    chan != SENSOR_CHAN_HUMIDITY) {
		return -ENOTSUP;
	}

	for (int i = 0; i < RETRIES && err != 0; i++) {
		err = i2c_write_dt(&cfg->i2c, &reg, sizeof(reg));
		if (err == 0) {
			k_msleep(MEASURE_MS);
			err = i2c_read_dt(&cfg->i2c, buf, sizeof(buf));
		}
	}
	if (err != 0) {
		LOG_ERR("measurement failed: %d", err);
		return err;
	}

	data->raw_temperature = sys_get_be16(&buf[0]);
	data->raw_humidity = sys_get_be16(&buf[2]);
	return 0;
}

static void from_milli(struct sensor_value *val, int32_t milli)
{
	val->val1 = milli / 1000;
	val->val2 = (milli % 1000) * 1000;
}

static int cht8305_channel_get(const struct device *dev, enum sensor_channel chan,
			       struct sensor_value *val)
{
	const struct cht8305_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		from_milli(val, (int32_t)(((int64_t)data->raw_temperature * 165000) / 65536 - 40000));
		return 0;
	case SENSOR_CHAN_HUMIDITY:
		from_milli(val, (int32_t)(((int64_t)data->raw_humidity * 100000) / 65536));
		return 0;
	default:
		return -ENOTSUP;
	}
}

static DEVICE_API(sensor, cht8305_api) = {
	.sample_fetch = cht8305_sample_fetch,
	.channel_get = cht8305_channel_get,
};

static int cht8305_init(const struct device *dev)
{
	const struct cht8305_config *cfg = dev->config;
	uint16_t manuf, id;
	int err;

	if (!device_is_ready(cfg->i2c.bus)) {
		return -ENODEV;
	}

	err = read_register16(&cfg->i2c, REG_MANUF_ID, &manuf);
	if (err == 0) {
		err = read_register16(&cfg->i2c, REG_DEVICE_ID, &id);
	}
	if (err != 0) {
		LOG_DBG("no answer: %d", err);
		return -ENODEV;
	}
	if (manuf != MANUF_ID || id != DEVICE_ID) {
		LOG_ERR("not a CHT8305: manufacturer 0x%04x, device 0x%04x", manuf, id);
		return -ENODEV;
	}

	err = write_config(&cfg->i2c, CFG_SOFT_RESET | CFG_MODE_BOTH);
	if (err == 0) {
		k_msleep(RESET_MS);
		err = write_config(&cfg->i2c, CFG_MODE_BOTH);
		k_msleep(RESET_MS);
	}

	return err;
}

#define CHT8305_INIT(inst)                                                                         \
	static struct cht8305_data cht8305_data_##inst;                                            \
	static const struct cht8305_config cht8305_config_##inst = {                               \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, cht8305_init, NULL, &cht8305_data_##inst,               \
				     &cht8305_config_##inst, POST_KERNEL,                          \
				     CONFIG_IOTDRV_SENSOR_CHT8305_INIT_PRIORITY, &cht8305_api);

DT_INST_FOREACH_STATUS_OKAY(CHT8305_INIT)
