/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Segment layout of the MHO-C401 panels, free of hardware access.
 *
 * Both variants keep the display as a bit map sent verbatim to the
 * controller. Segment positions are the ones of the ATC_MiThermometer
 * firmware. The original panel has no "%" segment of its own: its shared
 * point/"%" bit only follows the point of the big number.
 */

#ifndef IOTDRV_MHO_C401_LAYOUT_H_
#define IOTDRV_MHO_C401_LAYOUT_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define IOTDRV_MHO_C401_FRAME_SIZE 18U

enum iotdrv_mho_c401_layout_variant {
	IOTDRV_MHO_C401_LAYOUT_C401,
	IOTDRV_MHO_C401_LAYOUT_C401N,
};

enum iotdrv_mho_c401_layout_unit {
	IOTDRV_MHO_C401_LAYOUT_UNIT_NONE,
	IOTDRV_MHO_C401_LAYOUT_UNIT_C,
	IOTDRV_MHO_C401_LAYOUT_UNIT_F,
};

enum iotdrv_mho_c401_layout_smiley {
	IOTDRV_MHO_C401_LAYOUT_SMILEY_OFF,
	IOTDRV_MHO_C401_LAYOUT_SMILEY_HAPPY,
	IOTDRV_MHO_C401_LAYOUT_SMILEY_SAD,
};

struct iotdrv_mho_c401_frame {
	uint8_t b[IOTDRV_MHO_C401_FRAME_SIZE];
};

/* Bytes of the frame the variant sends: 18 for the C401, 16 for the C401N */
size_t iotdrv_mho_c401_layout_size(enum iotdrv_mho_c401_layout_variant v);

void iotdrv_mho_c401_layout_clear(struct iotdrv_mho_c401_frame *f);
void iotdrv_mho_c401_layout_big_number_x10(struct iotdrv_mho_c401_frame *f,
				       enum iotdrv_mho_c401_layout_variant v, int16_t value);
void iotdrv_mho_c401_layout_small_number(struct iotdrv_mho_c401_frame *f,
				     enum iotdrv_mho_c401_layout_variant v, int16_t value,
				     bool percent);
void iotdrv_mho_c401_layout_unit(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			     enum iotdrv_mho_c401_layout_unit unit);
void iotdrv_mho_c401_layout_smiley(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			       enum iotdrv_mho_c401_layout_smiley smiley);
void iotdrv_mho_c401_layout_ble(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			    bool on);
void iotdrv_mho_c401_layout_battery(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
				bool on);
/* Blanks the whole frame, then draws HH in the top and MM in the bottom digits */
void iotdrv_mho_c401_layout_clock(struct iotdrv_mho_c401_frame *f, enum iotdrv_mho_c401_layout_variant v,
			      uint8_t hours, uint8_t minutes);

#endif /* IOTDRV_MHO_C401_LAYOUT_H_ */
