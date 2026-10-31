# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_DISPLAY_CGG1)
  zephyr_library()
  zephyr_library_sources(layout.c cgg1.c)
endif()
