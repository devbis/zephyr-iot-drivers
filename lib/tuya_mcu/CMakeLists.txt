# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(tuya_proto.c tuya_mcu.c)
zephyr_library_sources_ifdef(CONFIG_IOTDRV_TUYA_UART tuya_uart.c)
