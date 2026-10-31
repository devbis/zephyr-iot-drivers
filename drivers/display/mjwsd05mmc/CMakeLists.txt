# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_DISPLAY_MJWSD05MMC)
  zephyr_library()
  zephyr_library_sources(layout.c mjwsd05mmc.c)
endif()
