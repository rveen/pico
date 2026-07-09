// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Rolf Veen

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "mcp2518.h"

// ---------------------------------------------------------------------------
// MCP2518FD register map (see MCP25XXFD-CAN-FD-Controller-Module-Family
// Reference Manual). Only the subset needed for a polled RX-only driver.
// ---------------------------------------------------------------------------
#define OSC             0xE00U
#define OSC_OSCRDY      (1U << 10)

#define C1CON           0x000U
#define C1CON_REQOP_SHIFT   24U
#define C1CON_OPMOD_SHIFT   21U
#define C1CON_MODE_MASK     0x7U
#define C1CON_MODE_CONFIG   0x4U
#define C1CON_MODE_LISTEN   0x3U
#define C1CON_MODE_NORMAL2  0x6U   // "Normal CAN 2.0" (classic CAN only)

#define C1NBTCFG        0x004U

#define C1FIFOCON1      0x05CU
#define C1FIFOCON1_UINC     (1U << 8)
#define C1FIFOCON1_FSIZE(n) (((n) & 0x1FU) << 24)

#define C1FIFOSTA1      0x060U
#define C1FIFOSTA1_TFNRFNIF (1U << 0)   // FIFO not empty

#define C1FIFOUA1       0x064U

// FIFO 2 is the transmit FIFO. Same register layout as FIFO 1, just at the
// next FIFO's address (each FIFO's CON/STA/UA registers are 12 bytes apart).
#define C1FIFOCON2      0x068U
#define C1FIFOCON2_TXEN     (1U << 7)
#define C1FIFOCON2_UINC     (1U << 8)
#define C1FIFOCON2_TXREQ    (1U << 9)
#define C1FIFOCON2_FSIZE(n) (((n) & 0x1FU) << 24)

#define C1FIFOSTA2      0x06CU
#define C1FIFOSTA2_TFNRFNIF (1U << 0)   // FIFO not full

#define C1FIFOUA2       0x070U

#define C1FLTCON0       0x1D0U   // byte 0 of this word controls filter 0
#define C1FLTOBJ0       0x1F0U
#define C1MASK0         0x1F4U

#define MSG_RAM_BASE    0x400U   // RX/TX message RAM window starts here

// ---------------------------------------------------------------------------
// Low level SPI transactions (MCP2518FD SPI command format: 4-bit opcode in
// the top nibble of the first byte, 12-bit address in the rest)
// ---------------------------------------------------------------------------
#define SPI_CMD_RESET   0x00U
#define SPI_CMD_READ    0x3U
#define SPI_CMD_WRITE   0x2U

static inline void can_cs_select(void) {
    gpio_put(CAN_CS_PIN, 0);
}

static inline void can_cs_deselect(void) {
    gpio_put(CAN_CS_PIN, 1);
}

static void write_word(uint16_t addr, uint32_t word) {
    uint8_t buf[6];
    buf[0] = (uint8_t)(SPI_CMD_WRITE << 4) | ((addr >> 8) & 0x0FU);
    buf[1] = (uint8_t)(addr & 0xFFU);
    buf[2] = (uint8_t)(word & 0xFFU);
    buf[3] = (uint8_t)((word >> 8) & 0xFFU);
    buf[4] = (uint8_t)((word >> 16) & 0xFFU);
    buf[5] = (uint8_t)((word >> 24) & 0xFFU);

    can_cs_select();
    spi_write_blocking(CAN_SPI_PORT, buf, sizeof(buf));
    can_cs_deselect();
}

static uint32_t read_word(uint16_t addr) {
    uint8_t cmd[2];
    uint8_t resp[4];
    cmd[0] = (uint8_t)(SPI_CMD_READ << 4) | ((addr >> 8) & 0x0FU);
    cmd[1] = (uint8_t)(addr & 0xFFU);

    can_cs_select();
    spi_write_blocking(CAN_SPI_PORT, cmd, sizeof(cmd));
    spi_read_blocking(CAN_SPI_PORT, 0x00, resp, sizeof(resp));
    can_cs_deselect();

    return (uint32_t)resp[0] | ((uint32_t)resp[1] << 8) |
           ((uint32_t)resp[2] << 16) | ((uint32_t)resp[3] << 24);
}

static void read_bytes(uint16_t addr, uint8_t *dst, size_t n) {
    uint8_t cmd[2];
    cmd[0] = (uint8_t)(SPI_CMD_READ << 4) | ((addr >> 8) & 0x0FU);
    cmd[1] = (uint8_t)(addr & 0xFFU);

    can_cs_select();
    spi_write_blocking(CAN_SPI_PORT, cmd, sizeof(cmd));
    spi_read_blocking(CAN_SPI_PORT, 0x00, dst, n);
    can_cs_deselect();
}

