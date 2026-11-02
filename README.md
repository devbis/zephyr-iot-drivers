zephyr-iot-drivers
===================

A Zephyr module with device libraries that do not depend on the radio, and
bridges that expose them over Zigbee or BLE.

    lib/thermostat_core/        setpoint, hysteresis, heating demand
    lib/tuya_mcu/               Tuya MCU: tuya_proto (codec), tuya_mcu (session engine,
                                listeners), tuya_uart (UART transport)
    lib/tuya_thermostat/        the 13 Tuya floor-heating thermostats: datapoint maps, value and
                                schedule codecs (host-testable)
    drivers/display/            thermometer panels behind one API (iot_drivers/climate_display.h):
      lywsd03mmc/ cgdk2/ mho_c122/ mjwsd05mmc/   segment LCDs
      cgg1/ mho_c401/                            e-paper (variant chosen in devicetree)
    drivers/sensor/             energy meters on the Zephyr sensor API (iot_drivers/energy_meter.h):
      bl0942/                                  over UART
      bl0937/                                  CF/CF1 pulse counting
    drivers/sensor/cht8305/     CHT8305 temperature and humidity (Zephyr sensor API, I2C)
    bridge/zigbee/              libraries -> ZCL clusters
    bridge/ble/                 libraries -> GATT services
    dts/bindings/               devicetree bindings (vendor prefixes `iotdrv`, `belling`, `tuya`)
    samples/

Each part has its own `CONFIG_IOTDRV_*` option. Libraries and drivers must not
include Zigbee or BLE headers; only `bridge/` does.

Use it from a west workspace by adding it to `west.yml`, or with
`-DZEPHYR_EXTRA_MODULES=/path/to/zephyr-iot-drivers`.
