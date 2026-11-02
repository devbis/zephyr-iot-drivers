# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_SENSOR_CHT8305)
  zephyr_library()
  zephyr_library_sources(cht8305.c)
endif()
