# CAN / SPI interface

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
Mass Storage Mode. Copy canspi.uf2 file onto the board to program the flash. In linux the board will appear in the
/run/media/root/ directory (if logged in as root).
