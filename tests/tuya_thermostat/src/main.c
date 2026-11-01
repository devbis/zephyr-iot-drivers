/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <iot_drivers/tuya_thermostat.h>

/* The models, by index */
enum { R01, R02, R03, R04, R05, R06, R07, R08, R09, R0A, R0B, R0C, R0D };

static int dp_bytes(int model, enum iotdrv_tt_field f, int32_t v, uint8_t *out)
{
	return iotdrv_tt_to_dp(model, f, v, out, 32);
}

ZTEST(tt, test_every_model_has_a_name_and_the_basic_fields)
{
	char want[24];

	for (int m = 0; m < IOTDRV_TT_MODEL_COUNT; m++) {
		const struct iotdrv_tt_model *model = &iotdrv_tt_models[m];

		snprintf(want, sizeof(want), "Tuya_Thermostat_r%02X", m + 1 <= 9 ? m + 1 : 0x0a + (m - 9));
		zassert_str_equal(model->name, want);
		zassert_not_null(model->products[0]);
		zassert_not_equal(model->dp[IOTDRV_TT_F_ONOFF].id, 0, "model %d", m);
		zassert_not_equal(model->dp[IOTDRV_TT_F_TEMP].id, 0, "model %d", m);
		zassert_not_equal(model->dp[IOTDRV_TT_F_SETPOINT].id, 0, "model %d", m);
		zassert_true(model->abs_min < model->abs_max);
	}
}

ZTEST(tt, test_products_find_their_model)
{
	static const struct {
		const char *product;
		int model;
	} cases[] = {
		{"u9bfwha0", R01}, {"aoclfnxz", R01}, {"edl8pz1k", R02}, {"tagezcph", R03},
		{"xyugziqv", R04}, {"5toc8efa", R05}, {"lzriup1j", R06}, {"oh8y8pv8", R06},
		{"gops3slb", R06}, {"cvub6xbb", R06}, {"mpbki2zm", R07}, {"7rghpoxo", R08},
		{"lndsb16m", R08}, {"6a4vxfnv", R09}, {"xalsoe3m", R0A}, {"8byfmxdv", R0B},
		{"szbxmorb", R0C}, {"lpedvtvr", R0D},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		zassert_equal(iotdrv_tt_model_by_product(cases[i].product), cases[i].model, "%s",
			      cases[i].product);
	}
	zassert_equal(iotdrv_tt_model_by_product("nope"), -ENOENT);
}

ZTEST(tt, test_product_in_the_product_info)
{
	const char info[] = "{\"p\":\"u9bfwha0\",\"v\":\"1.0.0\",\"m\":0}";
	char product[9];

	zassert_equal(iotdrv_tt_product_from_info((const uint8_t *)info, sizeof(info) - 1, product,
						  sizeof(product)), 8);
	zassert_str_equal(product, "u9bfwha0");
	zassert_equal(iotdrv_tt_product_from_info((const uint8_t *)"{}", 2, product, sizeof(product)),
		      -EBADMSG);
	/* blanks around the colon, as a JSON printer writes it */
	zassert_equal(iotdrv_tt_product_from_info((const uint8_t *)"{\"p\": \"edl8pz1k\"}", 17,
						  product, sizeof(product)), 8);
	zassert_str_equal(product, "edl8pz1k");
	/* a short buffer cuts the id */
	zassert_equal(iotdrv_tt_product_from_info((const uint8_t *)info, sizeof(info) - 1, product, 4), 3);
	zassert_str_equal(product, "u9b");
}

ZTEST(tt, test_find_field)
{
	const struct iotdrv_tt_model *m = &iotdrv_tt_models[R02];

	zassert_equal(iotdrv_tt_find_field(m, 0x10), IOTDRV_TT_F_SETPOINT);
	zassert_equal(iotdrv_tt_find_field(m, 0x67), IOTDRV_TT_F_DEADZONE);
	zassert_equal(iotdrv_tt_find_field(m, 0x6e), IOTDRV_TT_F_SCHEDULE_MON);
	zassert_equal(iotdrv_tt_find_field(m, 0xee), -ENOENT);
	zassert_equal(iotdrv_tt_find_field(m, 0), -ENOENT);
}

