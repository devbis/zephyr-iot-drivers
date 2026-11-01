# tuya_thermostat

The datapoint maps of the 13 floor-heating thermostats of
[tuya_thermostat_zrd](https://github.com/slacky1965/tuya_thermostat_zrd) (r01..r0D) and the
conversions between a Tuya datapoint and the value of the Zigbee Thermostat cluster, schedules
included. Plain C, no Zephyr or Zigbee headers: it is tested on the host.

    tt_models.c        the tables, generated: tools/gen_tt_models.py <tuya_thermostat_zrd>/src
    tuya_thermostat.c  model by product id, field by datapoint id, value <-> datapoint
    tt_schedule.c      weekly schedule <-> raw datapoints, four families of models

A model is found from the product id in the product information the MCU sends
(`iotdrv_tt_product_from_info()`, `iotdrv_tt_model_by_product()`); an unknown id is the caller's to
treat (the original falls back to r01). The caller keeps the state and the Zigbee attributes and
maps their ids to the logical fields (`enum iotdrv_tt_field`).

## What was taken from the original, and how it was checked

The tables come from the `init_datapoint_modelN()` functions by script, nothing is typed in. The
conversions were read from the handlers of the original (`remote_cmd_*` Zigbee -> MCU,
`local_cmd_*` MCU -> Zigbee) and are tested with values worked out by hand from that code; the
original itself was not run.

| Fields | Conversion |
|---|---|
| setpoint, temperature, outdoor temperature, eco temperatures | the device counts degrees (divisor 1) or tenths (10); Zigbee has 0.01 degrees |
| minimum, maximum, heat protection, frost protection | the same, but written through whole degrees (the fraction is lost) |
| dead band | degrees; divisor 10 or 100 scales it |
| calibration, external calibration | Zigbee tenths; divisor -10: the device counts degrees |
| humidity, humidity offset | percent; the offset is written through whole percents |
| operation mode | r02 is inverted, r0D has its own numbering (0, 3, 1, 4), the others pass through |
| level | r0D keeps a brightness percent (0, 5, 25, 100), the Zigbee side has the level 0..3; the highest writable level is 8 on r03, 9 on r04, 3 on r06, 2 on r0B |
| frost protection | a switch on r06, a temperature on the others |
| range | `min`/`max` of the table limit the value before it is written |

Schedules (`tt_schedule.c`):

| Models | Format |
|---|---|
| r01 | one raw datapoint, 36 bytes: Mon, Sat, Sun, 4 transitions each of hour, minute, half degrees |
| r0A, r0B | one raw datapoint, 48 bytes: the same groups, hour, minute, u16 (whole degrees on r0A, tenths on r0B) |
| r02 r03 r04 r05 r07 r09 r0C r0D | a raw datapoint per day, 17 bytes: 1, then 4 transitions of hour, minute, u16 in tenths |
| r06 | one raw datapoint, 32 bytes: 6 transitions of the weekdays, 2 of the weekend, tenths |

## Not done yet

- **r08's schedule.** It is a conversation over several datapoints (mode, day, hour, minute,
  temperature, period) driven by timers, not a codec: `iotdrv_tt_schedule_*()` return `-ENOTSUP`.
  Its table has the same datapoint id (0x6d) for the raw schedule and the period, and
  `iotdrv_tt_find_field()` returns the first one. `iotdrv_tt_schedule_from_dp()` answers `-ENOTSUP`
  only for the four event datapoints; every other id of the model is `-ENOENT`, a plain field.
- **Fields passed through without reading their handlers:** eco mode, sound, inversion, thermostat
  mode, fan mode and control, mode lock, screen off time, LED indicator, settings reset, the second
  level, the level of r0C, the run state. They are taken for plain bool/enum/value datapoints
  (what the tables say), which the original may refine.
- **Session quirks of the original:** the command it answers an MCU report with differs per model
  (r0C, two products of r06), and r01's `aoclfnxz` and r02 are not paced by 50 ms between commands.
  `lib/tuya_mcu` has one policy for all.
- Settings storage, the Zigbee side (attributes, commands, reporting) and the mapping of the
  system mode and of the run state to the attributes are the application's.

Inherited on purpose: the temperatures of the per-day and the whole-degree formats lose their
fraction, and the hours and minutes an MCU sends are not checked.
