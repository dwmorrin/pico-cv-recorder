cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(i2c_dac)

pico_sdk_init()

add_executable(dac
  dac.c
)

pico_enable_stdio_usb(dac 1)

target_link_libraries(dac pico_stdlib hardware_i2c)

pico_add_extra_outputs(dac)
