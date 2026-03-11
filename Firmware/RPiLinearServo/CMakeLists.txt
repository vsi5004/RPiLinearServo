cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(RPiLinearServo C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ---------------------------------------------------------------------------
# Version from git tag  (fallback: 1.0.0 / "unknown")
# CI can override via -DFW_VERSION_MAJOR=... -DFW_GIT_HASH=...
# ---------------------------------------------------------------------------
if(NOT DEFINED FW_VERSION_MAJOR)
    set(FW_VERSION_MAJOR 1)
    set(FW_VERSION_MINOR 0)
    set(FW_VERSION_PATCH 0)
    set(FW_GIT_HASH "unknown")

    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --abbrev=0
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_TAG_RESULT
        )
        if(GIT_TAG_RESULT EQUAL 0 AND GIT_TAG MATCHES "^v([0-9]+)\\.([0-9]+)\\.([0-9]+)")
            set(FW_VERSION_MAJOR ${CMAKE_MATCH_1})
            set(FW_VERSION_MINOR ${CMAKE_MATCH_2})
            set(FW_VERSION_PATCH ${CMAKE_MATCH_3})
        endif()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE FW_GIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()

message(STATUS "Firmware version: ${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.${FW_VERSION_PATCH} (${FW_GIT_HASH})")

# Executable
add_executable(RPiLinearServo
        src/main.cpp
        drivers/stepgen/stepgen.cpp
        drivers/tmc2209/tmc2209.cpp
        drivers/ws2812/ws2812.cpp
        ui/status_led.cpp
        drivers/pwm_input/pwm_input.cpp
        motion/homing.cpp
        motion/hall_verify.cpp
        motion/servo_loop.cpp
        cli/cli.cpp
        storage/position_store.cpp
        usb/usb_descriptors.cpp
        usb/usb_stdio.cpp
        usb/msc_disk.cpp
        storage/config_ini.cpp
        storage/config_store.cpp
        drivers/hall_sensor/hall_sensor.cpp
        power/dormant.cpp
)

pico_set_program_name(RPiLinearServo "RPiLinearServo")
pico_set_program_version(RPiLinearServo "${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.${FW_VERSION_PATCH}")

target_compile_definitions(RPiLinearServo PRIVATE
        FW_VERSION_MAJOR=${FW_VERSION_MAJOR}
        FW_VERSION_MINOR=${FW_VERSION_MINOR}
        FW_VERSION_PATCH=${FW_VERSION_PATCH}
        FW_GIT_HASH="${FW_GIT_HASH}"
)

# PIO header generation
pico_generate_pio_header(RPiLinearServo
        ${CMAKE_CURRENT_LIST_DIR}/drivers/stepgen/stepgen.pio)
pico_generate_pio_header(RPiLinearServo
        ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812/ws2812.pio)
pico_generate_pio_header(RPiLinearServo
        ${CMAKE_CURRENT_LIST_DIR}/drivers/pwm_input/pwm_capture.pio)

# stdio over USB CDC (handled by our composite TinyUSB layer)
pico_enable_stdio_uart(RPiLinearServo 0)
pico_enable_stdio_usb(RPiLinearServo 0)

# Include paths
target_include_directories(RPiLinearServo PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/drivers/stepgen
        ${CMAKE_CURRENT_LIST_DIR}/drivers/tmc2209
        ${CMAKE_CURRENT_LIST_DIR}/drivers/ws2812
        ${CMAKE_CURRENT_LIST_DIR}/drivers/pwm_input
        ${CMAKE_CURRENT_LIST_DIR}/motion
        ${CMAKE_CURRENT_LIST_DIR}/cli
        ${CMAKE_CURRENT_LIST_DIR}/storage
        ${CMAKE_CURRENT_LIST_DIR}/usb
        ${CMAKE_CURRENT_LIST_DIR}/ui
        ${CMAKE_CURRENT_LIST_DIR}/drivers/hall_sensor
        ${CMAKE_CURRENT_LIST_DIR}/power
)

# Libraries
target_link_libraries(RPiLinearServo
        pico_stdlib
        pico_unique_id
        tinyusb_device
        tinyusb_board
        hardware_pio
        hardware_gpio
        hardware_clocks
        hardware_timer
        hardware_uart
        hardware_flash
        hardware_sync
        hardware_adc
        hardware_xosc
        hardware_pll
)

# Extra outputs (.uf2, .bin, .hex, .map, .dis)
pico_add_extra_outputs(RPiLinearServo)

