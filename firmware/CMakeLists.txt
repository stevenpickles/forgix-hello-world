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
# bsp_usb_service() and the SDK's background IRQ task is switched off. Never
# enable one without the other: two owners of tud_task() corrupt stack state.
option(FORGIX_FOREGROUND_USB_SERVICE
  "Service TinyUSB from the foreground loop instead of the SDK background task" OFF)

add_library(forgix_bsp STATIC
  src/bsp/bsp.c
  src/bsp/bsp_button.c
  src/bsp/bsp_console.c
  src/bsp/bsp_fpga.c
  src/bsp/bsp_led.c
  src/bsp/bsp_time.c
  src/bsp/bsp_watchdog.c
  ${GENERATED_C})
target_include_directories(forgix_bsp
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/bsp
  PRIVATE ${GENERATED_DIR})
target_link_libraries(forgix_bsp PUBLIC pico_stdlib hardware_spi hardware_watchdog)

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
