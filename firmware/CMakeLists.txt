cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Forgix uses the RP2350A/RP2354A device")
set(PICO_FLASH_SIZE_BYTES 2097152 CACHE STRING "RP2354 integrated flash size")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(forgix_hello_world C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

if(NOT FPGA_IMAGE)
  message(FATAL_ERROR "Set FPGA_IMAGE to the generated passive-SPI binary")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(GENERATED_C ${GENERATED_DIR}/fpga_image.c)
set(GENERATED_H ${GENERATED_DIR}/fpga_image.h)
add_custom_command(
  OUTPUT ${GENERATED_C} ${GENERATED_H}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../scripts/embed_image.py
          ${FPGA_IMAGE} ${GENERATED_C} ${GENERATED_H}
  DEPENDS ${FPGA_IMAGE} ${CMAKE_CURRENT_LIST_DIR}/../scripts/embed_image.py
  VERBATIM)

# When enabled, TinyUSB is serviced from the foreground loop through
# BSP_UsbService() and the SDK's background IRQ task is switched off. Never
# enable one without the other: two owners of tud_task() corrupt stack state.
option(FORGIX_FOREGROUND_USB_SERVICE
  "Service TinyUSB from the foreground loop instead of the SDK background task" OFF)

# Reloading the bitstream drives CRESET_N and rewrites 173 KB on every failing
# sample, which is itself a disturbance. Off by default so a run establishes what
# the fault does when left alone; turn it on for the recovery comparison.
option(FORGIX_FPGA_AUTO_RECONFIGURE
  "Attempt FPGA reconfiguration after a runtime health-check failure" OFF)

# Routes the diagnostics report out of the USB-free image over UART, which needs
# no USB and survives the FPGA dying -- the LED cannot report either.
option(FORGIX_DIAGNOSTIC_UART
  "Emit the USB-free image's diagnostics over UART stdio" OFF)

# GPIO 0 is XIP_CS1n for the DRAM sharing the QSPI bus. When enabled, runtime_init
# gives the pin its chip-select function and sizes the device before main runs;
# when disabled the BSP holds the pin deasserted and the DRAM is unreachable.
# Either way the pad's power-up pull-down is swapped for a pull-up, which is the
# only substitute for the 10K resistor this board has no footprint for.
# ON: the fitted APS1604M-3SQR-SN is sized at 2 MByte by the SDK's own detection
# and passes a write/read pattern across the start, middle and end of its range.
# Note this puts the DRAM back on the bus that the original lockup lived on, so
# it is a configuration that has to earn its own soak rather than inherit one.
option(FORGIX_QSPI_PSRAM
  "Bring up the DRAM on QSPI chip select 1 (GPIO 0)" ON)

add_library(forgix_bsp STATIC
  src/bsp/bsp.c
  src/bsp/bsp_adc.c
  src/bsp/bsp_button.c
  src/bsp/bsp_clocks.c
  src/bsp/bsp_console.c
  src/bsp/bsp_fpga.c
  src/bsp/bsp_led.c
  src/bsp/bsp_mcu.c
  src/bsp/bsp_memory.c
  src/bsp/bsp_time.c
  src/bsp/bsp_watchdog.c
  ${GENERATED_C})
target_include_directories(forgix_bsp
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/bsp
  PRIVATE ${GENERATED_DIR})
# hardware_flash is unconditional: bsp_memory uses it to identify the QSPI
# devices whether or not the chip-select-1 device is brought up.
# hardware_clocks and hardware_adc back the built-in test's read-only view of the
# MCU, and pico_unique_id supplies the board identity it reports.
target_link_libraries(forgix_bsp PUBLIC
  pico_stdlib
  hardware_spi
  hardware_watchdog
  hardware_flash
  hardware_clocks
  hardware_adc
  pico_unique_id)

if(FORGIX_QSPI_PSRAM)
  target_link_libraries(forgix_bsp PUBLIC hardware_psram)
  # The chip select is known, so only the size needs probing. Naming the pin also
  # keeps auto-detection away from the other candidate CS pins -- GPIO 19 is this
  # board's FPGA oscillator enable, and wiggling it would be its own bug.
  target_compile_definitions(forgix_bsp PUBLIC
    FORGIX_QSPI_PSRAM=1
    PICO_PSRAM_CS_PIN=0
    PICO_AUTO_DETECT_PSRAM_SIZE=1)
endif()

# bsp_usb is a separate library because forgix_bsp is shared with the USB-free
# diagnostic image, which links the stub implementation instead.
add_library(forgix_bsp_usb STATIC src/bsp/bsp_usb.c)
target_include_directories(forgix_bsp_usb PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/bsp)
# pico_stdio_usb adds its sources to the consuming target, so it must stay in this
# static library's public link interface to reach the executable. Do not add
# tinyusb_device here: the marked library defines LIB_TINYUSB_DEVICE, which makes
# the SDK's pico_stdio_usb/include/tusb_config.h stand down on the assumption
# that the application supplies its own TinyUSB configuration, and CDC then
# compiles out of the whole image. pico_stdio_usb pulls tinyusb_device_unmarked.
target_link_libraries(forgix_bsp_usb PUBLIC pico_stdlib pico_stdio_usb)

add_library(forgix_bsp_usb_stub STATIC src/bsp/bsp_usb_stub.c)
target_include_directories(forgix_bsp_usb_stub PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/bsp)
target_link_libraries(forgix_bsp_usb_stub PUBLIC pico_stdlib)

add_library(forgix_application STATIC
  src/application/application.c
  src/application/application_console.c
  src/application/application_diagnostics.c
  src/application/application_runner.c)
target_include_directories(forgix_application
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/application)
target_link_libraries(forgix_application PUBLIC forgix_bsp)

add_executable(forgix_hello_world src/main.c)
target_link_libraries(forgix_hello_world PRIVATE forgix_application forgix_bsp_usb)
pico_enable_stdio_usb(forgix_hello_world 1)
pico_enable_stdio_uart(forgix_hello_world 0)
pico_add_extra_outputs(forgix_hello_world)

if(FORGIX_FOREGROUND_USB_SERVICE)
  target_compile_definitions(forgix_bsp_usb PRIVATE FORGIX_FOREGROUND_USB_SERVICE=1)
  target_compile_definitions(forgix_hello_world PRIVATE PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=0)
endif()

if(FORGIX_FPGA_AUTO_RECONFIGURE)
  target_compile_definitions(forgix_bsp PRIVATE FORGIX_FPGA_AUTO_RECONFIGURE=1)
endif()

# USB-free control image: same instrumented diagnostics loop, no USB code.
add_executable(forgix_led_only_diagnostic src/diagnostics/led_only_main.c)
target_link_libraries(forgix_led_only_diagnostic PRIVATE forgix_application forgix_bsp_usb_stub)
pico_enable_stdio_usb(forgix_led_only_diagnostic 0)
if(FORGIX_DIAGNOSTIC_UART)
  # Not GPIO 0/1: GPIO 0 is CE# of the QSPI device on chip select 1 and must stay
  # driven high, and GPIO 1 is the FPGA chip select. UART0 also maps to 12/13.
  pico_enable_stdio_uart(forgix_led_only_diagnostic 1)
  target_compile_definitions(forgix_led_only_diagnostic PRIVATE
    PICO_DEFAULT_UART_TX_PIN=12 PICO_DEFAULT_UART_RX_PIN=13)
else()
  pico_enable_stdio_uart(forgix_led_only_diagnostic 0)
endif()
pico_add_extra_outputs(forgix_led_only_diagnostic)