ZTEST(tt, test_setpoint_follows_the_divisor_of_the_model)
{
	uint8_t out[32];
	int32_t v;

	/* r01 counts in degrees: 21.50 degrees goes out as 21 */
	zassert_equal(dp_bytes(R01, IOTDRV_TT_F_SETPOINT, 2150, out), 8);
	zassert_mem_equal(out, ((uint8_t[]){0x10, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x15}), 8);
	zassert_ok(iotdrv_tt_from_dp(R01, IOTDRV_TT_F_SETPOINT, (uint8_t[]){0, 0, 0, 21}, 4, &v));
	zassert_equal(v, 2100);

	/* r02 in tenths */
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_SETPOINT, 2150, out), 8);
	zassert_mem_equal(out, ((uint8_t[]){0x10, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0xd7}), 8);
	zassert_ok(iotdrv_tt_from_dp(R02, IOTDRV_TT_F_SETPOINT, (uint8_t[]){0, 0, 0, 215}, 4, &v));
	zassert_equal(v, 2150);

	zassert_ok(iotdrv_tt_from_dp(R02, IOTDRV_TT_F_TEMP, (uint8_t[]){0, 0, 0, 183}, 4, &v));
	zassert_equal(v, 1830);
}

ZTEST(tt, test_limits_are_limited_through_whole_degrees)
{
	uint8_t out[32];

	/* r02 maximum: 15.00..45.00, tenths. 40 degrees -> 400 */
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_MAX, 4000, out), 8);
	zassert_mem_equal(out, ((uint8_t[]){0x13, 0x02, 0x00, 0x04, 0x00, 0x00, 0x01, 0x90}), 8);
	/* above the range: 45 -> 450; below it: 15 -> 150 */
	dp_bytes(R02, IOTDRV_TT_F_MAX, 9000, out);
	zassert_equal(out[6] << 8 | out[7], 450);
	dp_bytes(R02, IOTDRV_TT_F_MAX, 100, out);
	zassert_equal(out[6] << 8 | out[7], 150);
	/* the fraction is lost on the way, as in the original */
	dp_bytes(R02, IOTDRV_TT_F_MAX, 4050, out);
	zassert_equal(out[6] << 8 | out[7], 400);
	/* frost protection 0..10 degrees */
	dp_bytes(R02, IOTDRV_TT_F_FROST_PROTECT, 500, out);
	zassert_equal(out[7], 50);
}

ZTEST(tt, test_deadband_and_calibration)
{
	uint8_t out[32];
	int32_t v;

	/* r01 dead band 1..5 degrees, no scaling */
	dp_bytes(R01, IOTDRV_TT_F_DEADZONE, 3, out);
	zassert_equal(out[7], 3);
	dp_bytes(R01, IOTDRV_TT_F_DEADZONE, 9, out);
	zassert_equal(out[7], 5);
	zassert_ok(iotdrv_tt_from_dp(R01, IOTDRV_TT_F_DEADZONE, (uint8_t[]){0, 0, 0, 4}, 4, &v));
	zassert_equal(v, 4);

	/* calibration: the device counts degrees, Zigbee tenths; -3.0 degrees */
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_CALIBRATION, -30, out), 8);
	zassert_mem_equal(&out[4], ((uint8_t[]){0xff, 0xff, 0xff, 0xfd}), 4);
	zassert_ok(iotdrv_tt_from_dp(R02, IOTDRV_TT_F_CALIBRATION, (uint8_t[]){0xff, 0xff, 0xff, 0xfd}, 4,
				     &v));
	zassert_equal(v, -30);
	/* and limited to +-9 degrees */
	dp_bytes(R02, IOTDRV_TT_F_CALIBRATION, 500, out);
	zassert_equal(out[7], 9);
}

