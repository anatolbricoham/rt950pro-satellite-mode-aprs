/*
 * bluetooth.c - Bluetooth AT commands + CPS relay for the RT-950 Pro
 *
 * USART1 (PA9=TX, PA10=RX) @ 115200 connects to HC-05/BT-04 SPP module.
 *
 * AT mode: module config (name, PIN, baud, role). Enter AT mode by
 * holding KEY/EN pin high during power-on (or via AT+ORGL reset).
 * Most HC-05 clones auto-enter AT mode when no SPP link is active.
 *
 * SPP data mode: transparent serial bridge. When CPS handshake bytes
 * arrive via BT, we relay them to the CPS handler, creating a wireless
 * programming interface identical to the wired UART4 path.
 *
 * OEM reference: Bluetooth_UART1_Init @ 0x0800834C
 */

#include "app/bluetooth.h"
#include "app/cps.h"
#include "drivers/uart.h"
#include "drivers/gpio.h"
#include "at32f403a.h"
#include "debug_uart.h"

#include <string.h>

extern void delay_ms(uint32_t ms);

/* State ------------------------------------------------------------- */
static bt_state_t  state;
static uint8_t     rx_buf[512];  /* OEM uses 512B @ 0x2000AF70 */
static uint16_t    rx_pos;

/* CPS relay state */
static uint8_t     relay_handshake;

/* Helpers ----------------------------------------------------------- */

static void bt_send_str(const char *s)
{
    while (*s)
        uart_send_byte(USART1, (uint8_t)*s++);
}

static void bt_send_crlf(void)
{
    uart_send_byte(USART1, '\r');
    uart_send_byte(USART1, '\n');
}

/* Drain USART1 RX into buffer with timeout (ms). Returns bytes read. */
static uint16_t bt_recv_timeout(uint8_t *buf, uint16_t max, uint16_t timeout_ms)
{
    uint16_t pos = 0;
    uint16_t idle_ms = 0;

    while (idle_ms < timeout_ms && pos < max) {
        int16_t ch = uart_bt_rx_read();
        if (ch >= 0) {
            buf[pos++] = (uint8_t)ch;
            idle_ms = 0;
        } else {
            delay_ms(1);
            idle_ms++;
        }
    }
    return pos;
}

/* Check if response buffer contains "OK" */
static int response_has_ok(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i + 1 < len; i++) {
        if (buf[i] == 'O' && buf[i + 1] == 'K')
            return 1;
    }
    return 0;
}

/* Extract value after ':', e.g. "+NAME:RT950\r\n" -> "RT950" */
static uint16_t extract_value(const uint8_t *buf, uint16_t len,
                              char *out, uint16_t out_max)
{
    /* Find ':' */
    uint16_t start = 0;
    while (start < len && buf[start] != ':')
        start++;
    if (start >= len) return 0;
    start++;    /* skip ':' */

    uint16_t p = 0;
    while (start < len && buf[start] != '\r' && buf[start] != '\n' &&
           p < out_max - 1) {
        out[p++] = (char)buf[start++];
    }
    out[p] = '\0';
    return p;
}

/* ==========================================================================
 *  AT command interface
 * ========================================================================== */

bt_at_result_t bt_at_command(const char *cmd, char *response, uint16_t resp_len)
{
    /* Flush any pending RX data */
    uart_bt_rx_flush();

    /* Send command + CRLF */
    bt_send_str(cmd);
    bt_send_crlf();

    /* Wait for response (500ms timeout) */
    uint8_t resp[128];
    uint16_t rlen = bt_recv_timeout(resp, sizeof(resp), 500);

    if (rlen == 0)
        return BT_AT_TIMEOUT;

    /* Copy to caller's buffer if provided */
    if (response && resp_len > 0) {
        uint16_t clen = rlen;
        if (clen > resp_len - 1) clen = resp_len - 1;
        memcpy(response, resp, clen);
        response[clen] = '\0';
    }

    return response_has_ok(resp, rlen) ? BT_AT_OK : BT_AT_ERROR;
}

bt_at_result_t bt_set_name(const char *name)
{
    char cmd[40] = "AT+NAME=";
    uint8_t i = 8;
    while (*name && i < sizeof(cmd) - 1)
        cmd[i++] = *name++;
    cmd[i] = '\0';
    return bt_at_command(cmd, 0, 0);
}

bt_at_result_t bt_set_pin(const char *pin)
{
    char cmd[24] = "AT+PIN=";
    uint8_t i = 7;
    while (*pin && i < sizeof(cmd) - 1)
        cmd[i++] = *pin++;
    cmd[i] = '\0';
    return bt_at_command(cmd, 0, 0);
}

bt_at_result_t bt_set_role(uint8_t role)
{
    const char *cmd = role ? "AT+ROLE=1" : "AT+ROLE=0";
    return bt_at_command(cmd, 0, 0);
}

bt_at_result_t bt_query_info(bt_info_t *info)
{
    memset(info, 0, sizeof(*info));

    char resp[64];

    /* Query name */
    if (bt_at_command("AT+NAME?", resp, sizeof(resp)) == BT_AT_OK)
        extract_value((const uint8_t *)resp, (uint16_t)strlen(resp),
                      info->name, sizeof(info->name));

    /* Query PIN */
    if (bt_at_command("AT+PIN?", resp, sizeof(resp)) == BT_AT_OK)
        extract_value((const uint8_t *)resp, (uint16_t)strlen(resp),
                      info->pin, sizeof(info->pin));

    /* Query role */
    if (bt_at_command("AT+ROLE?", resp, sizeof(resp)) == BT_AT_OK) {
        char role_str[4];
        extract_value((const uint8_t *)resp, (uint16_t)strlen(resp),
                      role_str, sizeof(role_str));
        info->role = (role_str[0] == '1') ? 1 : 0;
    }

    return BT_AT_OK;
}

