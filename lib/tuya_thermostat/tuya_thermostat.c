/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <iot_drivers/tuya_proto.h>
#include <iot_drivers/tuya_thermostat.h>

/* The models, by index in iotdrv_tt_models[] */
#define MODEL_R02 1
#define MODEL_R03 2
#define MODEL_R04 3
#define MODEL_R06 5
#define MODEL_R08 7
#define MODEL_R0B 10
#define MODEL_R0D 12
#define MODEL_R01 0

/* r0D keeps the display brightness as a percent, the Zigbee side has the level 0..3 */
static const uint8_t brightness_levels[] = {0, 5, 25, 100};

int iotdrv_tt_product_from_info(const uint8_t *info, size_t len, char *out, size_t size)
{
	size_t i = 0;
	size_t n = 0;

	if (size == 0U) {
		return -EINVAL;
	}
	/* {"p":"xxxxxxxx",...}: after the first colon, past the opening quote, up to the next quote */
	while (i < len && info[i] != ':') {
		i++;
	}
	i++;
	while (i < len && (info[i] == ' ' || info[i] == '\t')) {
		i++;
	}
	if (i < len && info[i] == '"') {
		i++;
	}
	while (i < len && info[i] != '"' && n + 1U < size) {
		out[n++] = (char)info[i++];
	}
	out[n] = '\0';
	return n == 0U ? -EBADMSG : (int)n;
}

int iotdrv_tt_model_by_product(const char *product)
{
	for (int m = 0; m < IOTDRV_TT_MODEL_COUNT; m++) {
		for (const char *const *p = iotdrv_tt_models[m].products; *p != NULL; p++) {
			if (strcmp(*p, product) == 0) {
				return m;
			}
		}
	}
	return -ENOENT;
}

int iotdrv_tt_find_field(const struct iotdrv_tt_model *model, uint8_t dp_id)
{
	if (dp_id == 0U) {
		return -ENOENT;
	}
	for (int f = 0; f < IOTDRV_TT_FIELD_COUNT; f++) {
		if (model->dp[f].id == dp_id) {
			return f;
		}
	}
	return -ENOENT;
}

enum iotdrv_tt_kind iotdrv_tt_field_kind(int model, enum iotdrv_tt_field field)
{
	switch (field) {
	case IOTDRV_TT_F_TEMP:
	case IOTDRV_TT_F_TEMP_OUT:
	case IOTDRV_TT_F_SETPOINT:
	case IOTDRV_TT_F_ECO_HEAT_TEMP:
	case IOTDRV_TT_F_ECO_COOL_TEMP:
		return IOTDRV_TT_K_TEMP;
	case IOTDRV_TT_F_MIN:
	case IOTDRV_TT_F_MAX:
	case IOTDRV_TT_F_HEAT_PROTECT:
		return IOTDRV_TT_K_TEMP_DEG;
	case IOTDRV_TT_F_FROST_PROTECT:
		/* r06 has a switch, the others a temperature */
		return model == MODEL_R06 ? IOTDRV_TT_K_UINT : IOTDRV_TT_K_TEMP_DEG;
	case IOTDRV_TT_F_DEADZONE:
		return IOTDRV_TT_K_DEADBAND;
	case IOTDRV_TT_F_CALIBRATION:
	case IOTDRV_TT_F_EXT_CALIBRATION:
		return IOTDRV_TT_K_CALIB;
	case IOTDRV_TT_F_HUMIDITY:
		return IOTDRV_TT_K_HUMIDITY;
	case IOTDRV_TT_F_HUMIDITY_OFFSET:
		return IOTDRV_TT_K_HUM_OFFSET;
	case IOTDRV_TT_F_PROG:
		return (model == MODEL_R0D || model == MODEL_R02) ? IOTDRV_TT_K_PROG : IOTDRV_TT_K_UINT;
	case IOTDRV_TT_F_LEVEL_A:
		return model == MODEL_R0D ? IOTDRV_TT_K_LEVEL : IOTDRV_TT_K_UINT;
	case IOTDRV_TT_F_SCHEDULE_MON: /* r06 and r08: the schedule mode; the others: a schedule */
	case IOTDRV_TT_F_SCHEDULE_TUE: /* r08: the hour of a schedule event */
	case IOTDRV_TT_F_SCHEDULE_WED: /* r08: the minute */
	case IOTDRV_TT_F_SCHEDULE_THU: /* r08: the temperature */
	case IOTDRV_TT_F_SCHEDULE_FRI: /* r08: the period */
		return (model == MODEL_R06 || model == MODEL_R08) ? IOTDRV_TT_K_UINT : IOTDRV_TT_K_NONE;
	case IOTDRV_TT_F_ONOFF:
	case IOTDRV_TT_F_RUNSTATE:
	case IOTDRV_TT_F_SENSOR:
	case IOTDRV_TT_F_LOCKUNLOCK:
	case IOTDRV_TT_F_LEVEL_B:
	case IOTDRV_TT_F_ECO_MODE:
	case IOTDRV_TT_F_SOUND:
	case IOTDRV_TT_F_SETTINGS_RESET:
	case IOTDRV_TT_F_INVERSION:
	case IOTDRV_TT_F_THERM_MODE:
	case IOTDRV_TT_F_FAN_MODE:
	case IOTDRV_TT_F_FAN_CONTROL:
	case IOTDRV_TT_F_MODE_LOCK:
	case IOTDRV_TT_F_SCREEN_TIME:
	case IOTDRV_TT_F_LED_INDICATOR:
		return IOTDRV_TT_K_UINT;
	default:
		return IOTDRV_TT_K_NONE;
	}
}

