/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <iot_drivers/tuya_mcu.h>

int iotdrv_tuya_frame_build(uint8_t *out, size_t out_size, uint8_t version, uint8_t cmd,
			const uint8_t *data, size_t len)
{
	uint8_t sum = 0U;
	size_t total = IOTDRV_TUYA_HEADER_LEN + len + 1U;

	if ((len > IOTDRV_TUYA_MAX_DATA) || (out_size < total)) {
		return -ENOMEM;
	}

	out[0] = 0x55U;
	out[1] = 0xAAU;
	out[2] = version;
	out[3] = cmd;
	out[4] = (uint8_t)(len >> 8);
	out[5] = (uint8_t)len;
	for (size_t i = 0; i < len; i++) {
		out[IOTDRV_TUYA_HEADER_LEN + i] = data[i];
	}

	for (size_t i = 0; i < total - 1U; i++) {
		sum += out[i];
	}
	out[total - 1U] = sum;

	return (int)total;
}

int iotdrv_tuya_mcu_init(const struct device *uart)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	return 0;
}
