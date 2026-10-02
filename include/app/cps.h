/*
 * cps.h - CPS (Customer Programming Software) protocol handler
 *
 * V0.27 firmware addresses:
 *   UART4 init:       fw 0x08022762 (115200 baud, PC10/PC11)
 *   CPS state struct: RAM 0x2000A3B4
 *   CPS mode flag:    offset 0x4A in state struct (0xA5 = CPS active)
 *   Model string:     fw 0x080003E0 "RT-950      " (12 chars)
 *
 * Protocol: 0xA5-framed packets, CRC-CCITT, 128-byte data blocks.
 * Commands: R(ead), W(rite), T(readEnd), X(writeEnd), E(rase).
 *
 * See also: docs/cps-uart.md for full protocol specification.
 */

#ifndef APP_CPS_H
#define APP_CPS_H

#include <stdint.h>

/* CPS protocol state */
typedef enum {
    CPS_STATE_IDLE       = 0,
    CPS_STATE_HANDSHAKE  = 1,
    CPS_STATE_TRANSFER   = 2,
    CPS_STATE_DONE       = 3,
} cps_state_t;

/* CPS command bytes */
#define CPS_CMD_READ        0x52  /* 'R' - CPS requests read */
#define CPS_CMD_WRITE       0x57  /* 'W' - CPS sends data */
#define CPS_CMD_READ_END    0x54  /* 'T' - read session complete */
#define CPS_CMD_WRITE_END   0x58  /* 'X' - write session complete */
#define CPS_CMD_ERASE       0x45  /* 'E' - erase flash block */

/* Satellite extension (custom firmware only, see tools/rt950_sat.py) */
#define CPS_CMD_SAT_INFO    0x49  /* 'I' - query satellite support/info   */
#define CPS_CMD_SAT_TIME    0x4B  /* 'K' - set UTC clock: u32 unix LE      */
#define CPS_CMD_SAT_RELOAD  0x4C  /* 'L' - re-read satellite DB from flash */
#define CPS_SAT_API_VERSION 1

/* Frame constants */
#define CPS_FRAME_HEADER    0xA5
#define CPS_FRAME_PAD       0xFF
#define CPS_PACKET_SIZE     128   /* data bytes per transfer packet */
#define CPS_ERASE_BLOCK     256   /* erase granularity */

/* Handshake threshold - enter programming mode after 5 bytes */
#define CPS_HANDSHAKE_COUNT 5

/*
 * cps_init - Reset CPS state machine to idle.
 */
void cps_init(void);

/*
 * cps_poll - Call from main loop or UART4 ISR.
 * Processes incoming bytes, drives the state machine.
 * Returns non-zero while CPS session is active (radio should mute RF).
 */
int cps_poll(void);

/*
 * cps_is_active - Returns non-zero if CPS session is in progress.
 * Radio should suppress normal operation (mute RF, hold display).
 */
int cps_is_active(void);

/*
 * cps_crc_ccitt - CRC-CCITT calculation (polynomial 0x1021, init 0).
 */
uint16_t cps_crc_ccitt(const uint8_t *data, uint16_t offset, uint16_t length);

#endif /* APP_CPS_H */