int iotdrv_tt_field_read_only(enum iotdrv_tt_field field)
{
	return field == IOTDRV_TT_F_TEMP || field == IOTDRV_TT_F_TEMP_OUT ||
	       field == IOTDRV_TT_F_RUNSTATE || field == IOTDRV_TT_F_HUMIDITY;
}

/* Largest level the Zigbee side may write, the models differ */
static int32_t level_max(int model, enum iotdrv_tt_field field)
{
	switch (model) {
	case MODEL_R03:
		return 8;
	case MODEL_R04:
		return field == IOTDRV_TT_F_LEVEL_A ? 9 : 255;
	case MODEL_R06:
		return 3;
	case MODEL_R0B:
		return 2;
	default:
		return 255;
	}
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t be_value(const uint8_t *data, size_t len)
{
	uint32_t v = 0;

	for (size_t i = 0; i < len; i++) {
		v = (v << 8) | data[i];
	}
	return v;
}

int iotdrv_tt_from_dp(int model, enum iotdrv_tt_field field, const uint8_t *data, size_t len,
		      int32_t *value)
{
	const struct iotdrv_tt_dp *d;
	enum iotdrv_tt_kind kind;
	int32_t v;

	if (model < 0 || model >= IOTDRV_TT_MODEL_COUNT || field >= IOTDRV_TT_FIELD_COUNT) {
		return -EINVAL;
	}
	d = &iotdrv_tt_models[model].dp[field];
	if (d->id == 0U) {
		return -ENOENT;
	}
	kind = iotdrv_tt_field_kind(model, field);
	if (kind == IOTDRV_TT_K_NONE) {
		return -ENOTSUP;
	}
	if (len < 1U || len > 4U) {
		return -EINVAL;
	}
	/* A VALUE is a signed 32-bit number, a bool or an enum one byte */
	v = (int32_t)be_value(data, len);

	switch (kind) {
	case IOTDRV_TT_K_UINT:
		break;
	case IOTDRV_TT_K_TEMP:
	case IOTDRV_TT_K_TEMP_DEG:
		v *= d->divisor == 1 ? 100 : (d->divisor == 10 ? 10 : 1);
		break;
	case IOTDRV_TT_K_DEADBAND:
		v = d->divisor == 10 ? v / 10 : (d->divisor == 100 ? v / 100 : v);
		break;
	case IOTDRV_TT_K_CALIB:
		v = d->divisor == -10 ? v * 10 : (d->divisor == 10 ? v / 10 : v);
		break;
	case IOTDRV_TT_K_HUMIDITY:
		v *= d->divisor == 1 ? 100 : (d->divisor == 10 ? 10 : 1);
		break;
	case IOTDRV_TT_K_HUM_OFFSET:
		v = d->divisor == -10 ? v * 10 : (d->divisor == 10 ? v / 10 : v);
		v *= 100;
		break;
	case IOTDRV_TT_K_PROG:
		if (model == MODEL_R02) {
			v = v != 0 ? 0 : 1;
		} else { /* r0D: 0 setpoint, 1 schedule with preheat, 2 schedule, 3 eco */
			static const int8_t map[] = {0, 3, 1, 4};

			if (v < 0 || v >= (int32_t)sizeof(map)) {
				return -EINVAL;
			}
			v = map[v];
		}
		break;
	case IOTDRV_TT_K_LEVEL: {
		int32_t id = 0;

		for (int i = 0; i < (int)sizeof(brightness_levels); i++) {
			if (v <= brightness_levels[i]) {
				id = i;
				break;
			}
		}
		v = id;
		break;
	}
	default:
		return -ENOTSUP;
	}

	*value = v;
	return 0;
}

int iotdrv_tt_to_dp(int model, enum iotdrv_tt_field field, int32_t value, uint8_t *out, size_t size)
{
	const struct iotdrv_tt_dp *d;
	enum iotdrv_tt_kind kind;
	int32_t v = value;

	if (model < 0 || model >= IOTDRV_TT_MODEL_COUNT || field >= IOTDRV_TT_FIELD_COUNT) {
		return -EINVAL;
	}
	d = &iotdrv_tt_models[model].dp[field];
	if (d->id == 0U) {
		return -ENOENT;
	}
	if (iotdrv_tt_field_read_only(field)) {
		return -EPERM;
	}
	kind = iotdrv_tt_field_kind(model, field);

	switch (kind) {
	case IOTDRV_TT_K_UINT:
		if (field == IOTDRV_TT_F_SENSOR && (v < 0 || v > 2)) {
			return -EINVAL;
		}
		if ((field == IOTDRV_TT_F_ONOFF || field == IOTDRV_TT_F_LOCKUNLOCK) && (v < 0 || v > 1)) {
			return -EINVAL;
		}
		if ((field == IOTDRV_TT_F_LEVEL_A || field == IOTDRV_TT_F_LEVEL_B) &&
		    (v < 0 || v > level_max(model, field))) {
			return -EINVAL;
		}
		if (field == IOTDRV_TT_F_FROST_PROTECT && (v < 0 || v > 1)) {
			return -EINVAL; /* r06's switch */
		}
		break;
	case IOTDRV_TT_K_TEMP:
		v = clamp(v, d->min, d->max);
		v = d->divisor == 1 ? v / 100 : (d->divisor == 10 ? v / 10 : v);
		break;
	case IOTDRV_TT_K_TEMP_DEG:
		v = clamp(v, d->min, d->max);
		v /= 100; /* whole degrees */
		v = d->divisor == 10 ? v * 10 : (d->divisor == 100 ? v * 100 : v);
		break;
	case IOTDRV_TT_K_DEADBAND:
		v = clamp(v, d->min, d->max);
		v = d->divisor == 10 ? v * 10 : (d->divisor == 100 ? v * 100 : v);
		break;
	case IOTDRV_TT_K_CALIB:
		v = clamp(v, d->min, d->max);
		v = d->divisor == -10 ? v / 10 : (d->divisor == 10 ? v * 10 : v);
		break;
	case IOTDRV_TT_K_HUM_OFFSET:
		v = clamp(v, d->min, d->max);
		v /= 100;
		v = d->divisor == -10 ? v / 10 : (d->divisor == 10 ? v * 10 : v);
		break;
	case IOTDRV_TT_K_PROG:
		if (model == MODEL_R02) {
			v = v != 0 ? 0 : 1;
		} else { /* r0D */
			switch (v) {
			case 0:
				break;
			case 3:
				v = 1;
				break;
			case 1:
				v = 2;
				break;
			case 4:
				v = 3;
				break;
			default:
				return -EINVAL;
			}
		}
		break;
	case IOTDRV_TT_K_LEVEL:
		if (v < 0 || v >= (int32_t)sizeof(brightness_levels)) {
			return -EINVAL;
		}
		v = brightness_levels[v];
		break;
	default:
		return -ENOTSUP;
	}

	return iotdrv_tuya_dp_encode_u32(out, size, d->id, (enum iotdrv_tuya_dp_type)d->type,
					 (uint32_t)v);
}
