/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Weekly schedule codecs. The formats are those of tuya_thermostat_zrd: five
 * families of models, and the day order of the Zigbee side (bit n of the day
 * mask is day n, Sunday first).
 *
 * Inherited from that firmware, on purpose: the temperatures of the per-day and
 * the whole-degree formats lose the fraction (21.5 degrees goes out as 21), and
 * the hours and minutes the MCU sends are not checked.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <iot_drivers/tuya_proto.h>
#include <iot_drivers/tuya_thermostat.h>

#define ARRAY_SIZE_(a) (sizeof(a) / sizeof((a)[0]))

#define DAY_SUN 0
#define DAY_MON 1
#define DAY_SAT 6

#define MODEL_R01 0
#define MODEL_R06 5
#define MODEL_R08 7
#define MODEL_R0A 9
#define MODEL_R0B 10

enum iotdrv_tt_schedule_format iotdrv_tt_schedule_format(int model)
{
	switch (model) {
	case MODEL_R01:
		return IOTDRV_TT_SCHED_GROUPS_3B;
	case MODEL_R0A:
	case MODEL_R0B:
		return IOTDRV_TT_SCHED_GROUPS_U16;
	case MODEL_R06:
		return IOTDRV_TT_SCHED_WEEKDAYS;
	case MODEL_R08:
		return IOTDRV_TT_SCHED_EVENTS;
	default:
		if (model < 0 || model >= IOTDRV_TT_MODEL_COUNT) {
			return IOTDRV_TT_SCHED_NONE;
		}
		return iotdrv_tt_models[model].dp[IOTDRV_TT_F_SCHEDULE_MON].id != 0U
			       ? IOTDRV_TT_SCHED_PER_DAY
			       : IOTDRV_TT_SCHED_NONE;
	}
}

uint8_t iotdrv_tt_schedule_days(int model)
{
	switch (iotdrv_tt_schedule_format(model)) {
	case IOTDRV_TT_SCHED_GROUPS_3B:
	case IOTDRV_TT_SCHED_GROUPS_U16:
		return IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SAT | IOTDRV_TT_DAY_SUN;
	case IOTDRV_TT_SCHED_PER_DAY:
		return 0x7f;
	case IOTDRV_TT_SCHED_WEEKDAYS:
		return IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SUN;
	default:
		return 0;
	}
}

unsigned int iotdrv_tt_schedule_transitions(int model, uint8_t day_bit)
{
	if ((iotdrv_tt_schedule_days(model) & day_bit) == 0U) {
		return 0;
	}
	if (iotdrv_tt_schedule_format(model) == IOTDRV_TT_SCHED_WEEKDAYS) {
		return day_bit == IOTDRV_TT_DAY_MON ? 6U : 2U;
	}
	return 4U;
}

/* The datapoint of a day of a per-day model: SCHEDULE_MON .. SCHEDULE_SUN, Sunday last in the table */
static enum iotdrv_tt_field per_day_field(int day)
{
	static const uint8_t order[7] = {
		IOTDRV_TT_F_SCHEDULE_SUN, IOTDRV_TT_F_SCHEDULE_MON, IOTDRV_TT_F_SCHEDULE_TUE,
		IOTDRV_TT_F_SCHEDULE_WED, IOTDRV_TT_F_SCHEDULE_THU, IOTDRV_TT_F_SCHEDULE_FRI,
		IOTDRV_TT_F_SCHEDULE_SAT,
	};

	return order[day];
}

static void put_transition(struct iotdrv_tt_transition *t, unsigned int hour, unsigned int minute,
			   int32_t centi)
{
	t->minutes = (uint16_t)(hour * 60U + minute);
	t->centi = (int16_t)centi;
}

int iotdrv_tt_schedule_from_dp(int model, uint8_t dp_id, const uint8_t *data, size_t len,
			       struct iotdrv_tt_schedule *s, uint8_t *days)
{
	const struct iotdrv_tt_model *m;
	enum iotdrv_tt_schedule_format fmt = iotdrv_tt_schedule_format(model);
	static const uint8_t groups[3] = {DAY_MON, DAY_SAT, DAY_SUN};

