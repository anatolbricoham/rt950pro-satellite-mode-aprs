/*
 * spi.c - SPI driver for the RT-950 Pro
 *
 * SPI1: NOT used for BK4829 (BK4829 uses GPIOE bit-bang, see bk4829.c)
 *   SPI1 peripheral (0x40013000) has ZERO references in V0.27 binary.
 *   PA7 is the BK4829 RF scan latch (SET/CLR @ 0x800DB08), NOT SPI1_MOSI.
 *
 * SPI2: External SPI flash (V0.27 SPI2_Init @ fw 0x08017000)
 *   Base: 0x40003800, CS: PB12 (GPIOB 0x40010C00, mask 0x1000)
 *   PB13 = SCK, PB14 = MISO, PB15 = MOSI
 *
 *   OEM SPI2 config: Mode 0 (CPOL=0, CPHA=0)
 *     Master mode, APB1/4 prescaler (MDIV=001)
 *     MSB-first, 8-bit frame, Software NSS (SWCSEN+SWCSIL)
 *
 *   Flash commands (verified V0.27 addresses):
 *     0x06 WREN          @ fw 0x08024278 (2 callers: erase + page_program)
 *     0x03 READ          @ fw 0x0802407C (3-byte addr MSB-first, byte loop)
 *     0x02 PAGE_PROGRAM  @ fw 0x0802418C (WREN first, busy-wait after)
 *     0x20 SECTOR_ERASE  @ fw 0x08023FBC (4K, 9 call sites - only erase used)
 *     0x52 BLOCK_ERASE_32K @ fw 0x08023E7C (0 call sites - dead code)
 *     0xC7 CHIP_ERASE    @ fw 0x08023F60 (0 call sites - dead code)
 *     0x05 RDSR1          @ fw 0x080240E4 (arg=1 for SR1, arg=2 for SR2/0x35)
 *   Busy poll: SR1 bit 0 check @ fw 0x08023E68
 *   Page boundary: caller manages 256B alignment (no check in OEM page_program)
 */

#include "drivers/spi.h"
#include "drivers/gpio.h"
#include "rt950_pinmap.h"

extern void delay_ms(uint32_t ms);

/* SPI flash command opcodes */
#define FLASH_CMD_WRITE_ENABLE  0x06
#define FLASH_CMD_READ_STATUS   0x05
#define FLASH_CMD_READ_DATA     0x03
#define FLASH_CMD_PAGE_PROGRAM  0x02
#define FLASH_CMD_ERASE_4K      0x20
#define FLASH_CMD_ERASE_32K     0x52
#define FLASH_CMD_ERASE_64K     0xD8
#define FLASH_CMD_READ_JEDEC_ID 0x9F

/* Status register bit 0 = Write In Progress */
#define FLASH_SR_WIP            0x01

/* ========================================================================
 *  SPI1 - NOT USED on the RT-950 Pro
 *
 *  SPI1 peripheral (0x40013000) has ZERO references in the V0.27 OEM binary.
 *  Both BK4829 chips use GPIOE bit-bang SPI (see bk4829.c).
 *
 *  WARNING: PA5/PA6/PA7 PCB connections are UNKNOWN. Do NOT configure
 *  them as AF outputs - risk of shorting to unknown traces.
 *  PA7 is the BK4829 RF scan latch (BINARY VERIFIED @ 0x800DB08).
 * ======================================================================== */

void spi1_init(void)
{
    /* NO-OP: SPI1 is not used. PA5/PA6/PA7 left in reset state.
     * PA7 is confirmed as keypad latch - must not be reconfigured. */
}

uint8_t spi_xfer_byte(uint8_t tx)
{
    while (!(SPI1->SR & SPI_SR_TXE))
        ;
    SPI1->DR = tx;
    while (!(SPI1->SR & SPI_SR_RXNE))
        ;
    return (uint8_t)(SPI1->DR & 0xFF);
}

/* SPI1 CS helpers (stub - SPI1 not used for BK4829 on RT-950 Pro) */
void spi_cs_select(void)
{
    /* No dedicated SPI1 CS on RT-950 Pro */
}

void spi_cs_deselect(void)
{
    /* No dedicated SPI1 CS on RT-950 Pro */
}

/* ========================================================================
 *  SPI2 - External SPI flash
 *
 *  Initialization and transfer routines for the SPI2 peripheral
 *  connected to the external flash chip.
 * ======================================================================== */

/* SPI2 full-duplex single-byte transfer */
static uint8_t spi2_xfer_byte(uint8_t tx)
{
    while (!(SPI2->SR & SPI_SR_TXE))
        ;
    SPI2->DR = tx;
    while (!(SPI2->SR & SPI_SR_RXNE))
        ;
    return (uint8_t)(SPI2->DR & 0xFF);
}

static inline void flash_cs_low(void)
{
    gpio_clear_pin(FLASH_CS_PORT, FLASH_CS_PIN);
}

static inline void flash_cs_high(void)
{
    gpio_set_pin(FLASH_CS_PORT, FLASH_CS_PIN);
}

/* Send 3-byte address (MSB first), matching V0.27 firmware sequence */
static void flash_send_addr(uint32_t addr)
{
    spi2_xfer_byte((uint8_t)(addr >> 16));
    spi2_xfer_byte((uint8_t)(addr >> 8));
    spi2_xfer_byte((uint8_t)(addr));
}

/* Send Write Enable (0x06) command */
static void flash_write_enable(void)
{
    flash_cs_low();
    spi2_xfer_byte(FLASH_CMD_WRITE_ENABLE);
    flash_cs_high();
}

