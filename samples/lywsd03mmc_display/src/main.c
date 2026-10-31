/* SPDX-License-Identifier: Apache-2.0 */

/* Counts a temperature up on the LYWSD03MMC panel. */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/lywsd03mmc.h>

LOG_MODULE_REGISTER(lcd_demo, LOG_LEVEL_INF);

int main(void)
{
	const struct device *lcd = DEVICE_DT_GET(DT_NODELABEL(lcd));
	int16_t t = -100;

	if (!device_is_ready(lcd)) {
		LOG_ERR("panel not ready");
		return 0;
	}

	while (true) {
		iotdrv_lywsd03mmc_big_number_x10(lcd, t);
		iotdrv_lywsd03mmc_small_number(lcd, (int16_t)(50 + t / 100), true);
		iotdrv_lywsd03mmc_unit(lcd, IOTDRV_LYWSD03MMC_UNIT_C);
		iotdrv_lywsd03mmc_smiley(lcd, (t >= 200) ? IOTDRV_LYWSD03MMC_SMILEY_HAPPY
							     : IOTDRV_LYWSD03MMC_SMILEY_SAD);
		(void)iotdrv_lywsd03mmc_flush(lcd);

		t = (t > 350) ? -100 : (int16_t)(t + 5);
		k_msleep(500);
	}

	return 0;
}
