/* SPDX-License-Identifier: Apache-2.0 */

#define DT_DRV_COMPAT belling_bl0937

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>

#include <iot_drivers/bl0937_calc.h>
#include <iot_drivers/energy_meter.h>

LOG_MODULE_REGISTER(bl0937, CONFIG_SENSOR_LOG_LEVEL);

#define WINDOWS_PER_MODE 2

struct bl0937_config {
	struct gpio_dt_spec cf;
	struct gpio_dt_spec cf1;
	struct gpio_dt_spec sel;
	struct bl0937_calib calib;
	bool sel_high_is_voltage;
};

struct bl0937_data {
	const struct device *dev;
	struct gpio_callback cf_cb;
	struct gpio_callback cf1_cb;
	struct k_work_delayable work;
	atomic_t cf_pulses;
	atomic_t cf1_pulses;
	int64_t window_start_ticks;

	struct k_spinlock lock;
	struct bl0937 state;
	uint64_t power_mw;
	uint64_t current_ma;
	uint64_t voltage_mv;
	bool have_power;
	bool have_current;
	bool have_voltage;
};

static void cf_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct bl0937_data *data = CONTAINER_OF(cb, struct bl0937_data, cf_cb);

	atomic_inc(&data->cf_pulses);
}

static void cf1_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct bl0937_data *data = CONTAINER_OF(cb, struct bl0937_data, cf1_cb);

	atomic_inc(&data->cf1_pulses);
}

static void window_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct bl0937_data *data = CONTAINER_OF(dwork, struct bl0937_data, work);
	const struct bl0937_config *cfg = data->dev->config;
	struct bl0937_window w;
	struct bl0937_result r;
	int64_t now = k_uptime_ticks();
	int ret;

	w.cf_pulses = (uint32_t)atomic_set(&data->cf_pulses, 0);
	w.cf1_pulses = (uint32_t)atomic_set(&data->cf1_pulses, 0);
	w.duration_us = (uint32_t)MIN(k_ticks_to_us_floor64(now - data->window_start_ticks),
				      UINT32_MAX);
	data->window_start_ticks = now;

	k_work_reschedule(&data->work, K_MSEC(CONFIG_IOTDRV_SENSOR_BL0937_WINDOW_MS));

	K_SPINLOCK(&data->lock) {
		ret = bl0937_process_window(&data->state, &w, &r);
		if (ret == 0) {
			data->power_mw = r.power_mw;
			data->have_power = true;
			if (r.have_current) {
				data->current_ma = r.current_ma;
				data->have_current = true;
			}
			if (r.have_voltage) {
				data->voltage_mv = r.voltage_mv;
				data->have_voltage = true;
			}
		}
	}
	if (ret != 0) {
		return;
	}

	if (r.sel_changed) {
		gpio_pin_set_dt(&cfg->sel, r.next_sel_high);
		/* Pulses of the new CF1 source count from the switch */
		atomic_set(&data->cf1_pulses, 0);
	}
}

static int bl0937_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	/* The driver measures on its own; readings are always the last window's */
	return 0;
}

static int milli_out(uint64_t milli, struct sensor_value *val)
{
	iotdrv_energy_milli_to_value((int32_t)MIN(milli, (uint64_t)INT32_MAX), val);
	return 0;
}

static int bl0937_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct bl0937_data *data = dev->data;
	uint64_t milli = 0, uwh = 0;
	bool have = true;

	K_SPINLOCK(&data->lock) {
		switch ((int)chan) {
		case SENSOR_CHAN_VOLTAGE:
			milli = data->voltage_mv;
			have = data->have_voltage;
			break;
		case SENSOR_CHAN_CURRENT:
			milli = data->current_ma;
			have = data->have_current;
			break;
		case SENSOR_CHAN_POWER:
			milli = data->power_mw;
			have = data->have_power;
			break;
		case IOTDRV_SENSOR_CHAN_ENERGY:
			uwh = data->state.total_uwh;
			break;
		default:
			have = false;
			break;
		}
	}

	if (chan == IOTDRV_SENSOR_CHAN_ENERGY) {
		iotdrv_energy_uwh_to_kwh(uwh, val);
		return 0;
	}
	if (chan != SENSOR_CHAN_VOLTAGE && chan != SENSOR_CHAN_CURRENT &&
	    chan != SENSOR_CHAN_POWER) {
		return -ENOTSUP;
	}
	return have ? milli_out(milli, val) : -ENODATA;
}