static void write_bytes(uint16_t addr, const uint8_t *src, size_t n) {
    uint8_t cmd[2];
    cmd[0] = (uint8_t)(SPI_CMD_WRITE << 4) | ((addr >> 8) & 0x0FU);
    cmd[1] = (uint8_t)(addr & 0xFFU);

    can_cs_select();
    spi_write_blocking(CAN_SPI_PORT, cmd, sizeof(cmd));
    spi_write_blocking(CAN_SPI_PORT, src, n);
    can_cs_deselect();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void can_reset(void) {
    // Use a conservative SPI clock until the oscillator has been confirmed
    // stable; ramped up to full speed at the end of this function.
    spi_init(CAN_SPI_PORT, 1000000);
    gpio_set_function(CAN_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CAN_SDI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CAN_SDO_PIN, GPIO_FUNC_SPI);
    spi_set_format(CAN_SPI_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_init(CAN_CS_PIN);
    gpio_set_dir(CAN_CS_PIN, GPIO_OUT);
    gpio_put(CAN_CS_PIN, 1);   // deselected (active low)

    sleep_ms(2);    // let CS/SPI lines settle before talking to the chip

    // RESET command: 12 zero bits sent as two bytes
    uint8_t reset_cmd[2] = {SPI_CMD_RESET, 0x00};
    can_cs_select();
    spi_write_blocking(CAN_SPI_PORT, reset_cmd, sizeof(reset_cmd));
    can_cs_deselect();

    // After Reset the device re-runs its oscillator start-up timer. Poll
    // OSCRDY (with a bounded timeout) instead of trusting a fixed delay.
    for (int i = 0; i < 1000; i++) {
        if (read_word(OSC) & OSC_OSCRDY) {
            break;
        }
        sleep_us(100);
    }

    // With a 20MHz crystal and the PLL left disabled, SYSCLK = 20MHz, so SCK
    // must be <= 0.85 * SYSCLK/2 = 8.5MHz. Use a bit under that for margin.
    spi_set_baudrate(CAN_SPI_PORT, 8000000);
}

bool can_setup_controller(can_bitrate_t bitrate, can_mode_t mode) {
    // Request Configuration mode (required before changing bit timing, FIFO
    // or filter configuration) and confirm the controller got there.
    bool ok = false;
    for (int i = 0; i < 50; i++) {
        write_word(C1CON, C1CON_MODE_CONFIG << C1CON_REQOP_SHIFT);
        uint32_t c1con = read_word(C1CON);
        if (((c1con >> C1CON_OPMOD_SHIFT) & C1CON_MODE_MASK) == C1CON_MODE_CONFIG) {
            ok = true;
            break;
        }
    }
    if (!ok) {
        return false;
    }

    // Nominal bit timing, precomputed for a 20MHz crystal (CAN_OSC_HZ) with
    // no PLL, using 20 time quanta/bit (85% sample point) for every rate so
    // the prescaler comes out to an exact integer in each case. Register
    // fields already encode "actual value - 1" as required by the datasheet.
    uint32_t brp, tseg1, tseg2, sjw;
    switch (bitrate) {
        case CAN_BITRATE_125K:
            brp = 7; tseg1 = 15; tseg2 = 2; sjw = 1;
            break;
        case CAN_BITRATE_250K:
            brp = 3; tseg1 = 15; tseg2 = 2; sjw = 1;
            break;
        case CAN_BITRATE_1M:
            brp = 0; tseg1 = 15; tseg2 = 2; sjw = 1;
            break;
        case CAN_BITRATE_500K:
        default:
            brp = 1; tseg1 = 15; tseg2 = 2; sjw = 1;
            break;
    }
    write_word(C1NBTCFG, (brp << 24) | (tseg1 << 16) | (tseg2 << 8) | sjw);

    // FIFO 1 is the receive FIFO, 8 messages deep (FSIZE is depth-1). No
    // timestamping, no controller-interrupt-line enables: this driver polls
    // over SPI rather than using the MCP2518FD's INT pin.
    write_word(C1FIFOCON1, C1FIFOCON1_FSIZE(7));

    // FIFO 2 is the transmit FIFO, 8 messages deep, enabled via TXEN. Loaded
    // and kicked off by can_write_message().
    write_word(C1FIFOCON2, C1FIFOCON2_TXEN | C1FIFOCON2_FSIZE(7));

    // Filter 0: match every ID (mask = 0) and route hits to FIFO 1.
    write_word(C1FLTOBJ0, 0);
    write_word(C1MASK0, 0);
    write_word(C1FLTCON0, 0x81U);   // FLTEN0=1, F0BP[4:0]=1 (FIFO 1)

    uint32_t reqop = (mode == CAN_MODE_LISTEN_ONLY) ? C1CON_MODE_LISTEN : C1CON_MODE_NORMAL2;
    ok = false;
    for (int i = 0; i < 200; i++) {
        write_word(C1CON, reqop << C1CON_REQOP_SHIFT);
        uint32_t c1con = read_word(C1CON);
        if (((c1con >> C1CON_OPMOD_SHIFT) & C1CON_MODE_MASK) == reqop) {
            ok = true;
            break;
        }
    }
    return ok;
}

bool can_read_message(can_frame_t *frame) {
    uint32_t sta = read_word(C1FIFOSTA1);
    if (!(sta & C1FIFOSTA1_TFNRFNIF)) {
        return false;   // FIFO empty
    }

    uint16_t addr = (uint16_t)read_word(C1FIFOUA1) + MSG_RAM_BASE;

    uint8_t hdr[8];
    read_bytes(addr, hdr, sizeof(hdr));
    uint32_t r0 = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                  ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t r1 = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                  ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);

    bool extended = (r1 >> 4) & 1U;
    bool remote = (r1 >> 5) & 1U;
    uint8_t dlc = (uint8_t)(r1 & 0xFU);

    // R0 holds SID[10:0] at bits 10:0 and EID[17:0] at bits 28:11. For
    // standard frames only SID is meaningful; for extended frames the two
    // fields concatenate (SID:EID) into the conventional 29-bit ID.
    uint32_t raw = r0 & 0x1FFFFFFFU;
    uint32_t id = extended ? (((raw & 0x7FFU) << 18) | ((raw >> 11) & 0x3FFFFU))
                            : (raw & 0x7FFU);

    frame->id = id;
    frame->extended = extended;
    frame->remote = remote;
    frame->dlc = dlc;

    uint8_t nbytes = (dlc > 8U) ? 8U : dlc;
    if (nbytes > 0) {
        read_bytes(addr + 8U, frame->data, nbytes);
    }
    if (nbytes < 8U) {
        memset(frame->data + nbytes, 0, 8U - nbytes);
    }

    // Tell the controller we've taken the message so it can advance the FIFO.
    write_word(C1FIFOCON1, C1FIFOCON1_UINC);

    return true;
}

bool can_write_message(const can_frame_t *frame) {
    uint32_t sta = read_word(C1FIFOSTA2);
    if (!(sta & C1FIFOSTA2_TFNRFNIF)) {
        return false;   // FIFO full
    }

    uint16_t addr = (uint16_t)read_word(C1FIFOUA2) + MSG_RAM_BASE;

    // Inverse of the ID decode in can_read_message(): SID occupies bits
    // 10:0 of R0, EID occupies bits 28:11.
    uint32_t raw;
    if (frame->extended) {
        uint32_t sid = (frame->id >> 18) & 0x7FFU;
        uint32_t eid = frame->id & 0x3FFFFU;
        raw = sid | (eid << 11);
    } else {
        raw = frame->id & 0x7FFU;
    }

    uint8_t dlc = (frame->dlc > 8U) ? 8U : frame->dlc;
    uint32_t r1 = dlc | (frame->remote ? (1U << 5) : 0U) | (frame->extended ? (1U << 4) : 0U);

    uint8_t hdr[8];
    hdr[0] = (uint8_t)(raw & 0xFFU);
    hdr[1] = (uint8_t)((raw >> 8) & 0xFFU);
    hdr[2] = (uint8_t)((raw >> 16) & 0xFFU);
    hdr[3] = (uint8_t)((raw >> 24) & 0xFFU);
    hdr[4] = (uint8_t)(r1 & 0xFFU);
    hdr[5] = (uint8_t)((r1 >> 8) & 0xFFU);
    hdr[6] = (uint8_t)((r1 >> 16) & 0xFFU);
    hdr[7] = (uint8_t)((r1 >> 24) & 0xFFU);
    write_bytes(addr, hdr, sizeof(hdr));

    if (dlc > 0) {
        write_bytes(addr + 8U, frame->data, dlc);
    }

    // UINC (advance the FIFO head) and TXREQ (request transmission) must be
    // set together in the same write. This register also holds the static
    // TXEN/FSIZE config from can_setup_controller(), so read-modify-write it
    // rather than clobbering those bits.
    uint32_t con = read_word(C1FIFOCON2);
    write_word(C1FIFOCON2, con | C1FIFOCON2_UINC | C1FIFOCON2_TXREQ);

    return true;
}
