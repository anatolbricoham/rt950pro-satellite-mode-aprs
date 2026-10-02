/*
 * dma.c - DMA driver for AT32F403A on the RT-950 Pro
 *
 * DMA usage verified from firmware disassembly:
 *   Display_BufferFlush @ 0x800037B0: DMA2 pushes pixel data from RAM
 *     (0x20000BD0) to the LCD 8080 bus via GPIOD ODR.  TIM6 is used as
 *     the DMA trigger for paced transfers.
 *   AudioDMA_Trigger @ 0x8000DCA0: DMA2 CH3 streams samples from a RAM
 *     buffer to DAC1 DHR12R1 (base 0x40007400, register at +0x08).
 *     DMA2 CH3 registers sit at 0x40020430.
 */

#include "drivers/dma.h"

/* LCD data port: GPIOD ODR (base 0x40011400 + offset 0x0C) */
#define LCD_DATA_ADDR   ((uint32_t)&GPIOD->ODR)

/* DAC channel 1, 12-bit right-aligned data holding register */
#define DAC_DHR12R1_ADDR ((uint32_t)&DAC->DHR12R1)

/* LCD DMA uses DMA2 channel 1 (arbitrary - no HW request mux needed
 * when software-triggered or TIM6 TRGO is routed externally).         */
#define LCD_DMA_CH      DMA2_CH(1)
#define LCD_DMA_CHNUM   1

/* Audio DMA uses DMA2 channel 3 - verified at 0x40020430 */
#define DAC_DMA_CH      DMA2_CH(3)
#define DAC_DMA_CHNUM   3

/* ========================================================================
 *  dma_init - Enable DMA1 and DMA2 peripheral clocks.
 *
 *  Both controllers must be clocked before any channel is configured.
 * ======================================================================== */

void dma_init(void)
{
    CRM->AHBEN |= CRM_AHBEN_DMA1EN | CRM_AHBEN_DMA2EN;
}

/* ========================================================================
 *  dma_configure - Set up a DMA channel without enabling it.
 *
 *  The channel is disabled first to allow safe reconfiguration, then
 *  CPAR, CMAR, CNDTR, and CCR are written.  The EN bit is NOT set here;
 *  call dma_start() to begin the transfer.
 * ======================================================================== */

void dma_configure(DMA_Channel_TypeDef *ch, uint32_t periph_addr,
                   uint32_t mem_addr, uint16_t count, uint32_t ccr_flags)
{
    /* Disable channel before reconfiguring */
    ch->CCR &= ~DMA_CCR_EN;

    ch->CPAR  = periph_addr;
    ch->CMAR  = mem_addr;
    ch->CNDTR = count;
    ch->CCR   = ccr_flags & ~DMA_CCR_EN;   /* Load flags, keep EN clear */
}

/* ========================================================================
 *  dma_start - Enable a previously configured DMA channel.
 * ======================================================================== */

void dma_start(DMA_Channel_TypeDef *ch)
{
    ch->CCR |= DMA_CCR_EN;
}

/* ========================================================================
 *  dma_stop - Disable a DMA channel.
 * ======================================================================== */

void dma_stop(DMA_Channel_TypeDef *ch)
{
    ch->CCR &= ~DMA_CCR_EN;
}

/* ========================================================================
 *  dma_is_complete - Check the transfer-complete flag for a channel.
 *
 *  @return 1 if TCIF is set, 0 otherwise.
 * ======================================================================== */

int dma_is_complete(DMA_TypeDef *dma, uint8_t channel)
{
    return (dma->ISR & DMA_ISR_TCIF(channel)) ? 1 : 0;
}

/* ========================================================================
 *  dma_clear_flags - Clear all interrupt flags (GIF/TCIF/HTIF/TEIF)
 *  for the specified channel.
 *
 *  Writing to IFCR is write-1-to-clear.
 * ======================================================================== */

void dma_clear_flags(DMA_TypeDef *dma, uint8_t channel)
{
    dma->IFCR = DMA_IFCR_ALL(channel);
}

/* ========================================================================
 *  dma_start_lcd_push - Push a 16-bit pixel buffer to the LCD 8080 bus.
 *
 *  Configures DMA2 channel for memory->peripheral:
 *    - 16-bit peripheral and memory width
 *    - Memory increment (walk through framebuffer)
 *    - No peripheral increment (always write to GPIOD ODR)
 *    - High priority (display is latency-sensitive)
 *
 *  Peripheral address: GPIOD ODR = 0x4001140C
 *  Verified: Display_BufferFlush @ 0x800037B0 loads 0x20000BD0 as source.
 *
 *  Note: pixel_count is clamped to 16 bits (65535 max per transfer).
 *  For larger framebuffers, the caller should issue successive transfers.
 * ======================================================================== */

void dma_start_lcd_push(const uint16_t *fb, uint32_t pixel_count)
{
    uint16_t xfer_count;

    if (pixel_count > 0xFFFFUL)
        xfer_count = 0xFFFF;
    else
        xfer_count = (uint16_t)pixel_count;

    /* Clear any pending flags on LCD DMA channel */
    dma_clear_flags(DMA2, LCD_DMA_CHNUM);

    dma_configure(LCD_DMA_CH,
                  LCD_DATA_ADDR,
                  (uint32_t)(uintptr_t)fb,
                  xfer_count,
                  DMA_CCR_DIR          /* Memory -> peripheral */
                  | DMA_CCR_MINC       /* Increment memory pointer */
                  | DMA_CCR_PSIZE_16   /* 16-bit peripheral */
                  | DMA_CCR_MSIZE_16   /* 16-bit memory */
                  | DMA_CCR_PL_HIGH);  /* High priority */

    dma_start(LCD_DMA_CH);
}

/* ========================================================================
 *  dma_start_dac_playback - Stream 16-bit audio samples to DAC1 via
 *  DMA2 CH3.
 *
 *  Configures DMA2 channel 3 for memory->peripheral:
 *    - 16-bit peripheral and memory width
 *    - Memory increment (walk through sample buffer)
 *    - No peripheral increment (always write to DAC DHR12R1)
 *    - Circular mode if @circular is non-zero (looping playback)
 *    - Medium priority
 *
 *  Peripheral address: DAC DHR12R1 = 0x40007408
 *  Verified: AudioDMA_Trigger @ 0x8000DCA0, DMA2 CH3 @ 0x40020430
 * ======================================================================== */

void dma_start_dac_playback(const uint16_t *samples, uint16_t count,
                            int circular)
{
    uint32_t flags;

    /* Clear any pending flags on DAC DMA channel */
    dma_clear_flags(DMA2, DAC_DMA_CHNUM);

    flags = DMA_CCR_DIR            /* Memory -> peripheral */
          | DMA_CCR_MINC           /* Increment memory pointer */
          | DMA_CCR_PSIZE_16       /* 16-bit peripheral */
          | DMA_CCR_MSIZE_16       /* 16-bit memory */
          | DMA_CCR_PL_MED;        /* Medium priority */

    if (circular)
        flags |= DMA_CCR_CIRC;

    dma_configure(DAC_DMA_CH,
                  DAC_DHR12R1_ADDR,
                  (uint32_t)(uintptr_t)samples,
                  count,
                  flags);

    dma_start(DAC_DMA_CH);
}