ZTEST(tt, test_operation_mode_numbering)
{
	uint8_t out[32];
	int32_t v;

	/* r02 has it the other way round */
	zassert_ok(iotdrv_tt_from_dp(R02, IOTDRV_TT_F_PROG, (uint8_t[]){0}, 1, &v));
	zassert_equal(v, 1);
	zassert_ok(iotdrv_tt_from_dp(R02, IOTDRV_TT_F_PROG, (uint8_t[]){1}, 1, &v));
	zassert_equal(v, 0);
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_PROG, 1, out), 5);
	zassert_equal(out[4], 0);

	/* r0D numbers the modes differently from Zigbee: 0 setpoint, 1 preheat, 2 schedule, 3 eco */
	static const int8_t from[] = {0, 3, 1, 4};

	for (int dp = 0; dp < 4; dp++) {
		zassert_ok(iotdrv_tt_from_dp(R0D, IOTDRV_TT_F_PROG, (uint8_t[]){dp}, 1, &v));
		zassert_equal(v, from[dp]);
		zassert_equal(dp_bytes(R0D, IOTDRV_TT_F_PROG, from[dp], out), 5);
		zassert_equal(out[4], dp);
	}
	zassert_equal(iotdrv_tt_from_dp(R0D, IOTDRV_TT_F_PROG, (uint8_t[]){4}, 1, &v), -EINVAL);
	zassert_equal(dp_bytes(R0D, IOTDRV_TT_F_PROG, 2, out), -EINVAL);

	/* the others pass it through */
	zassert_ok(iotdrv_tt_from_dp(R01, IOTDRV_TT_F_PROG, (uint8_t[]){1}, 1, &v));
	zassert_equal(v, 1);
}

ZTEST(tt, test_levels)
{
	uint8_t out[32];
	int32_t v;

	/* r0D keeps the brightness in percent: 0, 5, 25, 100 */
	static const struct { uint8_t pct; int level; } from[] = {{0, 0}, {5, 1}, {20, 2}, {25, 2}, {100, 3}, {101, 0}};

	for (size_t i = 0; i < ARRAY_SIZE(from); i++) {
		zassert_ok(iotdrv_tt_from_dp(R0D, IOTDRV_TT_F_LEVEL_A, (uint8_t[]){0, 0, 0, from[i].pct}, 4, &v));
		zassert_equal(v, from[i].level, "%u", from[i].pct);
	}
	zassert_equal(dp_bytes(R0D, IOTDRV_TT_F_LEVEL_A, 2, out), 8);
	zassert_equal(out[7], 25);
	zassert_equal(dp_bytes(R0D, IOTDRV_TT_F_LEVEL_A, 4, out), -EINVAL);

	/* the highest level the Zigbee side may write depends on the model */
	zassert_equal(dp_bytes(R06, IOTDRV_TT_F_LEVEL_A, 3, out), 5);
	zassert_equal(dp_bytes(R06, IOTDRV_TT_F_LEVEL_A, 4, out), -EINVAL);
	zassert_equal(dp_bytes(R0B, IOTDRV_TT_F_LEVEL_A, 2, out), 5);
	zassert_equal(dp_bytes(R0B, IOTDRV_TT_F_LEVEL_A, 3, out), -EINVAL);
	zassert_true(dp_bytes(R04, IOTDRV_TT_F_LEVEL_A, 9, out) > 0);
	zassert_equal(dp_bytes(R04, IOTDRV_TT_F_LEVEL_A, 10, out), -EINVAL);
}