	if (fmt == IOTDRV_TT_SCHED_NONE) {
		return -ENOENT;
	}
	m = &iotdrv_tt_models[model];
	if (fmt == IOTDRV_TT_SCHED_EVENTS) {
		/* only the datapoints of the schedule events are not supported, the rest is a plain field */
		static const uint8_t slots[] = {IOTDRV_TT_F_SCHEDULE_HOUR, IOTDRV_TT_F_SCHEDULE_MINUTE,
						IOTDRV_TT_F_SCHEDULE_TEMP, IOTDRV_TT_F_SCHEDULE_PERIOD};

		for (size_t i = 0; i < ARRAY_SIZE_(slots); i++) {
			if (dp_id != 0U && dp_id == m->dp[slots[i]].id) {
				return -ENOTSUP;
			}
		}
		return -ENOENT;
	}

	switch (fmt) {
	case IOTDRV_TT_SCHED_GROUPS_3B:
		if (dp_id != m->dp[IOTDRV_TT_F_SCHEDULE].id) {
			return -ENOENT;
		}
		if (len != 36U) {
			return -EBADMSG;
		}
		for (unsigned int g = 0; g < 3U; g++) {
			for (unsigned int i = 0; i < 4U; i++) {
				const uint8_t *p = &data[(g * 4U + i) * 3U];

				/* the temperature is in half degrees */
				put_transition(&s->day[groups[g]][i], p[0], p[1], ((int32_t)p[2] * 100) / 2);
			}
		}
		*days = IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SAT | IOTDRV_TT_DAY_SUN;
		return 0;

	case IOTDRV_TT_SCHED_GROUPS_U16: {
		/* r0A counts in whole degrees, r0B in tenths */
		const int32_t scale = model == MODEL_R0A ? 100 : 10;

		if (dp_id != m->dp[IOTDRV_TT_F_SCHEDULE].id) {
			return -ENOENT;
		}
		if (len != 48U) {
			return -EBADMSG;
		}
		for (unsigned int g = 0; g < 3U; g++) {
			for (unsigned int i = 0; i < 4U; i++) {
				const uint8_t *p = &data[(g * 4U + i) * 4U];

				put_transition(&s->day[groups[g]][i], p[0], p[1],
					       (int32_t)(((unsigned int)p[2] << 8) | p[3]) * scale);
			}
		}
		*days = IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SAT | IOTDRV_TT_DAY_SUN;
		return 0;
	}

	case IOTDRV_TT_SCHED_PER_DAY:
		for (int day = 0; day < 7; day++) {
			const struct iotdrv_tt_dp *d = &m->dp[per_day_field(day)];

			if (d->id != dp_id) {
				continue;
			}
			if (len != 17U) {
				return -EBADMSG;
			}
			/* the first byte is not used, then four transitions */
			for (unsigned int i = 0; i < 4U; i++) {
				const uint8_t *p = &data[1U + i * 4U];
				const int32_t temp = (int32_t)(((unsigned int)p[2] << 8) | p[3]);

				put_transition(&s->day[day][i], p[0], p[1],
					       (d->divisor != 0 ? temp / d->divisor : temp) * 100);
			}
			*days = (uint8_t)(1U << day);
			return 0;
		}
		return -ENOENT;

	case IOTDRV_TT_SCHED_WEEKDAYS:
		if (dp_id != m->dp[IOTDRV_TT_F_SCHEDULE].id) {
			return -ENOENT;
		}
		if (len != 32U) {
			return -EBADMSG;
		}
		/* six transitions of the weekdays, then two of the weekend; tenths of a degree */
		for (unsigned int i = 0; i < 8U; i++) {
			const uint8_t *p = &data[i * 4U];

			put_transition(i < 6U ? &s->day[DAY_MON][i] : &s->day[DAY_SUN][i - 6U], p[0], p[1],
				       (int32_t)(((unsigned int)p[2] << 8) | p[3]) * 10);
		}
		*days = IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SUN;
		return 0;

	default:
		return -ENOENT;
	}
}

