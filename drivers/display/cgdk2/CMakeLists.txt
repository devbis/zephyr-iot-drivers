# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_DISPLAY_CGDK2)
  zephyr_library()
  zephyr_library_sources(layout.c cgdk2.c)
endif()
