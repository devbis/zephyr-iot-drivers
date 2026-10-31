/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT belling_bl0942

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/bl0942_proto.h>
#include <iot_drivers/energy_meter.h>

LOG_MODULE_REGISTER(bl0942, CONFIG_SENSOR_LOG_LEVEL);

struct bl0942_config {
	const struct device *uart;
	struct bl0942_calib calib;
	uint8_t address;
};

struct bl0942_data {
	struct k_mutex lock;
	struct bl0942_parser parser;
	struct bl0942_energy energy;
	struct bl0942_meas meas;
	bool valid;
};

static void drain_rx(const struct device *uart)
{
	uint8_t c;

	while (uart_poll_in(uart, &c) == 0) {
	}
}

static int bl0942_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct bl0942_config *cfg = dev->config;
	struct bl0942_data *data = dev->data;
	struct bl0942_raw raw;
	uint8_t req[2];
	int64_t deadline;
	int ret = -ETIMEDOUT;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_VOLTAGE &&
	    chan != SENSOR_CHAN_CURRENT && chan != SENSOR_CHAN_POWER &&
	    chan != IOTDRV_SENSOR_CHAN_FREQUENCY && chan != IOTDRV_SENSOR_CHAN_ENERGY) {
		return -ENOTSUP;
	}

	bl0942_full_packet_request(cfg->address, req);

	k_mutex_lock(&data->lock, K_FOREVER);

	drain_rx(cfg->uart);
	bl0942_parser_init(&data->parser, cfg->address);
	uart_poll_out(cfg->uart, req[0]);
	uart_poll_out(cfg->uart, req[1]);

	deadline = k_uptime_get() + CONFIG_IOTDRV_SENSOR_BL0942_RX_TIMEOUT_MS;
	while (k_uptime_get() < deadline) {
		uint8_t c;

		if (uart_poll_in(cfg->uart, &c) != 0) {
			k_msleep(1);
			continue;
		}
		ret = bl0942_parser_push(&data->parser, c, &raw);
		if (ret == 1) {
			enum bl0942_counter_event ev;

			bl0942_convert(&raw, &cfg->calib, &data->meas);
			ev = bl0942_energy_observe(&data->energy, raw.energy_counter);
			if (ev == BL0942_COUNTER_RESET) {
				LOG_WRN("energy counter restarted");
			}
			data->valid = true;
			ret = 0;
			break;
		}
		if (ret < 0) {
			LOG_DBG("bad frame: %d", ret);
		}
		ret = -ETIMEDOUT;
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int bl0942_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct bl0942_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->valid && chan != IOTDRV_SENSOR_CHAN_ENERGY) {
		ret = -ENODATA;
	} else if (chan == SENSOR_CHAN_VOLTAGE) {
		iotdrv_energy_milli_to_value((int32_t)MIN(data->meas.voltage_mv, INT32_MAX), val);
	} else if (chan == SENSOR_CHAN_CURRENT) {
		iotdrv_energy_milli_to_value((int32_t)MIN(data->meas.current_ma, INT32_MAX), val);
	} else if (chan == SENSOR_CHAN_POWER) {
		iotdrv_energy_milli_to_value(data->meas.power_mw, val);
	} else if (chan == IOTDRV_SENSOR_CHAN_FREQUENCY) {
		iotdrv_energy_milli_to_value((int32_t)MIN(data->meas.frequency_mhz, INT32_MAX), val);
	} else if (chan == IOTDRV_SENSOR_CHAN_ENERGY) {
		iotdrv_energy_uwh_to_kwh(data->energy.total_uwh, val);
	} else {
		ret = -ENOTSUP;
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int bl0942_attr_set(const struct device *dev, enum sensor_channel chan,
			   enum sensor_attribute attr, const struct sensor_value *val)
{
	struct bl0942_data *data = dev->data;

	if (chan != IOTDRV_SENSOR_CHAN_ENERGY || attr != IOTDRV_SENSOR_ATTR_ENERGY_TOTAL) {
		return -ENOTSUP;
	}
	if (val->val1 < 0 || val->val2 < 0) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	bl0942_energy_restore(&data->energy, iotdrv_energy_kwh_to_uwh(val));
	k_mutex_unlock(&data->lock);
	return 0;
}

static int bl0942_init(const struct device *dev)
{
	const struct bl0942_config *cfg = dev->config;
	struct bl0942_data *data = dev->data;

	if (!device_is_ready(cfg->uart)) {
		LOG_ERR("UART not ready");
		return -ENODEV;
	}
	k_mutex_init(&data->lock);
	bl0942_parser_init(&data->parser, cfg->address);
	bl0942_energy_init(&data->energy, cfg->calib.energy_counts_per_kwh);
	return 0;
}

static const struct sensor_driver_api bl0942_api = {
	.sample_fetch = bl0942_sample_fetch,
	.channel_get = bl0942_channel_get,
	.attr_set = bl0942_attr_set,
};

#define BL0942_INIT(n)                                                                             \
	BUILD_ASSERT(DT_INST_PROP(n, address) <= 3, "BL0942 address is 0..3");                     \
	BUILD_ASSERT(DT_INST_PROP(n, voltage_counts_per_volt) > 0 &&                               \
			     DT_INST_PROP(n, current_counts_per_amp) > 0 &&                        \
			     DT_INST_PROP(n, power_counts_per_watt) > 0 &&                         \
			     DT_INST_PROP(n, energy_counts_per_kwh) > 0,                           \
		     "BL0942 calibration values must not be zero");                                \
	static struct bl0942_data bl0942_data_##n;                                                 \
	static const struct bl0942_config bl0942_config_##n = {                                    \
		.uart = DEVICE_DT_GET(DT_INST_BUS(n)),                                             \
		.address = DT_INST_PROP(n, address),                                               \
		.calib =                                                                           \
			{                                                                          \
				.voltage_counts_per_volt = DT_INST_PROP(n, voltage_counts_per_volt), \
				.current_counts_per_amp = DT_INST_PROP(n, current_counts_per_amp), \
				.power_counts_per_watt = DT_INST_PROP(n, power_counts_per_watt),   \
				.energy_counts_per_kwh = DT_INST_PROP(n, energy_counts_per_kwh),   \
			},                                                                         \
	};                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(n, bl0942_init, NULL, &bl0942_data_##n, &bl0942_config_##n,   \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &bl0942_api);

DT_INST_FOREACH_STATUS_OKAY(BL0942_INIT)
