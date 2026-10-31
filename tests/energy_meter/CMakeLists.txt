# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(energy_meter_test)

target_sources(app PRIVATE
  src/main.c
  ../../drivers/sensor/bl0942/proto.c
  ../../drivers/sensor/bl0937/calc.c
)
target_include_directories(app PRIVATE ../../drivers/sensor/include)
