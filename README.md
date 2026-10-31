zephyr-iot-drivers
===================

A Zephyr module with device libraries that do not depend on the radio, and
bridges that expose them over Zigbee or BLE.

    lib/thermostat_core/        setpoint, hysteresis, heating demand
    lib/tuya_mcu/               Tuya MCU: tuya_proto (codec), tuya_mcu (session engine,
                                listeners), tuya_uart (UART transport)
    drivers/display/bu9792/     BU9792AFUV segment LCD controller (I2C)
    drivers/display/lcd_b14/    LYWSD03MMC B1.4 segment LCD controller (I2C)
    drivers/display/lywsd03mmc/ LYWSD03MMC panel layout on top of the controllers
    drivers/sensor/             temperature sensors (Zephyr sensor API)
    bridge/zigbee/              libraries -> ZCL clusters
    bridge/ble/                 libraries -> GATT services
    dts/bindings/               devicetree bindings (vendor prefix `iotdrv`)
    samples/

Each part has its own `CONFIG_IOTDRV_*` option. Libraries and drivers must not
include Zigbee or BLE headers; only `bridge/` does.

Use it from a west workspace by adding it to `west.yml`, or with
`-DZEPHYR_EXTRA_MODULES=/path/to/zephyr-iot-drivers`.