ZTEST(tt, test_switches_and_refusals)
{
	uint8_t out[32];

	/* r06's frost protection is a switch, the others' a temperature */
	zassert_equal(iotdrv_tt_field_kind(R06, IOTDRV_TT_F_FROST_PROTECT), IOTDRV_TT_K_UINT);
	zassert_equal(iotdrv_tt_field_kind(R02, IOTDRV_TT_F_FROST_PROTECT), IOTDRV_TT_K_TEMP_DEG);
	zassert_equal(dp_bytes(R06, IOTDRV_TT_F_FROST_PROTECT, 1, out), 5);
	zassert_equal(dp_bytes(R06, IOTDRV_TT_F_FROST_PROTECT, 2, out), -EINVAL);

	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_SENSOR, 2, out), 5);
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_SENSOR, 3, out), -EINVAL);
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_ONOFF, 1, out), 5);
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_ONOFF, 4, out), -EINVAL);

	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_TEMP, 2000, out), -EPERM);
	zassert_equal(dp_bytes(R01, IOTDRV_TT_F_MIN, 500, out), -ENOENT);
	zassert_true(dp_bytes(R02, IOTDRV_TT_F_SETPOINT, 2000, out) > 0);
	/* too small a buffer */
	zassert_equal(iotdrv_tt_to_dp(R02, IOTDRV_TT_F_SETPOINT, 2000, out, 4), -ENOMEM);
	/* a schedule field is not a plain one */
	zassert_equal(dp_bytes(R02, IOTDRV_TT_F_SCHEDULE_MON, 0, out), -ENOTSUP);
}

/* ---- schedules ---- */

ZTEST(tt, test_schedule_formats_of_the_models)
{
	zassert_equal(iotdrv_tt_schedule_format(R01), IOTDRV_TT_SCHED_GROUPS_3B);
	zassert_equal(iotdrv_tt_schedule_format(R0A), IOTDRV_TT_SCHED_GROUPS_U16);
	zassert_equal(iotdrv_tt_schedule_format(R0B), IOTDRV_TT_SCHED_GROUPS_U16);
	zassert_equal(iotdrv_tt_schedule_format(R06), IOTDRV_TT_SCHED_WEEKDAYS);
	zassert_equal(iotdrv_tt_schedule_format(R08), IOTDRV_TT_SCHED_EVENTS);
	static const int per_day[] = {R02, R03, R04, R05, R07, R09, R0C, R0D};

	for (size_t i = 0; i < ARRAY_SIZE(per_day); i++) {
		zassert_equal(iotdrv_tt_schedule_format(per_day[i]), IOTDRV_TT_SCHED_PER_DAY, "%d", per_day[i]);
		zassert_equal(iotdrv_tt_schedule_days(per_day[i]), 0x7f);
		zassert_equal(iotdrv_tt_schedule_transitions(per_day[i], IOTDRV_TT_DAY_TUE), 4);
	}
	zassert_equal(iotdrv_tt_schedule_days(R01), 0x43);
	zassert_equal(iotdrv_tt_schedule_transitions(R01, IOTDRV_TT_DAY_TUE), 0);
	zassert_equal(iotdrv_tt_schedule_transitions(R06, IOTDRV_TT_DAY_MON), 6);
	zassert_equal(iotdrv_tt_schedule_transitions(R06, IOTDRV_TT_DAY_SUN), 2);
	zassert_equal(iotdrv_tt_schedule_format(-1), IOTDRV_TT_SCHED_NONE);
}

/* i-th transition of a group format: h = i, m = 10 * (i % 6) */
static void fill_groups(uint8_t *raw, size_t entry, int (*temp)(int))
{
	for (int i = 0; i < 12; i++) {
		uint8_t *p = raw + i * entry;
		int t = temp(i);

		p[0] = i;
		p[1] = 10 * (i % 6);
		if (entry == 3U) {
			p[2] = t;
		} else {
			p[2] = t >> 8;
			p[3] = t;
		}
	}
}

static int half_degrees(int i)
{
	return 30 + i;
}

