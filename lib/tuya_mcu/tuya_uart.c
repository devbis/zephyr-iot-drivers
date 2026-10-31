/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/drivers/uart.h>

#include <iot_drivers/tuya_uart.h>

static int uart_send(void *ctx, const uint8_t *buf, size_t len)
{
	const struct device *uart = ctx;

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart, buf[i]);
	}

	return 0;
}

static void uart_isr(const struct device *uart, void *user_data)
{
	struct iotdrv_tuya_mcu *mcu = user_data;
	uint8_t buf[16];

	(void)uart_irq_update(uart);

	while (uart_irq_rx_ready(uart) > 0) {
		int n = uart_fifo_read(uart, buf, sizeof(buf));

		if (n <= 0) {
			break;
		}
		(void)iotdrv_tuya_mcu_rx(mcu, buf, (size_t)n);
	}
}

int iotdrv_tuya_uart_bind(const struct device *uart, struct iotdrv_tuya_mcu *mcu)
{
	int err;

	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	iotdrv_tuya_mcu_set_transport(mcu, uart_send, (void *)uart);

	err = uart_irq_callback_user_data_set(uart, uart_isr, mcu);
	if (err != 0) {
		return err;
	}
	uart_irq_rx_enable(uart);

	return 0;
}
