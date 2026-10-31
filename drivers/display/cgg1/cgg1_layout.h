/* SPDX-License-Identifier: Apache-2.0 */

/*
 * CGG1-M segment layout: edits a frame of segment bits and touches no
 * hardware. The segment maps and number rendering follow the ATC firmware.
 */

#ifndef IOTDRV_CGG1_LAYOUT_H_
#define IOTDRV_CGG1_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#define IOTDRV_CGG1_FRAME_MAX 18U

enum iotdrv_cgg1_variant {
	IOTDRV_CGG1_VARIANT_CGG1,      /* 2020, 2021: 18 bytes */
	IOTDRV_CGG1_VARIANT_CGG1_2022, /* 2022: 16 bytes */
};

/* Temperature unit symbol bits as ATC passes them to show_temp_symbol() */
enum iotdrv_cgg1_unit {
	IOTDRV_CGG1_UNIT_NONE = 0x00,
	IOTDRV_CGG1_UNIT_F = 0x60,
	IOTDRV_CGG1_UNIT_C = 0xA0,
};

struct iotdrv_cgg1_frame {
	uint8_t b[IOTDRV_CGG1_FRAME_MAX];
	uint8_t size;
	enum iotdrv_cgg1_variant variant;
};

void iotdrv_cgg1_layout_init(struct iotdrv_cgg1_frame *f, enum iotdrv_cgg1_variant variant);
void iotdrv_cgg1_layout_clear(struct iotdrv_cgg1_frame *f);

/* -995..19995 in 0.1; out of range shows Lo or Hi */
void iotdrv_cgg1_layout_big_number_x10(struct iotdrv_cgg1_frame *f, int16_t value);

/* -995..9995 in 0.1, shown as -9.9..99.9 or -99..999; out of range shows Lo or Hi */
void iotdrv_cgg1_layout_small_number_x10(struct iotdrv_cgg1_frame *f, int16_t value, bool percent);

/* -9..99, anything else shows Lo or Hi */
void iotdrv_cgg1_layout_small_number(struct iotdrv_cgg1_frame *f, int16_t value, bool percent);

void iotdrv_cgg1_layout_unit(struct iotdrv_cgg1_frame *f, enum iotdrv_cgg1_unit unit);
void iotdrv_cgg1_layout_smiley(struct iotdrv_cgg1_frame *f, bool on);
void iotdrv_cgg1_layout_ble(struct iotdrv_cgg1_frame *f, bool on);

/* A negative level hides the symbol, otherwise the gauge shows 0 to 5 bars */
void iotdrv_cgg1_layout_battery(struct iotdrv_cgg1_frame *f, int8_t percent);
void iotdrv_cgg1_layout_clock(struct iotdrv_cgg1_frame *f, uint8_t hours, uint8_t minutes);

#endif /* IOTDRV_CGG1_LAYOUT_H_ */
