/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Channels and attributes shared by the energy metering drivers in
 * drivers/sensor/ (BL0942 over UART, BL0937 pulse counting).
 *
 * Standard channels are used where Zephyr has one:
 *   SENSOR_CHAN_VOLTAGE   volts
 *   SENSOR_CHAN_CURRENT   amperes
 *   SENSOR_CHAN_POWER     watts, negative for reverse flow
 * and these are added here:
 *   IOTDRV_SENSOR_CHAN_FREQUENCY   mains frequency in hertz (BL0942 only)
 *   IOTDRV_SENSOR_CHAN_ENERGY      lifetime energy in kilowatt-hours
 *
 * The energy total lives in RAM and starts at zero. Save it from the
 * application (it changes slowly: once per few minutes or per 10 Wh is
 * enough) and give it back with sensor_attr_set(dev, IOTDRV_SENSOR_CHAN_ENERGY,
 * IOTDRV_SENSOR_ATTR_ENERGY_TOTAL, &value) after boot.
 */

#ifndef IOTDRV_ENERGY_METER_H_
#define IOTDRV_ENERGY_METER_H_

#include <stdint.h>

#include <zephyr/drivers/sensor.h>

#define IOTDRV_SENSOR_CHAN_FREQUENCY ((enum sensor_channel)(SENSOR_CHAN_PRIV_START))
#define IOTDRV_SENSOR_CHAN_ENERGY    ((enum sensor_channel)(SENSOR_CHAN_PRIV_START + 1))

#define IOTDRV_SENSOR_ATTR_ENERGY_TOTAL ((enum sensor_attribute)(SENSOR_ATTR_PRIV_START))

/* A value in thousandths (mV, mA, mW, mHz) as a sensor_value in whole units */
static inline void iotdrv_energy_milli_to_value(int32_t milli, struct sensor_value *val)
{
	val->val1 = milli / 1000;
	val->val2 = (milli % 1000) * 1000;
}

/* Microwatt-hours as kilowatt-hours with a microkilowatt-hour fraction */
static inline void iotdrv_energy_uwh_to_kwh(uint64_t uwh, struct sensor_value *val)
{
	val->val1 = (int32_t)(uwh / 1000000000ULL);
	val->val2 = (int32_t)((uwh % 1000000000ULL) / 1000U);
}

static inline uint64_t iotdrv_energy_kwh_to_uwh(const struct sensor_value *val)
{
	if (val->val1 < 0 || val->val2 < 0) {
		return 0;
	}
	return (uint64_t)val->val1 * 1000000000ULL + (uint64_t)val->val2 * 1000U;
}

#endif /* IOTDRV_ENERGY_METER_H_ */
