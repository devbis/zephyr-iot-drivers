/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Tuya floor-heating thermostats behind a Tuya MCU: the datapoint maps of 13
 * models and the conversions between a datapoint and the Zigbee Thermostat
 * cluster values. No Zephyr or Zigbee dependencies.
 *
 * The models are those of tuya_thermostat_zrd (r01..r0D). A model is found from
 * the product id the MCU sends in its product information; its table says which
 * datapoint carries which logical field, and the codecs turn the datapoint into
 * the value the Zigbee attribute has (and back), the way that firmware does.
 *
 * Units on the Zigbee side: temperatures in 0.01 degrees C, the dead band in
 * degrees, the calibration in 0.1 degrees C.
 */

#ifndef IOTDRV_TUYA_THERMOSTAT_H_
#define IOTDRV_TUYA_THERMOSTAT_H_

#include <stddef.h>
#include <stdint.h>

enum iotdrv_tt_field {
	IOTDRV_TT_F_ONOFF = 0,
	IOTDRV_TT_F_TEMP,
	IOTDRV_TT_F_SETPOINT,
	IOTDRV_TT_F_MIN,
	IOTDRV_TT_F_MAX,
	IOTDRV_TT_F_DEADZONE,
	IOTDRV_TT_F_CALIBRATION,
	IOTDRV_TT_F_RUNSTATE,
	IOTDRV_TT_F_SENSOR,
	IOTDRV_TT_F_PROG,
	IOTDRV_TT_F_LOCKUNLOCK,
	IOTDRV_TT_F_SCHEDULE,
	IOTDRV_TT_F_UNKNOWN,
	IOTDRV_TT_F_TEMP_OUT,
	IOTDRV_TT_F_FROST_PROTECT,
	IOTDRV_TT_F_HEAT_PROTECT,
	IOTDRV_TT_F_SCHEDULE_MON,
	IOTDRV_TT_F_SCHEDULE_TUE,
	IOTDRV_TT_F_SCHEDULE_WED,
	IOTDRV_TT_F_SCHEDULE_THU,
	IOTDRV_TT_F_SCHEDULE_FRI,
	IOTDRV_TT_F_SCHEDULE_SAT,
	IOTDRV_TT_F_SCHEDULE_SUN,
	IOTDRV_TT_F_LEVEL_A,
	IOTDRV_TT_F_LEVEL_B,
	IOTDRV_TT_F_ECO_MODE,
	IOTDRV_TT_F_ECO_HEAT_TEMP,
	IOTDRV_TT_F_ECO_COOL_TEMP,
	IOTDRV_TT_F_SOUND,
	IOTDRV_TT_F_SETTINGS_RESET,
	IOTDRV_TT_F_INVERSION,
	IOTDRV_TT_F_THERM_MODE,
	IOTDRV_TT_F_FAN_MODE,
	IOTDRV_TT_F_FAN_CONTROL,
	IOTDRV_TT_F_EXT_CALIBRATION,
	IOTDRV_TT_F_MODE_LOCK,
	IOTDRV_TT_F_HUMIDITY,
	IOTDRV_TT_F_HUMIDITY_OFFSET,
	IOTDRV_TT_F_SCREEN_TIME,
	IOTDRV_TT_F_LED_INDICATOR,
	IOTDRV_TT_FIELD_COUNT,

	/* r06 and r08 keep the schedule mode in the slot of Monday's schedule; r08 also has
	 * the datapoints of its schedule events in the slots of Tuesday to Friday. */
	IOTDRV_TT_F_SCHEDULE_MODE = IOTDRV_TT_F_SCHEDULE_MON,
	IOTDRV_TT_F_SCHEDULE_HOUR = IOTDRV_TT_F_SCHEDULE_TUE,
	IOTDRV_TT_F_SCHEDULE_MINUTE = IOTDRV_TT_F_SCHEDULE_WED,
	IOTDRV_TT_F_SCHEDULE_TEMP = IOTDRV_TT_F_SCHEDULE_THU,
	IOTDRV_TT_F_SCHEDULE_PERIOD = IOTDRV_TT_F_SCHEDULE_FRI,
};

/* One datapoint of a model. id 0: the model has no such field. */
struct iotdrv_tt_dp {
	uint8_t id;
	uint8_t type;     /* enum iotdrv_tuya_dp_type */
	uint16_t len;     /* of a raw datapoint */
	int16_t divisor;  /* how the device scales the value, see the codecs */
	int32_t min;      /* limits on the Zigbee side, INT32_MIN / INT32_MAX when there are none */
	int32_t max;
};

#define IOTDRV_TT_MODEL_COUNT 13

struct iotdrv_tt_model {
	const char *name;              /* Basic model id, Tuya_Thermostat_rXX */
	const char *const *products;   /* product ids of the MCU, NULL ended */
	int16_t abs_min;               /* AbsMinHeatSetpointLimit, 0.01 degrees */
	int16_t abs_max;
	const struct iotdrv_tt_dp *dp; /* IOTDRV_TT_FIELD_COUNT entries */
};

extern const struct iotdrv_tt_model iotdrv_tt_models[IOTDRV_TT_MODEL_COUNT];

/*
 * The product id inside the product information JSON of the MCU, e.g.
 * {"p":"u9bfwha0","v":"1.0.0"} gives "u9bfwha0". Returns its length, or
 * -EBADMSG when there is none; @p out holds at most @p size - 1 characters.
 */
int iotdrv_tt_product_from_info(const uint8_t *info, size_t len, char *out, size_t size);

