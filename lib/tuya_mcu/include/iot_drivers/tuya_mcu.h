/* SPDX-License-Identifier: Apache-2.0 */

#ifndef IOTDRV_TUYA_MCU_H_
#define IOTDRV_TUYA_MCU_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/* Tuya MCU frame: 55 AA, version, command, length (big endian), data, checksum. */
#define IOTDRV_TUYA_HEADER_LEN 6U
#define IOTDRV_TUYA_MAX_DATA   64U

/*
 * Build a frame into @p out and return its length, or a negative errno if
 * the data does not fit.
 */
int iotdrv_tuya_frame_build(uint8_t *out, size_t out_size, uint8_t version, uint8_t cmd,
			const uint8_t *data, size_t len);

/* Attach the protocol to @p uart. */
int iotdrv_tuya_mcu_init(const struct device *uart);

#endif /* IOTDRV_TUYA_MCU_H_ */
