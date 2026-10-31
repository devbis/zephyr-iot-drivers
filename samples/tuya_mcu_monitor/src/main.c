/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Logs everything a Tuya MCU reports. Shows the intended wiring: the engine
 * is bound to a transport on one side and to listeners on the other, and the
 * two never see each other.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/tuya_mcu.h>
#include <iot_drivers/tuya_uart.h>

LOG_MODULE_REGISTER(tuya_monitor, LOG_LEVEL_INF);

static struct iotdrv_tuya_mcu mcu;

static void on_ready(struct iotdrv_tuya_mcu *m, const char *product, void *user_data)
{
	LOG_INF("MCU ready: %s", product);
	(void)iotdrv_tuya_mcu_set_net_status(m, IOTDRV_TUYA_NET_JOINED);
	(void)iotdrv_tuya_mcu_query_dps(m);
}

static void on_dp(struct iotdrv_tuya_mcu *m, const struct iotdrv_tuya_dp *dp, void *user_data)
{
	if (dp->type == IOTDRV_TUYA_DP_RAW || dp->type == IOTDRV_TUYA_DP_STRING) {
		LOG_HEXDUMP_INF(dp->data, dp->len, "dp raw");
		LOG_INF("dp %u type %u len %u", dp->id, dp->type, dp->len);
	} else {
		LOG_INF("dp %u type %u = %u", dp->id, dp->type, iotdrv_tuya_dp_u32(dp));
	}
}

static void on_link_lost(struct iotdrv_tuya_mcu *m, void *user_data)
{
	LOG_WRN("MCU link lost");
}

static struct iotdrv_tuya_listener listener = {
	.on_ready = on_ready,
	.on_dp = on_dp,
	.on_link_lost = on_link_lost,
};

int main(void)
{
	int err;

	iotdrv_tuya_mcu_init(&mcu, 0U);
	iotdrv_tuya_mcu_add_listener(&mcu, &listener);

	err = iotdrv_tuya_uart_bind(DEVICE_DT_GET(DT_ALIAS(tuya_uart)), &mcu);
	if (err != 0) {
		LOG_ERR("UART bind failed: %d", err);
		return 0;
	}

	return iotdrv_tuya_mcu_start(&mcu);
}
