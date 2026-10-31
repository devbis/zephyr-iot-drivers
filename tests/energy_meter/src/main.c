/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <iot_drivers/bl0937_calc.h>
#include <iot_drivers/bl0942_proto.h>

static const struct bl0942_calib calib = {
	.voltage_counts_per_volt = 15883,
	.current_counts_per_amp = 251066,
	.power_counts_per_watt = 623,
	.energy_counts_per_kwh = 5347,
};

static void put_u24(uint8_t *b, uint32_t v)
{
	b[0] = v;
	b[1] = v >> 8;
	b[2] = v >> 16;
}

static void sample_frame(uint8_t address, uint8_t f[BL0942_FRAME_LEN])
{
	memset(f, 0, BL0942_FRAME_LEN);
	f[0] = BL0942_HEADER;
	put_u24(&f[1], 251066);
	put_u24(&f[4], 15883U * 230U);
	put_u24(&f[10], 623U * 100U);
	put_u24(&f[13], 5347);
	f[16] = 20000 & 0xFF;
	f[17] = 20000 >> 8;
	f[19] = 0x42;
	f[22] = bl0942_frame_checksum(address, f, 22);
}

ZTEST(bl0942, test_parse_and_scale)
{
	uint8_t f[BL0942_FRAME_LEN];
	struct bl0942_raw raw;
	struct bl0942_meas m;

	sample_frame(0, f);
	zassert_equal(bl0942_frame_parse(f, 0, &raw), 0);
	bl0942_convert(&raw, &calib, &m);
	zassert_equal(m.voltage_mv, 230000);
	zassert_equal(m.current_ma, 1000);
	zassert_equal(m.power_mw, 100000);
	zassert_equal(m.frequency_mhz, 50000);
	zassert_equal(raw.status, 0x42);
}

ZTEST(bl0942, test_negative_power)
{
	uint8_t f[BL0942_FRAME_LEN];
	struct bl0942_raw raw;

	sample_frame(0, f);
	put_u24(&f[10], 0xFFFFFF);
	f[22] = bl0942_frame_checksum(0, f, 22);
	zassert_equal(bl0942_frame_parse(f, 0, &raw), 0);
	zassert_equal(raw.active_power, -1);
}

ZTEST(bl0942, test_bad_checksum_and_header)
{
	uint8_t f[BL0942_FRAME_LEN];
	struct bl0942_raw raw;

	sample_frame(0, f);
	f[5] ^= 1;
	zassert_equal(bl0942_frame_parse(f, 0, &raw), -EBADMSG);
	f[0] = 0;
	zassert_equal(bl0942_frame_parse(f, 0, &raw), -EPROTO);
}

ZTEST(bl0942, test_stream_parser_skips_noise_and_resyncs)
{
	uint8_t f[BL0942_FRAME_LEN];
	struct bl0942_parser p;
	struct bl0942_raw raw;
	int ret = 0;

	sample_frame(0, f);
	bl0942_parser_init(&p, 0);
	zassert_equal(bl0942_parser_push(&p, 0x12, &raw), 0);
	for (size_t i = 0; i < sizeof(f); i++) {
		ret = bl0942_parser_push(&p, f[i], &raw);
	}
	zassert_equal(ret, 1);
	zassert_equal(p.len, 0);

	/* A stray header byte in front of a good frame: the first attempt fails
	 * on the checksum and the parser must pick the real frame up from the
	 * header inside the buffer.
	 */
	bl0942_parser_init(&p, 0);
	zassert_equal(bl0942_parser_push(&p, BL0942_HEADER, &raw), 0);
	for (size_t i = 0; i < sizeof(f) - 1; i++) {
		ret = bl0942_parser_push(&p, f[i], &raw);
	}
	zassert_equal(ret, -EBADMSG);
	zassert_equal(bl0942_parser_push(&p, f[sizeof(f) - 1], &raw), 1);
}

ZTEST(bl0942, test_write_request_address)
{
	uint8_t req[2];

	zassert_equal(bl0942_full_packet_request(2, req), 0);
	zassert_equal(req[0], 0x5A);
	zassert_equal(req[1], 0xAA);
	zassert_equal(bl0942_full_packet_request(4, req), -EINVAL);
}

ZTEST(bl0942, test_energy_wrap_vs_reset)
{
	struct bl0942_energy e;
	uint64_t before;

	bl0942_energy_init(&e, 5347);
	zassert_equal(bl0942_energy_observe(&e, 0xFFFFF0), BL0942_COUNTER_FIRST);
	zassert_equal(bl0942_energy_observe(&e, 0x000010), BL0942_COUNTER_WRAPPED);
	zassert_equal(bl0942_energy_observe(&e, 0x100000), BL0942_COUNTER_ADVANCED);
	before = e.total_uwh;
	zassert_equal(bl0942_energy_observe(&e, 100), BL0942_COUNTER_RESET);
	zassert_equal(e.total_uwh, before);
}

