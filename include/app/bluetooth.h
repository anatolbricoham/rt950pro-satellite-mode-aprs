/*
 * bluetooth.h - Bluetooth module interface for the RT-950 Pro
 *
 * USART1 (PA9=TX, PA10=RX) @ 115200 baud connects to a BT SPP module
 * (HC-05/BT-04 style). Two operating modes:
 *   1. AT command mode - configure module name, PIN, baud, role
 *   2. SPP data mode  - relay CPS protocol for wireless programming
 *
 * OEM firmware reference:
 *   Bluetooth_UART1_Init  @ 0x0800834C
 *   BT module uses standard AT command set (AT+NAME, AT+PIN, etc.)
 *
 * See also: docs/bluetooth.md for BLE GATT characteristics.
 */

#ifndef APP_BLUETOOTH_H
#define APP_BLUETOOTH_H

#include <stdint.h>

/* BT connection state ----------------------------------------------- */
typedef enum {
    BT_STATE_OFF = 0,       /* Module not initialized */
    BT_STATE_IDLE,          /* Initialized, no connection */
    BT_STATE_CONNECTED,     /* SPP link active */
    BT_STATE_AT_MODE,       /* AT command configuration */
    BT_STATE_CPS_RELAY,     /* Relaying CPS protocol over SPP */
} bt_state_t;

/* AT command result ------------------------------------------------- */
typedef enum {
    BT_AT_OK = 0,
    BT_AT_ERROR,
    BT_AT_TIMEOUT,
} bt_at_result_t;

/* BT module info (populated by bt_query_info) ----------------------- */
typedef struct {
    char name[20];          /* Module advertised name */
    char pin[8];            /* Pairing PIN */
    uint8_t role;           /* 0=slave, 1=master */
} bt_info_t;

/* API --------------------------------------------------------------- */

/* Initialize USART1 and BT module, optionally enter AT mode to configure */
void bt_init(void);

/* Get current BT state */
bt_state_t bt_get_state(void);

/* Returns 1 if an SPP connection is active */
int bt_is_connected(void);

/*
 * Send an AT command and wait for response.
 * cmd: full AT command string (e.g. "AT+NAME=RT950")
 * response: buffer for response (can be NULL)
 * resp_len: size of response buffer
 * Returns BT_AT_OK, BT_AT_ERROR, or BT_AT_TIMEOUT.
 */
bt_at_result_t bt_at_command(const char *cmd, char *response, uint16_t resp_len);

/* Convenience: set module name */
bt_at_result_t bt_set_name(const char *name);

/* Convenience: set pairing PIN */
bt_at_result_t bt_set_pin(const char *pin);

/* Convenience: set role (0=slave, 1=master) */
bt_at_result_t bt_set_role(uint8_t role);

/* Query module info (name, pin, role) via AT commands */
bt_at_result_t bt_query_info(bt_info_t *info);

/*
 * Poll for BT activity - call from main loop.
 * In IDLE/CONNECTED: checks for incoming SPP data -> starts CPS relay.
 * In CPS_RELAY: relays bytes between USART1 (BT) and CPS handler.
 * Returns 1 while CPS relay is active.
 */
int bt_poll(void);

/* Force exit from CPS relay mode */
void bt_relay_stop(void);

#endif /* APP_BLUETOOTH_H */
