/* SPDX-License-Identifier: Apache-2.0 */

#include <iot_drivers/thermostat.h>

void iotdrv_thermostat_init(struct iotdrv_thermostat *t, int16_t setpoint, int16_t hysteresis)
{
	t->setpoint = setpoint;
	t->hysteresis = hysteresis;
	t->heating = false;
}

void iotdrv_thermostat_set_setpoint(struct iotdrv_thermostat *t, int16_t setpoint)
{
	t->setpoint = setpoint;
}

bool iotdrv_thermostat_update(struct iotdrv_thermostat *t, int16_t temperature)
{
	if (temperature < (int16_t)(t->setpoint - t->hysteresis)) {
		t->heating = true;
	} else if (temperature > (int16_t)(t->setpoint + t->hysteresis)) {
		t->heating = false;
	}

	return t->heating;
}
