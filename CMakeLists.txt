# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(keypad)

zephyr_include_directories(include)

target_sources(app PRIVATE
  src/main.c
  src/app/encoder.c
  src/app/power_mgr.c
  src/app/mode.c
  src/app/keyboard_core.c
  src/app/keymap.c
  src/hid_scheduler.c
  src/usb_hid_transport.c
  src/ble_hid_service.c
  src/events/battery_event.c
  src/events/encoder_event.c
  src/events/mode_event.c
  src/events/hid_key_event.c
  src/events/hid_consumer_event.c
  src/events/hid_led_event.c
  src/events/hid_report_to_send_event.c
  src/events/hid_report_sent_event.c
  src/events/hid_channel_ready_event.c
  src/events/set_protocol_event.c
  src/events/bitmap_cfg_event.c
  src/events/funckey_report_event.c
  drivers/ip5306/ip5306.c
)
