/* SPDX-License-Identifier: Apache-2.0 */

#ifndef IOTDRV_TUYA_UART_H_
#define IOTDRV_TUYA_UART_H_

#include <zephyr/device.h>

#include <iot_drivers/tuya_mcu.h>

/* Connect @p uart to @p mcu as its transport. The UART must already be configured. */
int iotdrv_tuya_uart_bind(const struct device *uart, struct iotdrv_tuya_mcu *mcu);

#endif /* IOTDRV_TUYA_UART_H_ */
