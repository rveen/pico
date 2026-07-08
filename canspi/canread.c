// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Rolf Veen

#include <stdio.h>
#include "pico/stdlib.h"
#include "mcp2518.h"

// Set to 1 to ACK/error-flag frames on the bus (CAN_MODE_NORMAL) — needed if
// this node is the only other device on the bus, since CAN requires at least
// one ACKing node per frame. Set to 0 for listen-only mode (CAN_MODE_LISTEN_ONLY)
// — passive sniffing that never drives the bus, safe to run alongside other
// live traffic without risking an errant ACK/error frame.
#define CAN_ACK_ENABLED 1

int main() {
    stdio_init_all();
    sleep_ms(2000);     // give the USB CDC serial connection time to enumerate

    can_reset();

    can_mode_t can_mode = CAN_ACK_ENABLED ? CAN_MODE_NORMAL : CAN_MODE_LISTEN_ONLY;

    if (!can_setup_controller(CAN_BITRATE_500K, can_mode)) {
        printf("Failed to initialize MCP2518FD (check wiring/power)\n");
        while (true) {
            tight_loop_contents();
        }
    }

    printf("MCP2518FD initialized (%s), listening for CAN frames...\n",
           can_mode == CAN_MODE_NORMAL ? "ACK enabled" : "listen-only, no ACK");

    can_frame_t rx;
    while (true) {
        if (can_read_message(&rx)) {
            printf("ID: %s%08x  DLC: %u  Data:", rx.extended ? "X" : "S", rx.id, rx.dlc);
            for (uint8_t i = 0; i < rx.dlc; i++) {
                printf(" %02x", rx.data[i]);
            }
            printf("\n");
        }
    }

    return 0;
}
