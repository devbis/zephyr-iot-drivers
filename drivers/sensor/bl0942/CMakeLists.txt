# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_SENSOR_BL0942)
  zephyr_library()
  zephyr_library_sources(proto.c bl0942.c)
endif()
