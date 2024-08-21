# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
   set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(PICO_SDK_PATH ${USERHOME}/.pico-sdk/sdk/2.0.0)
set(PICO_TOOLCHAIN_PATH ${USERHOME}/.pico-sdk/toolchain/13_2_Rel1)
set(pioasm_HINT ${USERHOME}/.pico-sdk/tools/2.0.0/pioasm)
if(EXISTS ${pioasm_HINT})
    set(pioasm_DIR ${pioasm_HINT})
endif()
set(picotool_HINT ${USERHOME}/.pico-sdk/picotool/2.0.0/picotool)
if(EXISTS ${picotool_HINT})
    set(picotool_DIR ${picotool_HINT})
endif()
if(PICO_TOOLCHAIN_PATH MATCHES "RISCV")
    set(PICO_PLATFORM rp2350-riscv CACHE STRING "Pico Platform")
    if(PICO_TOOLCHAIN_PATH MATCHES "COREV")
        set(PICO_COMPILER pico_riscv_gcc_zcb_zcmp)
    endif()
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(pico-cv-recorder C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(pico-cv-recorder pico-cv-recorder.cpp )

pico_set_program_name(pico-cv-recorder "pico-cv-recorder")
pico_set_program_version(pico-cv-recorder "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(pico-cv-recorder 0)
pico_enable_stdio_usb(pico-cv-recorder 1)

# Add the standard library to the build
target_link_libraries(pico-cv-recorder
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(pico-cv-recorder PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

# Add any user requested libraries
target_link_libraries(pico-cv-recorder 
        hardware_adc
        hardware_i2c
        )

pico_add_extra_outputs(pico-cv-recorder)

