/*
 * spi.h - SPI driver for the RT-950 Pro
 *
 * SPI1: NOT used - 0x40013000 has zero references in V0.27 binary.
 *       Both BK4829 chips use GPIOE bit-bang SPI (see bk4829.c).
 *       SPI1 pins (PA5/6/7) may be unconnected or repurposed.
 * SPI2: External SPI flash (PB13=SCK, PB14=MISO, PB15=MOSI, PB12=CS)
 *
 * SPI2 flash bus verified from V0.27 binary:
 *   spi_xfer_byte @ fw 0x08021028: loads peripheral at 0x40003800 = SPI2
 *   Flash CS = PB12 (GPIOB pin 12, active low)
 */

#ifndef DRIVERS_SPI_H
#define DRIVERS_SPI_H

#include "at32f403a.h"

/* SPI1 - BK4829 #1 RF transceiver ------------------------------------ */

/*
 * spi1_init - Initialize HW SPI1 for BK4829 communication.
 * Mode 0 (CPOL=0, CPHA=0), 8-bit, master, ~7.5 MHz clock.
 */
void spi1_init(void);

/*
 * spi_xfer_byte - Full-duplex SPI1 transfer of one byte.
 * Returns the byte received while transmitting tx.
 */
uint8_t spi_xfer_byte(uint8_t tx);

/*
 * spi_cs_select / spi_cs_deselect - Assert/deassert chip-select for BK4829 #1.
 */
void spi_cs_select(void);
void spi_cs_deselect(void);

/* SPI2 - External SPI flash ------------------------------------------- */

/*
 * spi2_init - Initialize HW SPI2 for external flash.
 * PB13=SCK, PB14=MISO, PB15=MOSI, PB12=CS (manual GPIO).
 * APB1 clock, Mode 0, 8-bit master.
 */
void spi2_init(void);

/*
 * spi_flash_read_id - Read JEDEC ID (0x9F command).
 * Returns 3-byte manufacturer + device ID packed into uint32_t
 * (manufacturer in bits [23:16], type in [15:8], capacity in [7:0]).
 */
uint32_t spi_flash_read_id(void);

/*
 * spi_flash_wait_busy - Poll status register (0x05) until WIP bit clears.
 */
void spi_flash_wait_busy(void);

/*
 * SPI flash erase commands (JEDEC standard opcodes via SPI2).
 */
void spi_flash_erase_4k(uint32_t addr);    /* Sector erase (0x20) */
void spi_flash_erase_32k(uint32_t addr);   /* 32K block erase (0x52) */
void spi_flash_erase_64k(uint32_t addr);   /* 64K block erase (0xD8) */

/*
 * spi_flash_read - Read data from SPI flash (0x03 command).
 */
void spi_flash_read(uint32_t addr, uint8_t *buf, uint16_t len);

/*
 * spi_flash_write_page - Write up to 256 bytes to SPI flash (0x02 command).
 * Address should be page-aligned; len <= 256.
 */
void spi_flash_write_page(uint32_t addr, const uint8_t *buf, uint16_t len);

#endif /* DRIVERS_SPI_H */