ZTEST(tt, test_schedule_groups_with_half_degrees)
{
	static struct iotdrv_tt_schedule s;
	uint8_t raw[36], out[64], days = 0;

	fill_groups(raw, 3, half_degrees);
	zassert_ok(iotdrv_tt_schedule_from_dp(R01, 0x65, raw, 36, &s, &days));
	zassert_equal(days, IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SAT | IOTDRV_TT_DAY_SUN);
	/* transition 3 is Monday's fourth: 03:30, 33 half degrees */
	zassert_equal(s.day[1][3].minutes, 210);
	zassert_equal(s.day[1][3].centi, 1650);
	/* 4 is Saturday's first: 04:40, 34 half degrees; 11 is Sunday's last: 11:50, 41 */
	zassert_equal(s.day[6][0].minutes, 280);
	zassert_equal(s.day[6][0].centi, 1700);
	zassert_equal(s.day[0][3].minutes, 710);
	zassert_equal(s.day[0][3].centi, 2050);

	zassert_equal(iotdrv_tt_schedule_to_dp(R01, &s, 0, out, sizeof(out)), 4 + 36);
	zassert_mem_equal(out, ((uint8_t[]){0x65, 0x00, 0x00, 36}), 4);
	zassert_mem_equal(out + 4, raw, 36);

	zassert_equal(iotdrv_tt_schedule_from_dp(R01, 0x65, raw, 35, &s, &days), -EBADMSG);
	zassert_equal(iotdrv_tt_schedule_from_dp(R01, 0x66, raw, 36, &s, &days), -ENOENT);
}

static int whole_degrees_r0a(int i)
{
	return 15 + i; /* degrees */
}

static int tenths_r0b(int i)
{
	return 150 + 5 * i; /* tenths of a degree */
}

ZTEST(tt, test_schedule_groups_with_16_bit_temperatures)
{
	static struct iotdrv_tt_schedule s;
	uint8_t raw[48], out[64], days = 0;

	/* r0A: whole degrees */
	fill_groups(raw, 4, whole_degrees_r0a);
	zassert_ok(iotdrv_tt_schedule_from_dp(R0A, 0x44, raw, 48, &s, &days));
	zassert_equal(days, 0x43);
	zassert_equal(s.day[1][0].centi, 1500);
	zassert_equal(s.day[1][3].minutes, 210);
	zassert_equal(s.day[1][3].centi, 1800);
	zassert_equal(s.day[0][3].centi, 2600);
	zassert_equal(iotdrv_tt_schedule_to_dp(R0A, &s, 0, out, sizeof(out)), 4 + 48);
	zassert_mem_equal(out + 4, raw, 48);

	/* r0B: tenths */
	fill_groups(raw, 4, tenths_r0b);
	zassert_ok(iotdrv_tt_schedule_from_dp(R0B, 0x30, raw, 48, &s, &days));
	zassert_equal(s.day[1][0].centi, 1500);
	zassert_equal(s.day[1][1].centi, 1550);
	zassert_equal(s.day[6][0].centi, 1700);
	zassert_equal(iotdrv_tt_schedule_to_dp(R0B, &s, 0, out, sizeof(out)), 4 + 48);
	zassert_mem_equal(out + 4, raw, 48);
}

