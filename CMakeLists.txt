cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(cv_recorder)

pico_sdk_init()

add_executable(cv_recorder
  cv_recorder.c
)

pico_enable_stdio_usb(cv_recorder 1)

target_link_libraries(cv_recorder
  pico_stdlib
  pico_time
  hardware_adc
  hardware_i2c
)

pico_add_extra_outputs(cv_recorder)
