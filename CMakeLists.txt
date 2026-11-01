# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(
  drivers/display/include
  drivers/sensor/include
  lib/thermostat_core/include
  lib/tuya_mcu/include
  lib/tuya_thermostat/include
)

add_subdirectory_ifdef(CONFIG_IOTDRV_THERMOSTAT_CORE lib/thermostat_core)
add_subdirectory_ifdef(CONFIG_IOTDRV_TUYA_MCU lib/tuya_mcu)
add_subdirectory_ifdef(CONFIG_IOTDRV_TUYA_THERMOSTAT lib/tuya_thermostat)

# Each panel driver is enabled by its devicetree node: see its Kconfig
file(GLOB iotdrv_display_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/drivers/display
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/display/*/CMakeLists.txt)
foreach(iotdrv_display_cmake ${iotdrv_display_dirs})
  get_filename_component(iotdrv_display_dir ${iotdrv_display_cmake} DIRECTORY)
  add_subdirectory(drivers/display/${iotdrv_display_dir})
endforeach()

# Each sensor driver is enabled by its devicetree node: see its Kconfig
file(GLOB iotdrv_sensor_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/drivers/sensor
     ${CMAKE_CURRENT_SOURCE_DIR}/drivers/sensor/*/CMakeLists.txt)
foreach(iotdrv_sensor_cmake ${iotdrv_sensor_dirs})
  get_filename_component(iotdrv_sensor_dir ${iotdrv_sensor_cmake} DIRECTORY)
  add_subdirectory(drivers/sensor/${iotdrv_sensor_dir})
endforeach()

add_subdirectory_ifdef(CONFIG_IOTDRV_BRIDGE_ZIGBEE bridge/zigbee)
add_subdirectory_ifdef(CONFIG_IOTDRV_BRIDGE_BLE bridge/ble)
