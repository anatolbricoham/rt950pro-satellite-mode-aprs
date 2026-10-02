/*
 * uart.h - Interrupt-driven UART driver for the RT-950 Pro
 *
 * Three UART channels (V0.27 firmware verified):
 *   USART1  - Bluetooth module (PA9/PA10)  @ 115200 baud  fw 0x08007536
 *   USART3  - GPS module (PB10/PB11)       @   9600 baud  fw 0x08013B62
 *   UART4   - CPS / accessory (PC10/PC11)  @ 115200 baud  fw 0x08022762
 *
 * OEM uses SDK USART_Init (fw 0x0802240C) with BRR rounding.
 * OEM enables RDBFIEN (CR1 bit 5) but ISR vectors are non-functional.
 *
 * Our implementation enables real RXNE interrupts with ring buffers
 * on all three UARTs. GPS (USART3) ISR is in gps.c for historical
 * reasons; BT and CPS ring buffers are managed here.
 */

#ifndef DRIVERS_UART_H
#define DRIVERS_UART_H

#include "at32f403a.h"

/* Ring buffer sizes (must be power of 2) */
#define UART_BT_BUF_SIZE    256
#define UART_CPS_BUF_SIZE   256

/*
 * uart_gps_init - Initialize USART3 for GPS at 9600 baud.
 * Enables RXNE interrupt; ISR handler is in gps.c (USART3_IRQHandler).
 */
void uart_gps_init(void);

/*
 * uart_bt_init - Initialize USART1 for Bluetooth at 115200 baud.
 * Enables RXNE interrupt; ISR handler is USART1_IRQHandler in uart.c.
 */
void uart_bt_init(void);

/*
 * uart_acc_init - Initialize UART4 for accessory port at 115200 baud.
 * Enables RXNE interrupt; ISR handler is UART4_IRQHandler in uart.c.
 */
void uart_acc_init(void);

/* Blocking transmit */
void uart_send_byte(USART_TypeDef *uart, uint8_t data);
void uart_send_buf(USART_TypeDef *uart, const uint8_t *buf, uint16_t len);

/* Interrupt-driven receive (ring buffer backed) */

/* Check if BT ring buffer has data. Returns count of available bytes. */
uint16_t uart_bt_rx_available(void);

/* Read one byte from BT ring buffer. Returns -1 if empty. */
int16_t uart_bt_rx_read(void);

/* Read up to len bytes from BT ring buffer. Returns bytes read. */
uint16_t uart_bt_rx_read_buf(uint8_t *buf, uint16_t len);

/* Check if CPS ring buffer has data. */
uint16_t uart_cps_rx_available(void);

/* Read one byte from CPS ring buffer. Returns -1 if empty. */
int16_t uart_cps_rx_read(void);

/* Read up to len bytes from CPS ring buffer. */
uint16_t uart_cps_rx_read_buf(uint8_t *buf, uint16_t len);

/* Flush (discard) all data in a ring buffer */
void uart_bt_rx_flush(void);
void uart_cps_rx_flush(void);

/* Legacy polling API (still useful for simple single-byte checks) */
uint8_t uart_recv_byte(USART_TypeDef *uart);
uint8_t uart_data_available(USART_TypeDef *uart);

#endif /* DRIVERS_UART_H */
