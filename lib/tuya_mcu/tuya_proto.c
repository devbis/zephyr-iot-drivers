/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <iot_drivers/tuya_proto.h>

#define IOTDRV_TUYA_START1 0x55U
#define IOTDRV_TUYA_START2 0xAAU

static uint8_t sum8(const uint8_t *p, size_t len)
{
	uint8_t sum = 0U;

	for (size_t i = 0; i < len; i++) {
		sum += p[i];
	}

	return sum;
}

int iotdrv_tuya_frame_encode(uint8_t *out, size_t out_size, const struct iotdrv_tuya_frame *frame)
{
	size_t total = IOTDRV_TUYA_HEADER_LEN + frame->len + 1U;

	if ((frame->len > 0U) && (frame->data == NULL)) {
		return -EINVAL;
	}

	if ((frame->len > IOTDRV_TUYA_MAX_DATA) || (out_size < total)) {
		return -ENOMEM;
	}

	out[0] = IOTDRV_TUYA_START1;
	out[1] = IOTDRV_TUYA_START2;
	out[2] = frame->version;
	out[3] = (uint8_t)(frame->seq >> 8);
	out[4] = (uint8_t)frame->seq;
	out[5] = frame->cmd;
	out[6] = (uint8_t)(frame->len >> 8);
	out[7] = (uint8_t)frame->len;
	if (frame->len > 0U) {
		memcpy(&out[IOTDRV_TUYA_HEADER_LEN], frame->data, frame->len);
	}
	out[total - 1U] = sum8(out, total - 1U);

	return (int)total;
}

void iotdrv_tuya_parser_init(struct iotdrv_tuya_parser *p)
{
	p->pos = 0U;
}

int iotdrv_tuya_parser_feed(struct iotdrv_tuya_parser *p, uint8_t byte, struct iotdrv_tuya_frame *out)
{
	uint16_t len;

	if (p->pos == 0U) {
		if (byte == IOTDRV_TUYA_START1) {
			p->buf[p->pos++] = byte;
		}
		return 0;
	}

	if (p->pos == 1U) {
		if (byte == IOTDRV_TUYA_START2) {
			p->buf[p->pos++] = byte;
		} else if (byte != IOTDRV_TUYA_START1) {
			p->pos = 0U;
		}
		return 0;
	}

	p->buf[p->pos++] = byte;
	if (p->pos < IOTDRV_TUYA_HEADER_LEN) {
		return 0;
	}

	len = ((uint16_t)p->buf[6] << 8) | p->buf[7];
	if (len > IOTDRV_TUYA_MAX_RX_DATA) {
		p->pos = 0U;
		return -EBADMSG;
	}

	if (p->pos < (IOTDRV_TUYA_HEADER_LEN + len + 1U)) {
		return 0;
	}

	p->pos = 0U;
	if (sum8(p->buf, IOTDRV_TUYA_HEADER_LEN + len) != p->buf[IOTDRV_TUYA_HEADER_LEN + len]) {
		return -EBADMSG;
	}

	out->version = p->buf[2];
	out->seq = ((uint16_t)p->buf[3] << 8) | p->buf[4];
	out->cmd = p->buf[5];
	out->len = len;
	out->data = &p->buf[IOTDRV_TUYA_HEADER_LEN];

	return 1;
}

int iotdrv_tuya_dp_next(const uint8_t *buf, size_t len, size_t *off, struct iotdrv_tuya_dp *dp)
{
	size_t pos = *off;

	if (pos >= len) {
		return -ENOENT;
	}

	if ((len - pos) < 4U) {
		return -EBADMSG;
	}

	dp->id = buf[pos];
	dp->type = buf[pos + 1U];
	dp->len = ((uint16_t)buf[pos + 2U] << 8) | buf[pos + 3U];
	if ((len - pos - 4U) < dp->len) {
		return -EBADMSG;
	}

	dp->data = &buf[pos + 4U];
	*off = pos + 4U + dp->len;

	return 0;
}

int iotdrv_tuya_dp_encode(uint8_t *out, size_t out_size, uint8_t id, enum iotdrv_tuya_dp_type type,
		      const uint8_t *value, size_t len)
{
	if ((out_size < (4U + len)) || (len > UINT16_MAX)) {
		return -ENOMEM;
	}

	out[0] = id;
	out[1] = (uint8_t)type;
	out[2] = (uint8_t)(len >> 8);
	out[3] = (uint8_t)len;
	if (len > 0U) {
		memcpy(&out[4], value, len);
	}

	return (int)(4U + len);
}

int iotdrv_tuya_dp_encode_u32(uint8_t *out, size_t out_size, uint8_t id, enum iotdrv_tuya_dp_type type,
			  uint32_t value)
{
	uint8_t v[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
			(uint8_t)value};
	size_t len = ((type == IOTDRV_TUYA_DP_BOOL) || (type == IOTDRV_TUYA_DP_ENUM)) ? 1U : 4U;

	return iotdrv_tuya_dp_encode(out, out_size, id, type, &v[4U - len], len);
}

uint32_t iotdrv_tuya_dp_u32(const struct iotdrv_tuya_dp *dp)
{
	uint32_t v = 0U;

	for (size_t i = 0; (i < dp->len) && (i < 4U); i++) {
		v = (v << 8) | dp->data[i];
	}

	return v;
}
