/* SPDX-License-Identifier: Apache-2.0 */

/* Counts a temperature up on the LYWSD03MMC panel on the board's i2c0. */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <iot_drivers/lywsd03mmc.h>

LOG_MODULE_REGISTER(lcd_demo, LOG_LEVEL_INF);

static struct iotdrv_lywsd03mmc lcd;

int main(void)
{
	const struct device *i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
	enum iotdrv_lywsd03mmc_rev rev;
	int16_t t = -100;
	int err = iotdrv_lywsd03mmc_detect(i2c, &rev);

	if (err != 0) {
		LOG_ERR("no panel found: %d", err);
		return 0;
	}

	err = iotdrv_lywsd03mmc_init(&lcd, i2c, rev);
	if (err != 0) {
		LOG_ERR("init failed: %d", err);
		return 0;
	}

	while (true) {
		iotdrv_lywsd03mmc_big_number_x10(&lcd.frame, t);
		iotdrv_lywsd03mmc_small_number(&lcd.frame, (int16_t)(50 + t / 100), true);
		iotdrv_lywsd03mmc_unit(&lcd.frame, IOTDRV_LYWSD03MMC_UNIT_C);
		iotdrv_lywsd03mmc_smiley(&lcd.frame, (t >= 200) ? IOTDRV_LYWSD03MMC_SMILEY_HAPPY
							     : IOTDRV_LYWSD03MMC_SMILEY_SAD);
		(void)iotdrv_lywsd03mmc_flush(&lcd);

		t = (t > 350) ? -100 : (int16_t)(t + 5);
		k_msleep(500);
	}

	return 0;
}
