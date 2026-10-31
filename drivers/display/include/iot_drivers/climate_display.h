/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Common API of the thermometer panels (segment LCDs and e-paper).
 *
 * Every panel driver in drivers/display/ implements it, so an application
 * written against this header works with any of them: only the devicetree
 * node differs. Setters edit an in-memory frame; iotdrv_climate_display_flush()
 * sends it to the panel (e-paper panels refresh then, which takes seconds).
 * Not thread safe: use a panel from one thread. Elements a panel does not
 * have are ignored.
 */

#ifndef IOTDRV_CLIMATE_DISPLAY_H_
#define IOTDRV_CLIMATE_DISPLAY_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

enum iotdrv_climate_unit {
	IOTDRV_CLIMATE_UNIT_NONE = 0,
	IOTDRV_CLIMATE_UNIT_C,
	IOTDRV_CLIMATE_UNIT_F,
};

enum iotdrv_climate_smiley {
	IOTDRV_CLIMATE_SMILEY_OFF = 0,
	IOTDRV_CLIMATE_SMILEY_HAPPY,
	IOTDRV_CLIMATE_SMILEY_SAD,
};

struct iotdrv_climate_display_api {
	void (*big_number_x10)(const struct device *dev, int16_t value);
	void (*small_number)(const struct device *dev, int16_t value, bool percent);
	void (*unit)(const struct device *dev, enum iotdrv_climate_unit unit);
	void (*smiley)(const struct device *dev, enum iotdrv_climate_smiley smiley);
	void (*ble)(const struct device *dev, bool on);
	void (*battery)(const struct device *dev, int8_t percent);
	void (*clock)(const struct device *dev, uint8_t hours, uint8_t minutes);
	void (*clear)(const struct device *dev);
	int (*flush)(const struct device *dev);
	int (*off)(const struct device *dev);
};

/* Temperature in 0.1 degrees, -995..19995. Out of range shows oL or iH. */
static inline void iotdrv_climate_display_big_number_x10(const struct device *dev, int16_t value)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->big_number_x10(dev, value);
}

/* Humidity or other small value, -9..99, with an optional "%" sign. */
static inline void iotdrv_climate_display_small_number(const struct device *dev, int16_t value,
						   bool percent)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->small_number(dev, value, percent);
}

static inline void iotdrv_climate_display_unit(const struct device *dev, enum iotdrv_climate_unit unit)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->unit(dev, unit);
}

static inline void iotdrv_climate_display_smiley(const struct device *dev,
					     enum iotdrv_climate_smiley smiley)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->smiley(dev, smiley);
}

static inline void iotdrv_climate_display_ble(const struct device *dev, bool on)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->ble(dev, on);
}

/*
 * Battery symbol: a negative percentage hides it. Panels with a level gauge
 * show the level, the others show the symbol for any value from 0 up.
 */
static inline void iotdrv_climate_display_battery(const struct device *dev, int8_t percent)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->battery(dev, percent);
}

/* Time as HH:MM, in the digit groups of the panel. */
static inline void iotdrv_climate_display_clock(const struct device *dev, uint8_t hours,
					    uint8_t minutes)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->clock(dev, hours, minutes);
}

/* Blank every element in the frame. */
static inline void iotdrv_climate_display_clear(const struct device *dev)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	api->clear(dev);
}

/* Send the frame to the panel if it changed since the last flush. */
static inline int iotdrv_climate_display_flush(const struct device *dev)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	return api->flush(dev);
}

/* Blank the panel and put it to sleep where the hardware allows it. */
static inline int iotdrv_climate_display_off(const struct device *dev)
{
	const struct iotdrv_climate_display_api *api = dev->api;

	return api->off(dev);
}

#endif /* IOTDRV_CLIMATE_DISPLAY_H_ */