static int bl0937_attr_set(const struct device *dev, enum sensor_channel chan,
			   enum sensor_attribute attr, const struct sensor_value *val)
{
	struct bl0937_data *data = dev->data;

	if (chan != IOTDRV_SENSOR_CHAN_ENERGY || attr != IOTDRV_SENSOR_ATTR_ENERGY_TOTAL) {
		return -ENOTSUP;
	}
	if (val->val1 < 0 || val->val2 < 0) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		bl0937_restore_energy(&data->state, iotdrv_energy_kwh_to_uwh(val));
	}
	return 0;
}

static int pulse_input_init(const struct gpio_dt_spec *spec, struct gpio_callback *cb,
			    gpio_callback_handler_t handler)
{
	int ret;

	if (!gpio_is_ready_dt(spec)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	gpio_init_callback(cb, handler, BIT(spec->pin));
	ret = gpio_add_callback(spec->port, cb);
	if (ret != 0) {
		return ret;
	}
	return gpio_pin_interrupt_configure_dt(spec, GPIO_INT_EDGE_TO_ACTIVE);
}

static int bl0937_dev_init(const struct device *dev)
{
	const struct bl0937_config *cfg = dev->config;
	struct bl0937_data *data = dev->data;
	int ret;

	data->dev = dev;
	ret = bl0937_init(&data->state, &cfg->calib, BL0937_MODE_CURRENT, cfg->sel_high_is_voltage,
			  WINDOWS_PER_MODE);
	if (ret != 0) {
		return ret;
	}

	if (!gpio_is_ready_dt(&cfg->sel)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&cfg->sel, GPIO_OUTPUT);
	if (ret != 0) {
		return ret;
	}
	gpio_pin_set_dt(&cfg->sel, bl0937_sel_high(&data->state));

	ret = pulse_input_init(&cfg->cf, &data->cf_cb, cf_isr);
	if (ret != 0) {
		return ret;
	}
	ret = pulse_input_init(&cfg->cf1, &data->cf1_cb, cf1_isr);
	if (ret != 0) {
		return ret;
	}

	k_work_init_delayable(&data->work, window_work);
	data->window_start_ticks = k_uptime_ticks();
	k_work_schedule(&data->work, K_MSEC(CONFIG_IOTDRV_SENSOR_BL0937_WINDOW_MS));
	return 0;
}

static const struct sensor_driver_api bl0937_api = {
	.sample_fetch = bl0937_sample_fetch,
	.channel_get = bl0937_channel_get,
	.attr_set = bl0937_attr_set,
};

#define BL0937_CALIB(n)                                                                            \
	{                                                                                          \
		.power_mw = {1483524ULL * DT_INST_PROP(n, voltage_divider_milli),                  \
			     1000ULL * 1721506ULL * DT_INST_PROP(n, shunt_micro_ohms)},            \
		.current_ma = {1218000ULL, 94638ULL * DT_INST_PROP(n, shunt_micro_ohms)},          \
		.voltage_mv = {1218ULL * DT_INST_PROP(n, voltage_divider_milli),                   \
			       1000000ULL * 15397ULL},                                             \
		.cf_pulses_per_kwh = DT_INST_PROP(n, cf_pulses_per_kwh),                           \
		.current_noise_floor_ma = DT_INST_PROP(n, current_noise_floor_milliamps),          \
	}

#define BL0937_INIT(n)                                                                             \
	BUILD_ASSERT(DT_INST_PROP(n, shunt_micro_ohms) > 0 &&                                      \
			     DT_INST_PROP(n, voltage_divider_milli) > 0 &&                         \
			     DT_INST_PROP(n, cf_pulses_per_kwh) > 0,                               \
		     "BL0937 board values must not be zero");                                      \
	static struct bl0937_data bl0937_data_##n;                                                 \
	static const struct bl0937_config bl0937_config_##n = {                                    \
		.cf = GPIO_DT_SPEC_INST_GET(n, cf_gpios),                                          \
		.cf1 = GPIO_DT_SPEC_INST_GET(n, cf1_gpios),                                        \
		.sel = GPIO_DT_SPEC_INST_GET(n, sel_gpios),                                        \
		.sel_high_is_voltage = DT_INST_PROP(n, sel_high_is_voltage),                       \
		.calib = BL0937_CALIB(n),                                                          \
	};                                                                                         \
	SENSOR_DEVICE_DT_INST_DEFINE(n, bl0937_dev_init, NULL, &bl0937_data_##n,                  \
				     &bl0937_config_##n, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,  \
				     &bl0937_api);

DT_INST_FOREACH_STATUS_OKAY(BL0937_INIT)
