/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <iot_drivers/bl0942_proto.h>

static uint32_t read_u24(const uint8_t *b)
{
	return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
}

static int32_t read_i24(const uint8_t *b)
{
	uint32_t v = read_u24(b);

	return (int32_t)((v & 0x800000U) ? (v | 0xFF000000U) : v);
}

int bl0942_full_packet_request(uint8_t address, uint8_t out[2])
{
	if (address > 3) {
		return -EINVAL;
	}
	out[0] = BL0942_CMD_READ | address;
	out[1] = BL0942_REG_FULL_PKT;
	return 0;
}

uint8_t bl0942_frame_checksum(uint8_t address, const uint8_t *bytes, size_t len)
{
	uint8_t sum = BL0942_CMD_READ | address;

	for (size_t i = 0; i < len; i++) {
		sum = (uint8_t)(sum + bytes[i]);
	}
	return (uint8_t)~sum;
}

int bl0942_frame_parse(const uint8_t bytes[BL0942_FRAME_LEN], uint8_t address,
		       struct bl0942_raw *out)
{
	if (bytes[0] != BL0942_HEADER) {
		return -EPROTO;
	}
	if (bl0942_frame_checksum(address, bytes, BL0942_FRAME_LEN - 1) !=
	    bytes[BL0942_FRAME_LEN - 1]) {
		return -EBADMSG;
	}

	out->current_rms = read_u24(&bytes[1]);
	out->voltage_rms = read_u24(&bytes[4]);
	out->current_fast_rms = read_u24(&bytes[7]);
	out->active_power = read_i24(&bytes[10]);
	out->energy_counter = read_u24(&bytes[13]);
	out->frequency_period = (uint16_t)(bytes[16] | (bytes[17] << 8));
	out->status = bytes[19];
	return 0;
}

static uint32_t scale_u(uint32_t value, uint32_t divisor)
{
	uint64_t r = (uint64_t)value * 1000U / divisor;

	return r > UINT32_MAX ? UINT32_MAX : (uint32_t)r;
}

void bl0942_convert(const struct bl0942_raw *raw, const struct bl0942_calib *calib,
		    struct bl0942_meas *out)
{
	int64_t p = (int64_t)raw->active_power * 1000 / (int64_t)calib->power_counts_per_watt;

	out->voltage_mv = scale_u(raw->voltage_rms, calib->voltage_counts_per_volt);
	out->current_ma = scale_u(raw->current_rms, calib->current_counts_per_amp);
	out->power_mw = p > INT32_MAX ? INT32_MAX : (p < INT32_MIN ? INT32_MIN : (int32_t)p);
	out->frequency_mhz = raw->frequency_period ? 1000000000UL / raw->frequency_period : 0;
}

void bl0942_parser_init(struct bl0942_parser *p, uint8_t address)
{
	p->address = address;
	p->len = 0;
}

static void parser_resync(struct bl0942_parser *p)
{
	for (uint8_t i = 1; i < BL0942_FRAME_LEN; i++) {
		if (p->buf[i] == BL0942_HEADER) {
			p->len = BL0942_FRAME_LEN - i;
			memmove(p->buf, &p->buf[i], p->len);
			return;
		}
	}
	p->len = 0;
}

int bl0942_parser_push(struct bl0942_parser *p, uint8_t byte, struct bl0942_raw *out)
{
	int ret;

	if (p->len == 0) {
		if (byte == BL0942_HEADER) {
			p->buf[p->len++] = byte;
		}
		return 0;
	}

	p->buf[p->len++] = byte;
	if (p->len != BL0942_FRAME_LEN) {
		return 0;
	}

	ret = bl0942_frame_parse(p->buf, p->address, out);
	if (ret == 0) {
		p->len = 0;
		return 1;
	}
	parser_resync(p);
	return ret;
}

void bl0942_energy_init(struct bl0942_energy *e, uint32_t counts_per_kwh)
{
	memset(e, 0, sizeof(*e));
	e->counts_per_kwh = counts_per_kwh;
}

static void energy_add(struct bl0942_energy *e, uint32_t counts)
{
	uint64_t num = e->remainder + (uint64_t)counts * 1000000000ULL;

	e->total_uwh += num / e->counts_per_kwh;
	e->remainder = num % e->counts_per_kwh;
}

enum bl0942_counter_event bl0942_energy_observe(struct bl0942_energy *e, uint32_t counter)
{
	uint32_t prev;

	counter &= BL0942_COUNTER_MOD - 1;
	if (!e->have_previous) {
		e->have_previous = true;
		e->previous = counter;
		return BL0942_COUNTER_FIRST;
	}
	prev = e->previous;
	e->previous = counter;

	if (counter >= prev) {
		energy_add(e, counter - prev);
		return BL0942_COUNTER_ADVANCED;
	}
	/* A drop from near the top to near zero is a wrap, anything else a restart */
	if (prev >= 0xF00000U && counter <= 0x0FFFFFU) {
		energy_add(e, (uint32_t)(BL0942_COUNTER_MOD - prev + counter));
		return BL0942_COUNTER_WRAPPED;
	}
	return BL0942_COUNTER_RESET;
}

void bl0942_energy_restore(struct bl0942_energy *e, uint64_t total_uwh)
{
	e->have_previous = false;
	e->remainder = 0;
	e->total_uwh = total_uwh;
}
