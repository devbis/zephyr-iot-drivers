# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_DISPLAY_LYWSD03MMC)
  zephyr_library()
  zephyr_library_sources(layout.c lywsd03mmc.c)
endif()
