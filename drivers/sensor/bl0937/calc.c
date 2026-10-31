/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <iot_drivers/bl0937_calc.h>

#define UWH_PER_KWH     1000000000ULL
#define MHZ_PER_PER_US  1000000000ULL

static bool mul_overflow(uint64_t a, uint64_t b, uint64_t *r)
{
	if (a != 0 && b > UINT64_MAX / a) {
		return true;
	}
	*r = a * b;
	return false;
}

static uint64_t sat_add(uint64_t a, uint64_t b)
{
	return a > UINT64_MAX - b ? UINT64_MAX : a + b;
}

/* value * num / den without a 128-bit intermediate, saturating */
static uint64_t mul_div_sat(uint64_t value, uint64_t num, uint64_t den)
{
	uint64_t whole, frac;

	if (den == 0) {
		return UINT64_MAX;
	}
	if (mul_overflow(value / den, num, &whole)) {
		whole = UINT64_MAX;
	}
	if (mul_overflow(value % den, num, &frac)) {
		frac = UINT64_MAX;
	} else {
		frac /= den;
	}
	return sat_add(whole, frac);
}

static uint64_t scale_apply(const struct bl0937_scale *s, uint64_t mhz)
{
	return mul_div_sat(mhz, s->num, s->den);
}

int bl0937_frequency_mhz(uint32_t pulses, uint32_t duration_us, uint64_t *out)
{
	uint64_t v;

	if (duration_us == 0) {
		return -EINVAL;
	}
	if (mul_overflow(pulses, MHZ_PER_PER_US, &v)) {
		v = UINT64_MAX;
	}
	*out = v / duration_us;
	return 0;
}

int bl0937_calib_from_components(struct bl0937_calib *c, uint32_t shunt_microohms,
				 uint32_t voltage_divider_milli, uint32_t cf_pulses_per_kwh)
{
	uint64_t shunt = shunt_microohms, div = voltage_divider_milli;
	uint64_t pn, pd, cd, vn;

	if (shunt == 0 || div == 0 || cf_pulses_per_kwh == 0) {
		return -EINVAL;
	}
	if (mul_overflow(1483524ULL, div, &pn) || mul_overflow(1000ULL, shunt, &pd) ||
	    mul_overflow(pd, 1721506ULL, &pd) || mul_overflow(shunt, 94638ULL, &cd) ||
	    mul_overflow(1218ULL, div, &vn)) {
		return -EOVERFLOW;
	}

	c->power_mw = (struct bl0937_scale){pn, pd};
	c->current_ma = (struct bl0937_scale){1218000ULL, cd};
	c->voltage_mv = (struct bl0937_scale){vn, 1000000ULL * 15397ULL};
	c->cf_pulses_per_kwh = cf_pulses_per_kwh;
	c->current_noise_floor_ma = 0;
	return 0;
}

int bl0937_init(struct bl0937 *d, const struct bl0937_calib *calib, enum bl0937_mode initial,
		bool high_is_voltage, uint8_t windows_per_mode)
{
	if (windows_per_mode < 2 || calib->cf_pulses_per_kwh == 0 || calib->power_mw.num == 0 ||
	    calib->power_mw.den == 0 || calib->current_ma.num == 0 || calib->current_ma.den == 0 ||
	    calib->voltage_mv.num == 0 || calib->voltage_mv.den == 0) {
		return -EINVAL;
	}
	memset(d, 0, sizeof(*d));
	d->calib = *calib;
	d->mode = initial;
	d->high_is_voltage = high_is_voltage;
	d->windows_per_mode = windows_per_mode;
	return 0;
}

bool bl0937_sel_high(const struct bl0937 *d)
{
	return d->high_is_voltage == (d->mode == BL0937_MODE_VOLTAGE);
}

static uint64_t energy_add_pulses(struct bl0937 *d, uint32_t pulses)
{
	uint64_t v, num, delta;
	uint64_t divisor = d->calib.cf_pulses_per_kwh;

	if (mul_overflow(pulses, UWH_PER_KWH, &v)) {
		v = UINT64_MAX;
	}
	num = sat_add(d->remainder, v);
	delta = num / divisor;
	d->remainder = (uint32_t)(num % divisor);
	d->total_uwh = sat_add(d->total_uwh, delta);
	return delta;
}

int bl0937_process_window(struct bl0937 *d, const struct bl0937_window *w,
			  struct bl0937_result *out)
{
	uint64_t cf_mhz, cf1_mhz;

	if (bl0937_frequency_mhz(w->cf_pulses, w->duration_us, &cf_mhz) ||
	    bl0937_frequency_mhz(w->cf1_pulses, w->duration_us, &cf1_mhz)) {
		return -EINVAL;
	}

	memset(out, 0, sizeof(*out));
	out->cf1_valid = d->windows_since_switch != 0;
	out->power_mw = scale_apply(&d->calib.power_mw, cf_mhz);

	if (out->cf1_valid) {
		if (d->mode == BL0937_MODE_CURRENT) {
			uint64_t ma = scale_apply(&d->calib.current_ma, cf1_mhz);

			out->have_current = true;
			out->current_ma = ma <= d->calib.current_noise_floor_ma ? 0 : ma;
		} else {
			out->have_voltage = true;
			out->voltage_mv = scale_apply(&d->calib.voltage_mv, cf1_mhz);
		}
	}

	out->energy_delta_uwh = energy_add_pulses(d, w->cf_pulses);
	out->total_energy_uwh = d->total_uwh;

	if (d->windows_since_switch < UINT8_MAX) {
		d->windows_since_switch++;
	}
	if (d->windows_since_switch >= d->windows_per_mode) {
		d->mode = d->mode == BL0937_MODE_CURRENT ? BL0937_MODE_VOLTAGE : BL0937_MODE_CURRENT;
		d->windows_since_switch = 0;
		out->sel_changed = true;
		out->next_sel_high = bl0937_sel_high(d);
	}
	return 0;
}

void bl0937_restore_energy(struct bl0937 *d, uint64_t total_uwh)
{
	d->total_uwh = total_uwh;
	d->remainder = 0;
}
