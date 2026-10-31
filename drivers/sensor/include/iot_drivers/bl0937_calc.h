/* SPDX-License-Identifier: Apache-2.0 */

/*
 * BL0937 pulse-domain conversion. No Zephyr dependencies.
 *
 * The caller counts CF and CF1 pulses over a window, passes them to
 * bl0937_process_window() and drives the SEL pin to the level it returns.
 * CF1 carries current or voltage depending on SEL, so the driver alternates
 * them and drops the first window after each switch.
 *
 * Arithmetic stays within 64 bits: the TLSR8258 has no native 128-bit
 * support and the helpers cost tens of kilobytes there.
 */

#ifndef IOTDRV_BL0937_CALC_H_
#define IOTDRV_BL0937_CALC_H_

#include <stdbool.h>
#include <stdint.h>

enum bl0937_mode {
	BL0937_MODE_CURRENT,
	BL0937_MODE_VOLTAGE,
};

/* Pulse frequency in mHz times num / den */
struct bl0937_scale {
	uint64_t num;
	uint64_t den;
};

struct bl0937_calib {
	struct bl0937_scale power_mw;
	struct bl0937_scale current_ma;
	struct bl0937_scale voltage_mv;
	uint32_t cf_pulses_per_kwh;
	/* Current at or below this is reported as 0 */
	uint64_t current_noise_floor_ma;
};

struct bl0937_window {
	uint32_t cf_pulses;
	uint32_t cf1_pulses;
	uint32_t duration_us;
};

struct bl0937_result {
	uint64_t power_mw;
	bool have_current;
	uint64_t current_ma;
	bool have_voltage;
	uint64_t voltage_mv;
	uint64_t energy_delta_uwh;
	uint64_t total_energy_uwh;
	bool cf1_valid;
	bool sel_changed;
	bool next_sel_high;
};

struct bl0937 {
	struct bl0937_calib calib;
	enum bl0937_mode mode;
	bool high_is_voltage;
	uint8_t windows_per_mode;
	uint8_t windows_since_switch;
	uint32_t remainder;
	uint64_t total_uwh;
};

/* Datasheet conversion from board values. -EINVAL for zeros, -EOVERFLOW for huge inputs.
 * voltage_divider_milli is the divider ratio times 1000.
 */
int bl0937_calib_from_components(struct bl0937_calib *c, uint32_t shunt_microohms,
				 uint32_t voltage_divider_milli, uint32_t cf_pulses_per_kwh);

/* -EINVAL if windows_per_mode < 2 or the calibration has a zero */
int bl0937_init(struct bl0937 *d, const struct bl0937_calib *calib, enum bl0937_mode initial,
		bool high_is_voltage, uint8_t windows_per_mode);

bool bl0937_sel_high(const struct bl0937 *d);

/* -EINVAL on a zero duration */
int bl0937_process_window(struct bl0937 *d, const struct bl0937_window *w,
			  struct bl0937_result *out);

void bl0937_restore_energy(struct bl0937 *d, uint64_t total_uwh);

/* Frequency in mHz, saturating. -EINVAL on a zero duration. */
int bl0937_frequency_mhz(uint32_t pulses, uint32_t duration_us, uint64_t *out);

#endif /* IOTDRV_BL0937_CALC_H_ */
