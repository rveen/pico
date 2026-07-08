// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Rolf Veen

#ifndef MCP2518_H
#define MCP2518_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

// Wiring (RP2040 SPI0 pins are fixed in silicon, they cannot be swapped):
//   GP2 / SPI0 SCK  -> MCP2518FD SCK
//   GP3 / SPI0 TX   -> MCP2518FD SDI  (Pico transmits, chip receives)
//   GP4 / SPI0 RX   <- MCP2518FD SDO  (chip transmits, Pico receives)
//   GP5             -> MCP2518FD CS   (plain GPIO, driven low/high in software)
#define CAN_SPI_PORT    spi0
#define CAN_SCK_PIN     2
#define CAN_SDI_PIN     3
#define CAN_SDO_PIN     4
#define CAN_CS_PIN      5

// Crystal/oscillator frequency feeding the MCP2518FD, PLL left disabled (this
// board uses a 20MHz crystal). If you change crystals, the bit-time constants
// in mcp2518.c must be recalculated for the new clock.
#define CAN_OSC_HZ      20000000UL

typedef enum {
    CAN_BITRATE_125K = 0,
    CAN_BITRATE_250K,
    CAN_BITRATE_500K,
    CAN_BITRATE_1M,
} can_bitrate_t;

typedef enum {
    CAN_MODE_NORMAL = 0,    // Normal CAN 2.0: transmits ACK/error flags onto the bus
    CAN_MODE_LISTEN_ONLY,   // Passive: never drives the bus, even for ACK/error frames
} can_mode_t;

typedef struct {
    uint32_t id;        // Arbitration ID (11-bit standard or 29-bit extended)
    bool extended;      // True if 29-bit extended ID
    bool remote;        // True if remote frame (RTR)
    uint8_t dlc;         // Data length code (0-8 for classic CAN frames)
    uint8_t data[8];     // Payload, valid for the first dlc bytes
} can_frame_t;

// Brings up the SPI bus and hardware-resets the MCP2518FD. Must be called once
// at startup, before can_setup_controller().
void can_reset(void);

// Configures nominal bit timing, the receive FIFO and a match-all acceptance
// filter, then requests the given operating mode. Returns true on success,
// false if the controller did not respond as expected (e.g. not connected or
// wiring problem).
bool can_setup_controller(can_bitrate_t bitrate, can_mode_t mode);

// Polls the receive FIFO. If a frame is waiting, fills *frame and returns
// true. Returns false immediately if no frame is available; this call never
// blocks, so it is safe to call in a tight loop.
bool can_read_message(can_frame_t *frame);

#endif
