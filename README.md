# Environment setup

```
cd pico
git clone https://github.com/raspberrypi/pico-sdk.git --branch master
cd pico-sdk
git submodule update --init
cd ..
git clone https://github.com/raspberrypi/pico-examples.git --branch master
```

# Toolschain setup on Fedora

```
dnf group install "C Development Tools and Libraries" "Development Tools"
dnf install cmake
dnf install arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ arm-none-eabi-newlib
```

# Build examples

```
export PICO_SDK_PATH=/SOMEDIR/pico-sdk
cd pico-examples
mkdir build
cd build
cmake ..
cd blinky
make -j4
```

# Connect to serial comm port (USB)

```
minicom -D /dev/ttyACM0 -b 115200
```

# References

- W5100S-EVB Pico: https://docs.wiznet.io/Product/Chip/Ethernet/W5100S/w5100s-evb-pico
- Raspberri Pi Pico datasheet: https://pip-assets.raspberrypi.com/categories/610-raspberry-pi-pico/documents/RP-008307-DS-1-pico-datasheet.pdf
- Getting started (C/C++) : https://pip-assets.raspberrypi.com/categories/610-raspberry-pi-pico/documents/RP-008276-DS-1-getting-started-with-pico.pdf
- https://lofthouse.dev/2024/02/23/raspberry-pi-pico-c-assembly-development-on-fedora/
