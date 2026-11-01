# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tuya_thermostat_test)

target_sources(app PRIVATE
  src/main.c
  ../../lib/tuya_thermostat/tuya_thermostat.c
  ../../lib/tuya_thermostat/tt_schedule.c
  ../../lib/tuya_thermostat/tt_models.c
  ../../lib/tuya_mcu/tuya_proto.c
)
target_include_directories(app PRIVATE
  ../../lib/tuya_thermostat/include
  ../../lib/tuya_mcu/include
)
