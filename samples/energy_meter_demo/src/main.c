/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Prints the readings of the energy meter aliased as "energy-meter" (a
 * belling,bl0942 or belling,bl0937 node) every two seconds. Add the node and
 * the alias in a board overlay, for example:
 *
 *   / { aliases { energy-meter = &meter; }; };
 *   &uart0 { current-speed = <4800>; meter: bl0942 { compatible = "belling,bl0942"; }; };
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>

#include <iot_drivers/energy_meter.h>

static double to_double(const struct sensor_value *v)
{
	return sensor_value_to_double(v);
}

int main(void)
{
	const struct device *meter = DEVICE_DT_GET(DT_ALIAS(energy_meter));
	struct sensor_value v, a, w, f, e;

	if (!device_is_ready(meter)) {
		printk("energy meter is not ready\n");
		return 0;
	}

	while (true) {
		if (sensor_sample_fetch(meter) == 0) {
			/* A pulse-counting meter has no reading until its first windows end */
			bool ok = sensor_channel_get(meter, SENSOR_CHAN_VOLTAGE, &v) == 0;

			ok &= sensor_channel_get(meter, SENSOR_CHAN_CURRENT, &a) == 0;
			ok &= sensor_channel_get(meter, SENSOR_CHAN_POWER, &w) == 0;
			if (ok) {
				printk("%.1f V  %.3f A  %.1f W", to_double(&v), to_double(&a),
				       to_double(&w));
				if (sensor_channel_get(meter, IOTDRV_SENSOR_CHAN_FREQUENCY, &f) == 0) {
					printk("  %.2f Hz", to_double(&f));
				}
				sensor_channel_get(meter, IOTDRV_SENSOR_CHAN_ENERGY, &e);
				printk("  %.3f kWh\n", to_double(&e));
			}
		}
		k_sleep(K_SECONDS(2));
	}
}