void spi2_init(void)
{
    /* Enable SPI2 clock on APB1 */
    CRM->APB1EN |= CRM_APB1EN_SPI2EN;

    /* Enable GPIOB clock (APB2) */
    CRM->APB2EN |= CRM_APB2EN_IOPBEN;

    /* Configure SPI2 pins:
     *   PB13 = SCK  - AF push-pull, 50 MHz
     *   PB14 = MISO - floating input
     *   PB15 = MOSI - AF push-pull, 50 MHz
     */
    gpio_config_pin(GPIOB, GPIO_PIN_13, GPIO_MODE_OUT_50MHZ, GPIO_CNF_AF_PP);
    gpio_config_pin(GPIOB, GPIO_PIN_14, GPIO_MODE_INPUT,     GPIO_CNF_FLOATING);
    gpio_config_pin(GPIOB, GPIO_PIN_15, GPIO_MODE_OUT_50MHZ, GPIO_CNF_AF_PP);

    /* Flash CS (PB12) - manual GPIO, push-pull output, idle high */
    gpio_config_pin(FLASH_CS_PORT, FLASH_CS_PIN,
                    GPIO_MODE_OUT_50MHZ, GPIO_CNF_PP);
    flash_cs_high();

    /* Configure SPI2:
     *   Master mode, 8-bit, Mode 0 (CPOL=0, CPHA=0) - matches OEM
     *   Software slave management (SSM + SSI)
     *   Baud rate = APB1 / 4
     *
     *   OEM SPI2_Init @ 0x08014000: Mode 0 via HAL config struct.
     */
    SPI2->CR1 = SPI_CR1_MSTR
              | SPI_CR1_SSM
              | SPI_CR1_SSI
              | (0x1UL << 3);      /* BR[2:0] = 001 -> /4 */

    SPI2->CR1 |= SPI_CR1_SPE;
}

/* ========================================================================
 *  spi_flash_wait_busy - Poll status register until WIP bit clears.
 *
 *  Matches V0.27 firmware busy-wait after erase/program commands.
 * ======================================================================== */

void spi_flash_wait_busy(void)
{
    flash_cs_low();
    spi2_xfer_byte(FLASH_CMD_READ_STATUS);
    while (spi2_xfer_byte(0xFF) & FLASH_SR_WIP)
        ;
    flash_cs_high();
}

/* ========================================================================
 *  spi_flash_read_id - Read 3-byte JEDEC ID (command 0x9F).
 *
 *  Returns (manufacturer << 16) | (type << 8) | capacity.
 * ======================================================================== */

uint32_t spi_flash_read_id(void)
{
    flash_cs_low();
    spi2_xfer_byte(FLASH_CMD_READ_JEDEC_ID);
    uint32_t id  = (uint32_t)spi2_xfer_byte(0xFF) << 16;
    id          |= (uint32_t)spi2_xfer_byte(0xFF) << 8;
    id          |= (uint32_t)spi2_xfer_byte(0xFF);
    flash_cs_high();
    return id;
}

/* ========================================================================
 *  Flash erase commands
 *
 *  Sequence from V0.27 firmware Flash_SectorErase4K (0x08023FBC):
 *    1. Write Enable (0x06)
 *    2. CS low, 1 ms delay
 *    3. Erase command + 3-byte address (MSB first)
 *    4. CS high, 1 ms delay
 *    5. Poll status register for completion
 *
 *  OEM erase usage (V0.27 cross-reference analysis):
 *    4K  (0x20): 9 call sites - ALL erase operations use this
 *    32K (0x52): 0 call sites - DEAD CODE in OEM binary
 *    64K (0xD8): 0 call sites - DEAD CODE in OEM binary
 * ======================================================================== */

static void flash_erase(uint8_t cmd, uint32_t addr)
{
    flash_write_enable();

    flash_cs_low();
    delay_ms(1);
    spi2_xfer_byte(cmd);
    flash_send_addr(addr);
    flash_cs_high();
    delay_ms(1);

    spi_flash_wait_busy();
}

void spi_flash_erase_4k(uint32_t addr)
{
    flash_erase(FLASH_CMD_ERASE_4K, addr);
}

void spi_flash_erase_32k(uint32_t addr)
{
    flash_erase(FLASH_CMD_ERASE_32K, addr);
}

void spi_flash_erase_64k(uint32_t addr)
{
    flash_erase(FLASH_CMD_ERASE_64K, addr);
}

/* ========================================================================
 *  spi_flash_read - Read data from flash (command 0x03).
 *
 *  Send 0x03 + 3-byte address, then clock out len bytes.
 * ======================================================================== */

void spi_flash_read(uint32_t addr, uint8_t *buf, uint16_t len)
{
    flash_cs_low();
    spi2_xfer_byte(FLASH_CMD_READ_DATA);
    flash_send_addr(addr);
    for (uint16_t i = 0; i < len; i++)
        buf[i] = spi2_xfer_byte(0xFF);
    flash_cs_high();
}

/* ========================================================================
 *  spi_flash_write_page - Program up to 256 bytes (command 0x02).
 *
 *  Sequence: Write Enable -> CS low -> 0x02 + addr + data -> CS high -> wait.
 *  Caller must ensure len <= 256 and data does not cross a page boundary.
 * ======================================================================== */

void spi_flash_write_page(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    if (len == 0 || len > 256)
        return;

    flash_write_enable();

    flash_cs_low();
    spi2_xfer_byte(FLASH_CMD_PAGE_PROGRAM);
    flash_send_addr(addr);
    for (uint16_t i = 0; i < len; i++)
        spi2_xfer_byte(buf[i]);
    flash_cs_high();

    spi_flash_wait_busy();
}
