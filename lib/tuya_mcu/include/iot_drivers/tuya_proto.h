/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Tuya MCU wire format. Pure codec: no I/O, no Zephyr kernel services.
 *
 * Frame: 55 AA | version | seq (2, BE) | cmd | len (2, BE) | data | sum
 * where sum is the byte sum of everything before it.
 */

#ifndef IOTDRV_TUYA_PROTO_H_
#define IOTDRV_TUYA_PROTO_H_

#include <stddef.h>
#include <stdint.h>

#define IOTDRV_TUYA_HEADER_LEN 8U
#define IOTDRV_TUYA_MAX_DATA   128U
#define IOTDRV_TUYA_FRAME_MAX  (IOTDRV_TUYA_HEADER_LEN + IOTDRV_TUYA_MAX_DATA + 1U)

enum iotdrv_tuya_cmd {
	IOTDRV_TUYA_CMD_HEARTBEAT = 0x00,
	IOTDRV_TUYA_CMD_PRODUCT_INFO = 0x01,
	IOTDRV_TUYA_CMD_WORKING_MODE = 0x02,
	IOTDRV_TUYA_CMD_NET_STATUS = 0x03,
	IOTDRV_TUYA_CMD_SEND_DP = 0x04,
	IOTDRV_TUYA_CMD_REPORT_DP = 0x05,
	IOTDRV_TUYA_CMD_REPORT_DP_ACKED = 0x06,
	IOTDRV_TUYA_CMD_NET_STATUS_QUERY = 0x20,
	IOTDRV_TUYA_CMD_TIME_SYNC = 0x24,
	IOTDRV_TUYA_CMD_QUERY_DPS = 0x28,
};

enum iotdrv_tuya_dp_type {
	IOTDRV_TUYA_DP_RAW = 0,
	IOTDRV_TUYA_DP_BOOL = 1,
	IOTDRV_TUYA_DP_VALUE = 2,
	IOTDRV_TUYA_DP_STRING = 3,
	IOTDRV_TUYA_DP_ENUM = 4,
	IOTDRV_TUYA_DP_BITMAP = 5,
};

struct iotdrv_tuya_frame {
	uint8_t version;
	uint16_t seq;
	uint8_t cmd;
	uint16_t len;
	const uint8_t *data;
};

/* Encode @p frame into @p out. Returns the frame length or -ENOMEM / -EINVAL. */
int iotdrv_tuya_frame_encode(uint8_t *out, size_t out_size, const struct iotdrv_tuya_frame *frame);

/* Streaming frame parser; feed it bytes as they arrive. */
struct iotdrv_tuya_parser {
	uint8_t buf[IOTDRV_TUYA_FRAME_MAX];
	uint16_t pos;
};

void iotdrv_tuya_parser_init(struct iotdrv_tuya_parser *p);

/*
 * Feed one byte. Returns 1 when @p out holds a complete valid frame (its data
 * points into the parser and is valid until the next feed), 0 when more bytes
 * are needed, -EBADMSG when a frame was discarded (bad checksum or oversized).
 */
int iotdrv_tuya_parser_feed(struct iotdrv_tuya_parser *p, uint8_t byte, struct iotdrv_tuya_frame *out);

struct iotdrv_tuya_dp {
	uint8_t id;
	uint8_t type;
	uint16_t len;
	const uint8_t *data;
};

/*
 * Iterate the datapoints in a frame payload. @p off starts at 0. Returns 0 and
 * fills @p dp, -ENOENT at the end, -EBADMSG on a truncated entry.
 */
int iotdrv_tuya_dp_next(const uint8_t *buf, size_t len, size_t *off, struct iotdrv_tuya_dp *dp);

/* Append-style encoders; return the encoded length or -ENOMEM. */
int iotdrv_tuya_dp_encode(uint8_t *out, size_t out_size, uint8_t id, enum iotdrv_tuya_dp_type type,
		      const uint8_t *value, size_t len);

/* BOOL and ENUM take one byte, VALUE and BITMAP four. */
int iotdrv_tuya_dp_encode_u32(uint8_t *out, size_t out_size, uint8_t id, enum iotdrv_tuya_dp_type type,
			  uint32_t value);

/* Big-endian value of a BOOL, ENUM, VALUE or BITMAP datapoint. */
uint32_t iotdrv_tuya_dp_u32(const struct iotdrv_tuya_dp *dp);

#endif /* IOTDRV_TUYA_PROTO_H_ */
