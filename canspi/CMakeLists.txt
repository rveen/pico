cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(canread C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(canread
mcp2518.c
canread.c
)
pico_enable_stdio_usb(canread 1)
pico_enable_stdio_uart(canread 0)
pico_add_extra_outputs(canread)
target_link_libraries(canread pico_stdlib hardware_spi)