ZTEST(tt, test_schedule_per_day)
{
	static struct iotdrv_tt_schedule s;
	uint8_t days = 0, out[64];
	/* Monday of r02: the first byte is not used, then 6:00 21.0, 8:30 18.0, 17:00 22.0, 22:00 16.0 */
	const uint8_t mon[17] = {1, 6, 0, 0x00, 210, 8, 30, 0x00, 180, 17, 0, 0x00, 220, 22, 0, 0x00, 160};

	zassert_ok(iotdrv_tt_schedule_from_dp(R02, 0x6e, mon, 17, &s, &days));
	zassert_equal(days, IOTDRV_TT_DAY_MON);
	zassert_equal(s.day[1][0].minutes, 360);
	zassert_equal(s.day[1][0].centi, 2100);
	zassert_equal(s.day[1][1].minutes, 510);
	zassert_equal(s.day[1][1].centi, 1800);
	zassert_equal(s.day[1][3].centi, 1600);

	/* back: Monday only */
	zassert_equal(iotdrv_tt_schedule_to_dp(R02, &s, IOTDRV_TT_DAY_MON, out, sizeof(out)), 4 + 17);
	zassert_mem_equal(out, ((uint8_t[]){0x6e, 0x00, 0x00, 17}), 4);
	zassert_mem_equal(out + 4, mon, 17);

	/* the datapoint of Sunday is another one */
	s.day[0][0] = (struct iotdrv_tt_transition){.minutes = 7 * 60 + 15, .centi = 2000};
	zassert_equal(iotdrv_tt_schedule_to_dp(R02, &s, IOTDRV_TT_DAY_SUN | IOTDRV_TT_DAY_MON, out, sizeof(out)),
		      2 * (4 + 17));
	zassert_equal(out[0], 0x65); /* Sunday first, in the order of the days */
	zassert_equal(out[4], 1);
	zassert_equal(out[5], 7);
	zassert_equal(out[6], 15);
	zassert_equal(out[7] << 8 | out[8], 200);
	zassert_equal(out[21], 0x6e);

	/* 21.5 degrees goes out as 21.0, as in the original */
	s.day[1][0].centi = 2150;
	iotdrv_tt_schedule_to_dp(R02, &s, IOTDRV_TT_DAY_MON, out, sizeof(out));
	zassert_equal(out[7] << 8 | out[8], 210);

	zassert_equal(iotdrv_tt_schedule_from_dp(R02, 0x6e, mon, 16, &s, &days), -EBADMSG);
	zassert_equal(iotdrv_tt_schedule_from_dp(R02, 0x01, mon, 17, &s, &days), -ENOENT);
}

ZTEST(tt, test_schedule_weekdays_and_weekend)
{
	static struct iotdrv_tt_schedule s;
	uint8_t raw[32], out[64], days = 0;

	/* r06: six transitions of the weekdays, two of the weekend, tenths of a degree */
	for (int i = 0; i < 8; i++) {
		int t = 200 + 5 * i;

		raw[i * 4] = 6 + i;
		raw[i * 4 + 1] = 0;
		raw[i * 4 + 2] = t >> 8;
		raw[i * 4 + 3] = t;
	}
	zassert_ok(iotdrv_tt_schedule_from_dp(R06, 0x6d, raw, 32, &s, &days));
	zassert_equal(days, IOTDRV_TT_DAY_MON | IOTDRV_TT_DAY_SUN);
	zassert_equal(s.day[1][5].minutes, 11 * 60);
	zassert_equal(s.day[1][5].centi, 2250);
	zassert_equal(s.day[0][0].minutes, 12 * 60);
	zassert_equal(s.day[0][0].centi, 2300);
	zassert_equal(s.day[0][1].centi, 2350);
	zassert_equal(iotdrv_tt_schedule_to_dp(R06, &s, 0, out, sizeof(out)), 4 + 32);
	zassert_mem_equal(out + 4, raw, 32);
}

ZTEST(tt, test_schedule_events_are_not_supported_yet)
{
	static struct iotdrv_tt_schedule s;
	uint8_t days, out[8];

	zassert_equal(iotdrv_tt_schedule_from_dp(R08, 0x6d, out, 8, &s, &days), -ENOTSUP);
	/* the other datapoints of the model are plain fields */
	zassert_equal(iotdrv_tt_schedule_from_dp(R08, 0x01, out, 1, &s, &days), -ENOENT);
	zassert_equal(iotdrv_tt_schedule_to_dp(R08, &s, 0x7f, out, sizeof(out)), -ENOTSUP);
}

ZTEST_SUITE(tt, NULL, NULL, NULL, NULL, NULL);
