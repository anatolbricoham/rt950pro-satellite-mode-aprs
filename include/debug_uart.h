/*
 * debug_uart.h - Compile-time debug output over UART4 (CH341 USB serial)
 *
 * Enable with -DDEBUG_UART at build time (make DEBUG=1).
 * When disabled, all debug calls compile to nothing.
 *
 * Uses polled TX only (no interrupts, no DMA) so it can run before
 * the interrupt system is fully initialized. Output goes to the same
 * PC10/PC11 UART4 that the CPS programming interface uses.
 *
 * Baud: 115200 (matches CPS and upload tool).
 */

#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#ifdef DEBUG_UART

void dbg_init(void);
void dbg_puts(const char *s);
void dbg_hex8(unsigned char v);
void dbg_hex32(unsigned long v);
void dbg_newline(void);

/* Convenience: print label + hex32 + newline */
void dbg_reg(const char *label, unsigned long val);

#else

#define dbg_init()          ((void)0)
#define dbg_puts(s)         ((void)0)
#define dbg_hex8(v)         ((void)0)
#define dbg_hex32(v)        ((void)0)
#define dbg_newline()       ((void)0)
#define dbg_reg(l, v)       ((void)0)

#endif /* DEBUG_UART */
#endif /* DEBUG_UART_H */
