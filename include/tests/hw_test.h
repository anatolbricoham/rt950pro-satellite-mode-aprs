/*
 * hw_test.h - Hardware test suite for RT-950 Pro bring-up
 *
 * Standalone tests for each peripheral.  Compile with -DHW_TEST=N
 * to select which test runs instead of the normal super-loop.
 *
 * Usage:
 *   make test TEST=1   -> blinky (backlight toggle)
 *   make test TEST=2   -> UART echo (GPS + BT loopback)
 *   make test TEST=3   -> LCD test pattern (color bars)
 *   make test TEST=4   -> BK4829 chip ID read
 *   make test TEST=5   -> SI4732 chip revision read
 *   make test TEST=6   -> SPI flash JEDEC ID read
 *   make test TEST=7   -> ADC battery + audio level
 *   make test TEST=8   -> DAC 1 kHz test tone
 *   make test TEST=9   -> Keypad + encoder scan
 *   make test TEST=10  -> GPS NMEA display
 *   make test TEST=11  -> Full system diagnostic
 */

#ifndef TESTS_HW_TEST_H
#define TESTS_HW_TEST_H

void test_blinky(void);
void test_uart_echo(void);
void test_lcd_pattern(void);
void test_bk4829_id(void);
void test_si4732_rev(void);
void test_spi_flash_id(void);
void test_adc_monitor(void);
void test_dac_tone(void);
void test_keypad_encoder(void);
void test_gps_display(void);
void test_full_diagnostic(void);

#endif /* TESTS_HW_TEST_H */
