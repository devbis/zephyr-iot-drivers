# SPDX-License-Identifier: Apache-2.0

if(CONFIG_IOTDRV_DISPLAY_MHO_C122)
  zephyr_library()
  zephyr_library_sources(layout.c mho_c122.c)
endif()