/* Index of the model of a product id, -ENOENT when it is none of them. */
int iotdrv_tt_model_by_product(const char *product);

/* Field carried by a datapoint id, -ENOENT when the model has none. */
int iotdrv_tt_find_field(const struct iotdrv_tt_model *model, uint8_t dp_id);

/* How a field's Zigbee value relates to its datapoint. */
enum iotdrv_tt_kind {
	IOTDRV_TT_K_NONE,       /* not a plain field: the schedule codecs, or unsupported */
	IOTDRV_TT_K_UINT,       /* bool, enum or value passed through */
	IOTDRV_TT_K_TEMP,       /* 0.01 degrees, the device in degrees or tenths: setpoint, eco */
	IOTDRV_TT_K_TEMP_DEG,   /* the same, written to the device through whole degrees */
	IOTDRV_TT_K_DEADBAND,   /* degrees */
	IOTDRV_TT_K_CALIB,      /* 0.1 degrees, the device may count in whole degrees */
	IOTDRV_TT_K_HUMIDITY,   /* 0.01 percent */
	IOTDRV_TT_K_HUM_OFFSET, /* 0.01 percent, written through whole percents */
	IOTDRV_TT_K_PROG,       /* operation mode with the model's own numbering */
	IOTDRV_TT_K_LEVEL,      /* a level that the device keeps as a brightness percent */
};

enum iotdrv_tt_kind iotdrv_tt_field_kind(int model, enum iotdrv_tt_field field);

/* True for a field the device reports but does not accept. */
int iotdrv_tt_field_read_only(enum iotdrv_tt_field field);

/*
 * A datapoint payload (the data of the datapoint, big-endian) into the value
 * the Zigbee attribute has. -ENOENT: no such field in the model; -ENOTSUP: a
 * field the schedule codecs handle; -EINVAL: a value that means nothing.
 */
int iotdrv_tt_from_dp(int model, enum iotdrv_tt_field field, const uint8_t *data, size_t len,
		      int32_t *value);

/*
 * The datapoint to send for a Zigbee write: id, type, length and data, as
 * iotdrv_tuya_dp_encode() makes them. The value is limited to the model's range
 * first. Returns the encoded length; -ENOENT: no such field; -EPERM: read only;
 * -EINVAL: a value the device cannot take; -ENOMEM: @p size is too small.
 */
int iotdrv_tt_to_dp(int model, enum iotdrv_tt_field field, int32_t value, uint8_t *out, size_t size);

/* ---- weekly schedule ---- */

/* Bits of the Zigbee days of week, as in the Set Weekly Schedule command. */
#define IOTDRV_TT_DAY_SUN 0x01
#define IOTDRV_TT_DAY_MON 0x02
#define IOTDRV_TT_DAY_TUE 0x04
#define IOTDRV_TT_DAY_WED 0x08
#define IOTDRV_TT_DAY_THU 0x10
#define IOTDRV_TT_DAY_FRI 0x20
#define IOTDRV_TT_DAY_SAT 0x40

#define IOTDRV_TT_TRANSITIONS 6

struct iotdrv_tt_transition {
	uint16_t minutes;  /* since midnight */
	int16_t centi;     /* heat setpoint, 0.01 degrees */
};

/* Index of a day: 0 Sunday .. 6 Saturday, the order of the day bits. */
struct iotdrv_tt_schedule {
	struct iotdrv_tt_transition day[7][IOTDRV_TT_TRANSITIONS];
};

enum iotdrv_tt_schedule_format {
	IOTDRV_TT_SCHED_NONE,
	IOTDRV_TT_SCHED_GROUPS_3B,  /* one raw datapoint: Mon, Sat, Sun, 4 transitions of h, m, half degrees */
	IOTDRV_TT_SCHED_GROUPS_U16, /* one raw datapoint: Mon, Sat, Sun, 4 transitions of h, m, u16 */
	IOTDRV_TT_SCHED_PER_DAY,    /* a raw datapoint per day: 1, then 4 transitions of h, m, u16 */
	IOTDRV_TT_SCHED_WEEKDAYS,   /* one raw datapoint: Mon 6 transitions, Sun 2, h, m, u16 in tenths */
	IOTDRV_TT_SCHED_EVENTS,     /* one datapoint per period (model r08): not supported yet */
};

enum iotdrv_tt_schedule_format iotdrv_tt_schedule_format(int model);

/* The days the model keeps a schedule for, as day bits, and how many transitions each has. */
uint8_t iotdrv_tt_schedule_days(int model);
unsigned int iotdrv_tt_schedule_transitions(int model, uint8_t day_bit);

/*
 * The schedule datapoint of the MCU into @p s. @p dp_id is the datapoint's id;
 * on success *days has the days it changed. -ENOENT: not a schedule datapoint of
 * the model; -ENOTSUP: the model's format is not supported; -EBADMSG: wrong length.
 */
int iotdrv_tt_schedule_from_dp(int model, uint8_t dp_id, const uint8_t *data, size_t len,
			       struct iotdrv_tt_schedule *s, uint8_t *days);

/*
 * The datapoints to send so that the MCU gets the schedule of the days in @p days:
 * one per day for a per-day model, one for all days otherwise (whatever the
 * bits say, as the original does). Returns the length written to @p out, several
 * datapoints back to back, or a negative error as above.
 */
int iotdrv_tt_schedule_to_dp(int model, const struct iotdrv_tt_schedule *s, uint8_t days,
			     uint8_t *out, size_t size);

#endif /* IOTDRV_TUYA_THERMOSTAT_H_ */
