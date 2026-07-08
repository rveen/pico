# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A bare-metal firmware project for the Raspberry Pi Pico (RP2040) that drives an
MCP2518FD CAN-FD controller over SPI in classic-CAN mode, and prints received
frames over the USB CDC serial console. Built directly on the Pico C SDK (no
RTOS). Target board is a "CAN Pico" (RP2040 + MCP2518FD on one board,
`w5100s-evb-pico`-style form factor — see the pinout PNG and
`CANPico hardware reference manual.pdf` in the repo root).

## Build

Requires the Pico SDK checked out separately (this repo only contains
`pico_sdk_import.cmake`, a pointer file). The SDK path used in this
environment is `/files/pico/pico-sdk`.

```bash
mkdir -p build && cd build
cmake .. -DPICO_SDK_PATH=/files/pico/pico-sdk
make -j4
```

Outputs land in `build/`: `canread.uf2` (drag-and-drop flash to the Pico in
BOOTSEL mode), plus `.elf`, `.hex`, `.bin`, `.dis`, and `.elf.map`.

There is no test suite, linter, or CI — this is a two-source-file embedded
program flashed to hardware and verified by reading its serial output.

## Architecture

Two translation units:

- **`mcp2518.c` / `mcp2518.h`** — the CAN controller driver. Owns all SPI
  traffic to the MCP2518FD and hides its register map behind three calls:
  `can_reset()` (SPI bus bring-up + hardware reset + oscillator-ready poll),
  `can_setup_controller(bitrate, mode)` (bit timing, RX FIFO, match-all
  acceptance filter, mode transition), and `can_read_message(frame)`
  (non-blocking FIFO poll into a `can_frame_t`). This is a polled RX-only
  driver — it does not use the MCP2518FD's INT pin, and there is no transmit
  path.
- **`canread.c`** — `main()`. Initializes stdio/USB, resets and configures the
  controller at 500 kbit/s in normal mode, then loops printing every received
  frame as `ID: <hex> DLC: <n> Data: <bytes>` over USB serial.

Key hardware/protocol details baked into the driver (see comments in
`mcp2518.c` and `mcp2518.h` for specifics before changing them):

- SPI0 pins are fixed in RP2040 silicon (GP2/3/4 for SCK/TX/RX, GP5 as a
  plain GPIO chip-select) — these cannot be reassigned.
- Bit-timing constants in `can_setup_controller()` are precomputed for a
  20 MHz crystal with the PLL disabled (`CAN_OSC_HZ`). Changing the crystal
  or enabling the PLL requires recalculating `brp`/`tseg1`/`tseg2`/`sjw` for
  every `can_bitrate_t` value.
- The MCP2518FD SPI clock is bounded by `0.85 * SYSCLK/2`; `can_reset()`
  starts SPI at a conservative 1 MHz until `OSCRDY` is confirmed, then ramps
  to 8 MHz.
- RX uses FIFO 1 with a single match-all filter (filter 0, mask 0) routing
  every incoming ID there; there is no filtering/routing logic beyond that.
