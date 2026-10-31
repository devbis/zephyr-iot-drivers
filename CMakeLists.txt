# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(
  lib/thermostat_core/include
  lib/tuya_mcu/include
)

add_subdirectory_ifdef(CONFIG_IOTDRV_THERMOSTAT_CORE lib/thermostat_core)
add_subdirectory_ifdef(CONFIG_IOTDRV_TUYA_MCU lib/tuya_mcu)
add_subdirectory_ifdef(CONFIG_IOTDRV_DISPLAY_LYWSD03MMC drivers/display/lywsd03mmc)
add_subdirectory_ifdef(CONFIG_IOTDRV_BRIDGE_ZIGBEE bridge/zigbee)
add_subdirectory_ifdef(CONFIG_IOTDRV_BRIDGE_BLE bridge/ble)
