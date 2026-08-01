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

add_library(forgix_bsp STATIC
  src/bsp/bsp.c
  src/bsp/bsp_button.c
  src/bsp/bsp_console.c
  src/bsp/bsp_fpga.c
  src/bsp/bsp_led.c
  ${GENERATED_C})
target_include_directories(forgix_bsp
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/bsp
  PRIVATE ${GENERATED_DIR})
target_link_libraries(forgix_bsp PUBLIC pico_stdlib hardware_spi)

add_library(forgix_application STATIC
  src/application/application.c
  src/application/application_runner.c)
target_include_directories(forgix_application
  PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/application)
target_link_libraries(forgix_application PUBLIC forgix_bsp)

add_executable(forgix_hello_world src/main.c)
target_link_libraries(forgix_hello_world PRIVATE forgix_application)
pico_enable_stdio_usb(forgix_hello_world 1)
pico_enable_stdio_uart(forgix_hello_world 0)
pico_add_extra_outputs(forgix_hello_world)