static void put_u16(uint8_t *p, unsigned int v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

int iotdrv_tt_schedule_to_dp(int model, const struct iotdrv_tt_schedule *s, uint8_t days,
			     uint8_t *out, size_t size)
{
	const struct iotdrv_tt_model *m;
	enum iotdrv_tt_schedule_format fmt = iotdrv_tt_schedule_format(model);
	static const uint8_t groups[3] = {DAY_MON, DAY_SAT, DAY_SUN};
	uint8_t raw[48];
	size_t total = 0;
	int n;

	if (fmt == IOTDRV_TT_SCHED_NONE) {
		return -ENOENT;
	}
	if (fmt == IOTDRV_TT_SCHED_EVENTS) {
		return -ENOTSUP;
	}
	m = &iotdrv_tt_models[model];

	switch (fmt) {
	case IOTDRV_TT_SCHED_GROUPS_3B:
		for (unsigned int g = 0; g < 3U; g++) {
			for (unsigned int i = 0; i < 4U; i++) {
				const struct iotdrv_tt_transition *t = &s->day[groups[g]][i];
				uint8_t *p = &raw[(g * 4U + i) * 3U];

				p[0] = (uint8_t)(t->minutes / 60U);
				p[1] = (uint8_t)(t->minutes % 60U);
				p[2] = (uint8_t)(((int32_t)t->centi * 2) / 100);
			}
		}
		return iotdrv_tuya_dp_encode(out, size, m->dp[IOTDRV_TT_F_SCHEDULE].id,
					     IOTDRV_TUYA_DP_RAW, raw, 36U);

	case IOTDRV_TT_SCHED_GROUPS_U16: {
		const int32_t scale = model == MODEL_R0A ? 100 : 10;

		for (unsigned int g = 0; g < 3U; g++) {
			for (unsigned int i = 0; i < 4U; i++) {
				const struct iotdrv_tt_transition *t = &s->day[groups[g]][i];
				uint8_t *p = &raw[(g * 4U + i) * 4U];

				p[0] = (uint8_t)(t->minutes / 60U);
				p[1] = (uint8_t)(t->minutes % 60U);
				put_u16(&p[2], (unsigned int)((int32_t)t->centi / scale));
			}
		}
		return iotdrv_tuya_dp_encode(out, size, m->dp[IOTDRV_TT_F_SCHEDULE].id,
					     IOTDRV_TUYA_DP_RAW, raw, 48U);
	}

	case IOTDRV_TT_SCHED_PER_DAY:
		for (int day = 0; day < 7; day++) {
			const struct iotdrv_tt_dp *d = &m->dp[per_day_field(day)];

			if ((days & (1U << day)) == 0U || d->id == 0U) {
				continue;
			}
			raw[0] = 1;
			for (unsigned int i = 0; i < 4U; i++) {
				const struct iotdrv_tt_transition *t = &s->day[day][i];
				uint8_t *p = &raw[1U + i * 4U];

				p[0] = (uint8_t)(t->minutes / 60U);
				p[1] = (uint8_t)(t->minutes % 60U);
				put_u16(&p[2], (unsigned int)(((int32_t)t->centi / 100) * d->divisor));
			}
			n = iotdrv_tuya_dp_encode(out + total, size - total, d->id, IOTDRV_TUYA_DP_RAW,
						  raw, 17U);
			if (n < 0) {
				return n;
			}
			total += (size_t)n;
		}
		return (int)total;

	case IOTDRV_TT_SCHED_WEEKDAYS:
		for (unsigned int i = 0; i < 8U; i++) {
			const struct iotdrv_tt_transition *t =
				i < 6U ? &s->day[DAY_MON][i] : &s->day[DAY_SUN][i - 6U];
			uint8_t *p = &raw[i * 4U];

			p[0] = (uint8_t)(t->minutes / 60U);
			p[1] = (uint8_t)(t->minutes % 60U);
			put_u16(&p[2], (unsigned int)((int32_t)t->centi / 10));
		}
		return iotdrv_tuya_dp_encode(out, size, m->dp[IOTDRV_TT_F_SCHEDULE].id,
					     IOTDRV_TUYA_DP_RAW, raw, 32U);

	default:
		return -ENOENT;
	}
}