ZTEST(bl0942, test_energy_accumulates_and_restores)
{
	struct bl0942_energy e;

	bl0942_energy_init(&e, 5347);
	bl0942_energy_observe(&e, 0);
	bl0942_energy_observe(&e, 5347);
	zassert_equal(e.total_uwh, 1000000000ULL);

	bl0942_energy_restore(&e, 42000000);
	zassert_equal(bl0942_energy_observe(&e, 123), BL0942_COUNTER_FIRST);
	zassert_equal(e.total_uwh, 42000000ULL);
	bl0942_energy_observe(&e, 5470);
	zassert_equal(e.total_uwh, 1042000000ULL);
}

ZTEST_SUITE(bl0942, NULL, NULL, NULL, NULL, NULL);

static struct bl0937_calib simple_calib(void)
{
	return (struct bl0937_calib){
		.power_mw = {2, 1},
		.current_ma = {3, 1},
		.voltage_mv = {4, 1},
		.cf_pulses_per_kwh = 1000,
	};
}

static const struct bl0937_window win_1s = {.cf_pulses = 1, .cf1_pulses = 2, .duration_us = 1000000};

ZTEST(bl0937, test_frequency)
{
	uint64_t f;

	zassert_equal(bl0937_frequency_mhz(5, 1000000, &f), 0);
	zassert_equal(f, 5000);
	zassert_equal(bl0937_frequency_mhz(1, 0, &f), -EINVAL);
}

ZTEST(bl0937, test_first_cf1_window_after_switch_is_dropped)
{
	struct bl0937_calib c = simple_calib();
	struct bl0937 d;
	struct bl0937_result r;

	zassert_equal(bl0937_init(&d, &c, BL0937_MODE_CURRENT, true, 2), 0);
	zassert_false(bl0937_sel_high(&d));

	zassert_equal(bl0937_process_window(&d, &win_1s, &r), 0);
	zassert_false(r.cf1_valid);
	zassert_false(r.have_current);

	zassert_equal(bl0937_process_window(&d, &win_1s, &r), 0);
	zassert_equal(r.power_mw, 2000);
	zassert_true(r.have_current);
	zassert_equal(r.current_ma, 6000);
	zassert_true(r.sel_changed);
	zassert_true(r.next_sel_high);
	zassert_equal(d.mode, BL0937_MODE_VOLTAGE);
}

ZTEST(bl0937, test_sel_polarity)
{
	struct bl0937_calib c = simple_calib();
	struct bl0937 d;

	zassert_equal(bl0937_init(&d, &c, BL0937_MODE_CURRENT, false, 2), 0);
	zassert_true(bl0937_sel_high(&d));
}

ZTEST(bl0937, test_energy_keeps_fractional_pulses)
{
	struct bl0937_calib c = simple_calib();
	struct bl0937 d;
	struct bl0937_result r;

	c.cf_pulses_per_kwh = 3200;
	zassert_equal(bl0937_init(&d, &c, BL0937_MODE_CURRENT, true, 2), 0);
	for (int i = 0; i < 3200; i++) {
		zassert_equal(bl0937_process_window(&d, &win_1s, &r), 0);
	}
	zassert_equal(d.total_uwh, 1000000000ULL);
}

ZTEST(bl0937, test_current_noise_floor)
{
	struct bl0937_calib c = simple_calib();
	struct bl0937 d;
	struct bl0937_result r;
	struct bl0937_window w = {.cf_pulses = 0, .cf1_pulses = 0, .duration_us = 1000000};

	c.current_noise_floor_ma = 12000;
	zassert_equal(bl0937_init(&d, &c, BL0937_MODE_CURRENT, true, 2), 0);
	bl0937_process_window(&d, &w, &r);
	w.cf1_pulses = 4; /* 3 mA per mHz -> exactly 12000 mA */
	bl0937_process_window(&d, &w, &r);
	zassert_equal(r.current_ma, 0);

	zassert_equal(bl0937_init(&d, &c, BL0937_MODE_CURRENT, true, 2), 0);
	w.cf1_pulses = 0;
	bl0937_process_window(&d, &w, &r);
	w.cf1_pulses = 5;
	bl0937_process_window(&d, &w, &r);
	zassert_equal(r.current_ma, 15000);
}

ZTEST(bl0937, test_from_components_and_init_validation)
{
	struct bl0937_calib c;
	struct bl0937 d;

	zassert_equal(bl0937_calib_from_components(&c, 1000, 2351000, 3200), 0);
	zassert_true(c.power_mw.num / c.power_mw.den + c.current_ma.num > 0);
	zassert_equal(bl0937_calib_from_components(&c, 0, 2351000, 3200), -EINVAL);
	zassert_equal(bl0937_init(&d, &c, BL0937_MODE_CURRENT, true, 1), -EINVAL);
}

ZTEST_SUITE(bl0937, NULL, NULL, NULL, NULL, NULL);
