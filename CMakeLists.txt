# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky)

target_sources(app PRIVATE
  src/main.c
  src/app/matrix.c
  src/app/encoder.c
  src/app/encoder_event.c
  src/app/keymap.c
)
