/* SPDX-License-Identifier: Apache-2.0 */

/*
 * BL0942 UART protocol and fixed-point conversion. No Zephyr dependencies.
 * The caller writes the request bytes and feeds the received bytes into the
 * stream parser.
 */

#ifndef IOTDRV_BL0942_PROTO_H_
#define IOTDRV_BL0942_PROTO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BL0942_CMD_READ     0x58U
#define BL0942_CMD_WRITE    0xA8U
#define BL0942_REG_FULL_PKT 0xAAU
#define BL0942_HEADER       0x55U
#define BL0942_FRAME_LEN    23U
#define BL0942_COUNTER_MOD  (1UL << 24)

struct bl0942_raw {
	uint32_t current_rms;
	uint32_t voltage_rms;
	uint32_t current_fast_rms;
	int32_t active_power;
	uint32_t energy_counter;
	uint16_t frequency_period;
	uint8_t status;
};

/* Raw counts per physical unit. None may be zero. */
struct bl0942_calib {
	uint32_t voltage_counts_per_volt;
	uint32_t current_counts_per_amp;
	uint32_t power_counts_per_watt;
	uint32_t energy_counts_per_kwh;
};

struct bl0942_meas {
	uint32_t voltage_mv;
	uint32_t current_ma;
	int32_t power_mw;
	uint32_t frequency_mhz; /* 0 when the chip reports no period */
};

/* Request for the full packet. address is 0..3. */
int bl0942_full_packet_request(uint8_t address, uint8_t out[2]);

uint8_t bl0942_frame_checksum(uint8_t address, const uint8_t *bytes, size_t len);

/* -EPROTO: bad header, -EBADMSG: bad checksum */
int bl0942_frame_parse(const uint8_t bytes[BL0942_FRAME_LEN], uint8_t address,
		       struct bl0942_raw *out);

void bl0942_convert(const struct bl0942_raw *raw, const struct bl0942_calib *calib,
		    struct bl0942_meas *out);

struct bl0942_parser {
	uint8_t address;
	uint8_t len;
	uint8_t buf[BL0942_FRAME_LEN];
};

void bl0942_parser_init(struct bl0942_parser *p, uint8_t address);

/*
 * Returns 1 when a frame was completed (stored in *out), 0 while more bytes
 * are needed, or a negative error from bl0942_frame_parse() after which the
 * parser has already resynchronized on the next header byte.
 */
int bl0942_parser_push(struct bl0942_parser *p, uint8_t byte, struct bl0942_raw *out);

enum bl0942_counter_event {
	BL0942_COUNTER_FIRST,
	BL0942_COUNTER_ADVANCED,
	BL0942_COUNTER_WRAPPED,
	BL0942_COUNTER_RESET, /* the chip restarted: counts since the last sample are lost */
};

/* Monotonic energy from the wrapping 24-bit chip counter */
struct bl0942_energy {
	uint32_t counts_per_kwh;
	uint32_t previous;
	bool have_previous;
	uint64_t remainder;
	uint64_t total_uwh;
};

void bl0942_energy_init(struct bl0942_energy *e, uint32_t counts_per_kwh);
enum bl0942_counter_event bl0942_energy_observe(struct bl0942_energy *e, uint32_t counter);
/* Sets the total and re-baselines on the next sample */
void bl0942_energy_restore(struct bl0942_energy *e, uint64_t total_uwh);

#endif /* IOTDRV_BL0942_PROTO_H_ */
