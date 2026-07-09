# CAN / SPI interface

A bare-metal firmware project for the Raspberry Pi Pico (RP2040) that drives an MCP2518FD CAN-FD controller over SPI in classic-CAN mode, and prints received frames over the USB CDC serial console. Built directly on the Pico C SDK (no RTOS).

The hardware details are available in canspi-hw/.

## Build

```
export PICO_SDK_PATH=/SOMEDIR/pico-sdk
cd canspi
mkdir build
cd build
cmake ..
make -j4
```

## Install

Hold down the BOOTSEL button on the Pico board while plugging in your device using a micro-USB cable to force it into USB
Mass Storage Mode. Copy build/canread.uf2 file onto the board to program the flash. In linux the board will appear as
/run/media/root/RPI-RP2 (if logged in as root, that is). Do a 'sync' operation to flush the write operation.
