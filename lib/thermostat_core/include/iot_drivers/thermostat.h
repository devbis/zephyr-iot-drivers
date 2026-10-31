/* SPDX-License-Identifier: Apache-2.0 */

#ifndef IOTDRV_THERMOSTAT_H_
#define IOTDRV_THERMOSTAT_H_

#include <stdbool.h>
#include <stdint.h>

/* Temperatures are in hundredths of a degree Celsius, as in ZCL. */
struct iotdrv_thermostat {
	int16_t setpoint;
	int16_t hysteresis;
	bool heating;
};

void iotdrv_thermostat_init(struct iotdrv_thermostat *t, int16_t setpoint, int16_t hysteresis);

void iotdrv_thermostat_set_setpoint(struct iotdrv_thermostat *t, int16_t setpoint);

/*
 * Feed a measurement and return the heating demand: it turns on below
 * setpoint - hysteresis and off above setpoint + hysteresis.
 */
bool iotdrv_thermostat_update(struct iotdrv_thermostat *t, int16_t temperature);

#endif /* IOTDRV_THERMOSTAT_H_ */
