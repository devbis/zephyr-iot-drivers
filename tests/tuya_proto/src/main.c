/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/ztest.h>

#include <iot_drivers/tuya_proto.h>

static int feed_all(struct iotdrv_tuya_parser *p, const uint8_t *buf, size_t len,
		    struct iotdrv_tuya_frame *out)
{
	int ret = 0;

	for (size_t i = 0; i < len; i++) {
		ret = iotdrv_tuya_parser_feed(p, buf[i], out);
		if (ret != 0) {
			break;
		}
	}

	return ret;
}

ZTEST(tuya_proto, test_encode_known_frame)
{
	/* Heartbeat from the module: 55 AA 00 00 01 00 00 00 + sum */
	struct iotdrv_tuya_frame f = {.version = 0, .seq = 1, .cmd = 0, .len = 0};
	uint8_t out[16];
	const uint8_t want[] = {0x55, 0xAA, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};

	zassert_equal(iotdrv_tuya_frame_encode(out, sizeof(out), &f), 9);
	zassert_mem_equal(out, want, sizeof(want) - 1U);
	zassert_equal(out[8], (uint8_t)(0x55 + 0xAA + 0x01));
}

ZTEST(tuya_proto, test_roundtrip_with_garbage_prefix)
{
	const uint8_t data[] = {1, 2, 3};
	struct iotdrv_tuya_frame f = {.version = 3, .seq = 0x1234, .cmd = 4, .len = 3, .data = data};
	struct iotdrv_tuya_frame r;
	struct iotdrv_tuya_parser p;
	uint8_t buf[32] = {0x00, 0x55, 0x13};
	int n;

	iotdrv_tuya_parser_init(&p);
	n = iotdrv_tuya_frame_encode(&buf[3], sizeof(buf) - 3U, &f);
	zassert_true(n > 0);
	zassert_equal(feed_all(&p, buf, 3U + n, &r), 1);
	zassert_equal(r.seq, 0x1234);
	zassert_equal(r.cmd, 4);
	zassert_equal(r.len, 3);
	zassert_mem_equal(r.data, data, 3);
}

ZTEST(tuya_proto, test_bad_checksum_resyncs)
{
	struct iotdrv_tuya_frame f = {.cmd = 0, .len = 0};
	struct iotdrv_tuya_frame r;
	struct iotdrv_tuya_parser p;
	uint8_t buf[9];

	iotdrv_tuya_parser_init(&p);
	(void)iotdrv_tuya_frame_encode(buf, sizeof(buf), &f);
	buf[8] ^= 0xFF;
	zassert_equal(feed_all(&p, buf, sizeof(buf), &r), -EBADMSG);
	buf[8] ^= 0xFF;
	zassert_equal(feed_all(&p, buf, sizeof(buf), &r), 1);
}

ZTEST(tuya_proto, test_dp_encode_and_iterate)
{
	uint8_t buf[32];
	struct iotdrv_tuya_dp dp;
	size_t off = 0U;
	int n = iotdrv_tuya_dp_encode_u32(buf, sizeof(buf), 2, IOTDRV_TUYA_DP_VALUE, 0x01020304U);

	zassert_equal(n, 8);
	n += iotdrv_tuya_dp_encode_u32(&buf[n], sizeof(buf) - n, 1, IOTDRV_TUYA_DP_BOOL, 1U);
	zassert_equal(n, 13);

	zassert_equal(iotdrv_tuya_dp_next(buf, n, &off, &dp), 0);
	zassert_equal(dp.id, 2);
	zassert_equal(iotdrv_tuya_dp_u32(&dp), 0x01020304U);
	zassert_equal(iotdrv_tuya_dp_next(buf, n, &off, &dp), 0);
	zassert_equal(dp.len, 1);
	zassert_equal(iotdrv_tuya_dp_u32(&dp), 1U);
	zassert_equal(iotdrv_tuya_dp_next(buf, n, &off, &dp), -ENOENT);
	off = 0U;
	zassert_equal(iotdrv_tuya_dp_next(buf, 6U, &off, &dp), -EBADMSG);
}

ZTEST_SUITE(tuya_proto, NULL, NULL, NULL, NULL, NULL);