/* ==========================================================================
 *  CPS relay - bridge USART1 (BT) <-> CPS protocol handler
 *
 *  When CPS handshake bytes arrive over Bluetooth SPP, we enter relay
 *  mode. All subsequent data is forwarded bidirectionally:
 *    BT RX -> UART4 TX (to CPS handler)
 *    UART4 RX -> BT TX (CPS responses back to PC)
 *
 *  This gives the CPS software transparent wireless programming as if
 *  plugged in via cable.
 * ========================================================================== */

static void relay_start(void)
{
    state = BT_STATE_CPS_RELAY;

    relay_handshake = 0;

    /* Forward handshake bytes that triggered relay to UART4 */
    for (uint16_t i = 0; i < rx_pos; i++)
        uart_send_byte(UART4, rx_buf[i]);
    rx_pos = 0;
}

static void relay_poll(void)
{
    /* BT RX -> UART4 TX (CPS commands from PC) */
    while (uart_bt_rx_available()) {
        int16_t c = uart_bt_rx_read();
        if (c >= 0)
            uart_send_byte(UART4, (uint8_t)c);
    }

    /* UART4 RX -> BT TX (CPS responses to PC) */
    while (uart_cps_rx_available()) {
        int16_t c = uart_cps_rx_read();
        if (c >= 0)
            uart_send_byte(USART1, (uint8_t)c);
    }
}

/* ==========================================================================
 *  Public API
 * ========================================================================== */

void bt_init(void)
{
    dbg_puts("[DBG]   bt: uart_bt_init\n");
    uart_bt_init();

    /*
     * PE9 (SW_TO_BT) - Audio routing switch: LOW = speaker, HIGH = BT.
     * OEM gpio_modes_init configures PE9 as output LOW (speaker mode).
     * Only set HIGH when BT audio streaming is active.
     * DO NOT set HIGH during init - it routes audio away from speaker!
     */
    gpio_config_pin(GPIOE, 9, GPIO_MODE_OUT_10MHZ, GPIO_CNF_PP);
    gpio_clear_pin(GPIOE, GPIO_PIN_9);  /* PE9 LOW = speaker audio path */
    delay_ms(100);             /* BT module power-up settle */

    state = BT_STATE_IDLE;
    rx_pos = 0;

    relay_handshake = 0;

    /* Verify SysTick is running before using delay_ms */
    {
        extern uint32_t get_tick(void);
        uint32_t t0 = get_tick();
        for (volatile uint32_t i = 0; i < 1000000; i++) ;
        uint32_t t1 = get_tick();
        dbg_puts("[DBG]   bt: tick delta=");
        dbg_hex32(t1 - t0);
        dbg_newline();
        (void)t0; (void)t1;
    }

    /* Brief AT probe - if module responds, configure defaults */
    dbg_puts("[DBG]   bt: AT probe\n");
    bt_at_result_t r = bt_at_command("AT", 0, 0);
    dbg_puts("[DBG]   bt: AT done\n");
    if (r == BT_AT_OK) {
        bt_set_name("RT-950");
        bt_set_pin("1234");
        bt_set_role(0);     /* slave - wait for host connection */
    }
    dbg_puts("[DBG]   bt: init done\n");
}

bt_state_t bt_get_state(void)
{
    return state;
}

int bt_is_connected(void)
{
    return (state == BT_STATE_CONNECTED || state == BT_STATE_CPS_RELAY);
}

int bt_poll(void)
{
    switch (state) {
    case BT_STATE_OFF:
        break;

    case BT_STATE_IDLE:
    case BT_STATE_CONNECTED:
        /* Check for incoming data on USART1 - could be SPP connection */
        while (uart_bt_rx_available() && rx_pos < sizeof(rx_buf)) {
            int16_t ch = uart_bt_rx_read();
            if (ch >= 0)
                rx_buf[rx_pos++] = (uint8_t)ch;
        }

        if (rx_pos > 0) {
            state = BT_STATE_CONNECTED;

            /* Detect CPS handshake: multiple 0xA5 bytes or preamble */
            uint8_t handshake_count = 0;
            for (uint16_t i = 0; i < rx_pos; i++) {
                if (rx_buf[i] == CPS_FRAME_HEADER)
                    handshake_count++;
            }

            if (handshake_count >= CPS_HANDSHAKE_COUNT) {
                /* CPS over Bluetooth - enter relay mode */
                relay_start();
                return 1;
            }

            /* Not CPS data - discard after brief timeout.
             * In a full implementation, this could be AT command
             * pass-through or other SPP protocol. */
            if (rx_pos >= sizeof(rx_buf))
                rx_pos = 0;
        }
        break;

    case BT_STATE_CPS_RELAY:
        relay_poll();
        /* Exit relay when CPS session ends (detect idle) */
        if (!cps_is_active() && !uart_bt_rx_available() &&
            !uart_cps_rx_available()) {
            /* Brief inactivity check - stay in relay a while */
            static uint16_t idle_ticks;
            idle_ticks++;
            if (idle_ticks > 500) {     /* ~500 main loop cycles */
                bt_relay_stop();
                idle_ticks = 0;
            }
        } else {
            /* Activity - keep relaying */
        }
        return 1;

    case BT_STATE_AT_MODE:
        /* AT mode is only entered synchronously via bt_at_command */
        break;
    }

    return 0;
}

void bt_relay_stop(void)
{
    state = BT_STATE_IDLE;
    rx_pos = 0;

}
