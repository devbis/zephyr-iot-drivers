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
	uint32_t last_cf_tick;   /* timer counting: the tick at the start of the window */
	uint32_t last_cf1_tick;
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

#if defined(CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT)
/*
 * The TLSR8258 timers in the GPIO trigger mode (datasheet 5.1.3 and 7.1.2): the tick of a timer goes
 * up on each edge of the pin routed to it. The counting signal of a timer is
 * (input ^ polarity) & m<timer>, m0/m1/m2 at 0x5b8/0x5c0/0x5c8 + port, the polarity at 0x584 + 8 * port.
 * Timer0 counts CF, Timer2 CF1 (Timer1 is the SoC sleep code's). The capture value is the largest,
 * so the timer never stops.
 */
#define TLSR_REG8(a)  (*(volatile uint8_t *)(a))
#define TLSR_REG32(a) (*(volatile uint32_t *)(a))
#define TLSR_TMR_CTRL     0x00800620u /* [0] en0 [2:1] mode0 | [6] en2 [7] mode2 lsb, 0x621[0] msb */
#define TLSR_TMR_CAPT(t)  (0x00800624u + 4u * (t))
#define TLSR_TMR_TICK(t)  (0x00800630u + 4u * (t))
#define TLSR_GPIO_POL(g)  (0x00800584u + 8u * (g))
#define TLSR_GPIO_ROUTE(t, g) ((t) == 0 ? 0x008005b8u + (g) : (t) == 1 ? 0x008005c0u + (g) : 0x008005c8u + (g))

#define CF_TIMER  0
#define CF1_TIMER 2

static int gpio_group(const struct device *port)
{
	static const struct device *const ports[] = {
		DEVICE_DT_GET(DT_NODELABEL(gpioa)), DEVICE_DT_GET(DT_NODELABEL(gpiob)),
		DEVICE_DT_GET(DT_NODELABEL(gpioc)), DEVICE_DT_GET(DT_NODELABEL(gpiod)),
#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(gpioe))
		DEVICE_DT_GET(DT_NODELABEL(gpioe)),
#endif
	};

	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		if (port == ports[i]) {
			return (int)i;
		}
	}
	return -ENODEV;
}

static void timer_enable(unsigned int t, bool on)
{
	if (t == 0) {
		uint8_t c = TLSR_REG8(TLSR_TMR_CTRL) & (uint8_t)~(BIT(0) | (3u << 1));

		TLSR_REG8(TLSR_TMR_CTRL) = on ? (uint8_t)(c | BIT(0) | (1u << 1)) : c; /* mode 1 */
	} else {
		/* Timer2: enable bit 6, mode in 0x620[7] (low) and 0x621[0] (high); mode 1 = 01 */
		uint8_t c = TLSR_REG8(TLSR_TMR_CTRL) & (uint8_t)~(BIT(6) | BIT(7));

		TLSR_REG8(TLSR_TMR_CTRL + 1u) &= (uint8_t)~BIT(0);
		TLSR_REG8(TLSR_TMR_CTRL) = on ? (uint8_t)(c | BIT(6) | BIT(7)) : c;
	}
}

static int pulse_timer_init(const struct gpio_dt_spec *spec, unsigned int t)
{
	int ret = gpio_pin_configure_dt(spec, GPIO_INPUT);
	int g = gpio_group(spec->port);
	uint8_t bit = BIT(spec->pin);

	if (ret != 0) {
		return ret;
	}
	if (g < 0) {
		return g;
	}

	timer_enable(t, false);
	TLSR_REG32(TLSR_TMR_TICK(t)) = 0;
	TLSR_REG32(TLSR_TMR_CAPT(t)) = UINT32_MAX;
	/* the edge to count: rising for an active-high pin, falling for an active-low one */
	if ((spec->dt_flags & GPIO_ACTIVE_LOW) != 0) {
		TLSR_REG8(TLSR_GPIO_POL(g)) |= bit;
	} else {
		TLSR_REG8(TLSR_GPIO_POL(g)) &= (uint8_t)~bit;
	}
	TLSR_REG8(TLSR_GPIO_ROUTE(t, g)) |= bit;
	timer_enable(t, true);
	return 0;
}

static uint32_t pulse_tick(unsigned int t)
{
	return TLSR_REG32(TLSR_TMR_TICK(t));
}
#endif /* CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT */

#if !defined(CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT)
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

#endif

static void window_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct bl0937_data *data = CONTAINER_OF(dwork, struct bl0937_data, work);
	const struct bl0937_config *cfg = data->dev->config;
	struct bl0937_window w;
	struct bl0937_result r;
	int64_t now = k_uptime_ticks();
	int ret;

#if defined(CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT)
	{
		uint32_t cf = pulse_tick(CF_TIMER), cf1 = pulse_tick(CF1_TIMER);

		w.cf_pulses = cf - data->last_cf_tick;
		w.cf1_pulses = cf1 - data->last_cf1_tick;
		data->last_cf_tick = cf;
		data->last_cf1_tick = cf1;
	}
#else
	w.cf_pulses = (uint32_t)atomic_set(&data->cf_pulses, 0);
	w.cf1_pulses = (uint32_t)atomic_set(&data->cf1_pulses, 0);
#endif
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
#if defined(CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT)
		data->last_cf1_tick = pulse_tick(CF1_TIMER);
#else
		atomic_set(&data->cf1_pulses, 0);
#endif
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

#if !defined(CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT)
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
#endif

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

#if defined(CONFIG_IOTDRV_SENSOR_BL0937_TIMER_COUNT)
	ret = pulse_timer_init(&cfg->cf, CF_TIMER);
	if (ret != 0) {
		return ret;
	}
	ret = pulse_timer_init(&cfg->cf1, CF1_TIMER);
	if (ret != 0) {
		return ret;
	}
	data->last_cf_tick = pulse_tick(CF_TIMER);
	data->last_cf1_tick = pulse_tick(CF1_TIMER);
#else
	ret = pulse_input_init(&cfg->cf, &data->cf_cb, cf_isr);
	if (ret != 0) {
		return ret;
	}
	ret = pulse_input_init(&cfg->cf1, &data->cf1_cb, cf1_isr);
	if (ret != 0) {
		return ret;
	}
#endif

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

/* A scale given in the devicetree (<num den>, measured on the product), else the datasheet's */
#define BL0937_SCALE(n, prop, def_num, def_den)                                                    \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, prop),                                                \
		    ({DT_INST_PROP_BY_IDX(n, prop, 0), DT_INST_PROP_BY_IDX(n, prop, 1)}),          \
		    ({def_num, def_den}))

#define BL0937_CALIB(n)                                                                            \
	{                                                                                          \
		.power_mw = BL0937_SCALE(n, power_mw_per_mhz,                                      \
					 1483524ULL * DT_INST_PROP(n, voltage_divider_milli),      \
					 1000ULL * 1721506ULL * DT_INST_PROP(n, shunt_micro_ohms)), \
		.current_ma = BL0937_SCALE(n, current_ma_per_mhz, 1218000ULL,                      \
					   94638ULL * DT_INST_PROP(n, shunt_micro_ohms)),          \
		.voltage_mv = BL0937_SCALE(n, voltage_mv_per_mhz,                                  \
					   1218ULL * DT_INST_PROP(n, voltage_divider_milli),       \
					   1000000ULL * 15397ULL),                                 \
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
