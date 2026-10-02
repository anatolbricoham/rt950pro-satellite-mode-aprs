/*
 * dma.h - DMA driver for AT32F403A on the RT-950 Pro
 *
 * RT-950 DMA usage verified from firmware disassembly:
 *   Display_BufferFlush @ 0x800037B0: DMA2 pushes 16-bit pixel data to LCD
 *   AudioDMA_Trigger    @ 0x8000DCA0: DMA2 CH3 streams samples to DAC1
 */

#ifndef DRIVERS_DMA_H
#define DRIVERS_DMA_H

#include "at32f403a.h"

/* DMA CCR bit definitions ------------------------------------------- */
#define DMA_CCR_EN          (1UL << 0)
#define DMA_CCR_TCIE        (1UL << 1)   /* Transfer complete interrupt */
#define DMA_CCR_HTIE        (1UL << 2)   /* Half transfer interrupt */
#define DMA_CCR_TEIE        (1UL << 3)   /* Transfer error interrupt */
#define DMA_CCR_DIR         (1UL << 4)   /* 0=peripheral->memory, 1=memory->peripheral */
#define DMA_CCR_CIRC        (1UL << 5)   /* Circular mode */
#define DMA_CCR_PINC        (1UL << 6)   /* Peripheral increment */
#define DMA_CCR_MINC        (1UL << 7)   /* Memory increment */

#define DMA_CCR_PSIZE_8     (0UL << 8)
#define DMA_CCR_PSIZE_16    (1UL << 8)
#define DMA_CCR_PSIZE_32    (2UL << 8)

#define DMA_CCR_MSIZE_8     (0UL << 10)
#define DMA_CCR_MSIZE_16    (1UL << 10)
#define DMA_CCR_MSIZE_32    (2UL << 10)

#define DMA_CCR_PL_LOW      (0UL << 12)
#define DMA_CCR_PL_MED      (1UL << 12)
#define DMA_CCR_PL_HIGH     (2UL << 12)
#define DMA_CCR_PL_VHIGH    (3UL << 12)

#define DMA_CCR_MEM2MEM     (1UL << 14)

/* DMA ISR / IFCR flag macros ---------------------------------------- */
/* Each channel occupies 4 bits: GIF, TCIF, HTIF, TEIF.
 * Channel 1 -> bits [3:0], channel 2 -> bits [7:4], etc. */
#define DMA_ISR_GIF(ch)     (1UL << (((ch) - 1) * 4))
#define DMA_ISR_TCIF(ch)    (1UL << (((ch) - 1) * 4 + 1))
#define DMA_ISR_HTIF(ch)    (1UL << (((ch) - 1) * 4 + 2))
#define DMA_ISR_TEIF(ch)    (1UL << (((ch) - 1) * 4 + 3))

#define DMA_IFCR_GIF(ch)    (1UL << (((ch) - 1) * 4))
#define DMA_IFCR_TCIF(ch)   (1UL << (((ch) - 1) * 4 + 1))
#define DMA_IFCR_HTIF(ch)   (1UL << (((ch) - 1) * 4 + 2))
#define DMA_IFCR_TEIF(ch)   (1UL << (((ch) - 1) * 4 + 3))
#define DMA_IFCR_ALL(ch)    (0xFUL << (((ch) - 1) * 4))

/* Function declarations --------------------------------------------- */

/*
 * dma_init - Enable DMA1 and DMA2 peripheral clocks via CRM AHBEN.
 */
void dma_init(void);

/*
 * dma_configure - Set up a DMA channel (leaves it disabled).
 *
 * @param ch          DMA channel (e.g. DMA1_CH(3) or DMA2_CH(3))
 * @param periph_addr Peripheral register address
 * @param mem_addr    Memory buffer address
 * @param count       Number of data items to transfer
 * @param ccr_flags   CCR configuration (direction, size, priority, etc.)
 */
void dma_configure(DMA_Channel_TypeDef *ch, uint32_t periph_addr,
                   uint32_t mem_addr, uint16_t count, uint32_t ccr_flags);

/*
 * dma_start - Enable a previously configured DMA channel.
 */
void dma_start(DMA_Channel_TypeDef *ch);

/*
 * dma_stop - Disable a DMA channel.
 */
void dma_stop(DMA_Channel_TypeDef *ch);

/*
 * dma_is_complete - Check if the transfer-complete flag is set.
 *
 * @param dma      DMA controller (DMA1 or DMA2)
 * @param channel  Channel number (1-7 for DMA1, 1-5 for DMA2)
 * @return         1 if TCIF is set, 0 otherwise
 */
int dma_is_complete(DMA_TypeDef *dma, uint8_t channel);

/*
 * dma_clear_flags - Clear all interrupt flags for a channel.
 *
 * @param dma      DMA controller (DMA1 or DMA2)
 * @param channel  Channel number (1-7 for DMA1, 1-5 for DMA2)
 */
void dma_clear_flags(DMA_TypeDef *dma, uint8_t channel);

/*
 * dma_start_lcd_push - Push a framebuffer to the LCD via DMA2.
 *
 * Memory-to-peripheral, 16-bit, memory-increment, high priority.
 * Peripheral address = GPIOD ODR (0x4001140C), matching the 8080 bus
 * wiring verified in Display_BufferFlush @ 0x800037B0.
 *
 * @param fb           Pointer to 16-bit pixel buffer
 * @param pixel_count  Number of pixels to transfer
 */
void dma_start_lcd_push(const uint16_t *fb, uint32_t pixel_count);

/*
 * dma_start_dac_playback - Stream audio samples to DAC1 via DMA2 CH3.
 *
 * Memory-to-peripheral, 16-bit, memory-increment.
 * Peripheral address = DAC DHR12R1 (0x40007408), matching
 * AudioDMA_Trigger @ 0x8000DCA0.
 *
 * @param samples   Pointer to 16-bit sample buffer
 * @param count     Number of samples
 * @param circular  Non-zero for circular (looping) playback
 */
void dma_start_dac_playback(const uint16_t *samples, uint16_t count,
                            int circular);

#endif /* DRIVERS_DMA_H */
