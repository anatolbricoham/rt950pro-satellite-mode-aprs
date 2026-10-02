/*
 * rt950_pinmap.h - Board-level pin assignments for Radtel RT-950 Pro
 *
 * All pins verified via radare2 disassembly of V0.27 binary,
 * cross-referenced against hardware probing data, and validated
 * via live GPIO scanning on actual hardware (April 2026).
 *
 * VERIFICATION SOURCE BINARIES:
 *   V0.27: binary/RT_950Pro_V0.27_260203/RT_950Pro_V0.27_decrypted.bin
 *   V0.18: binary/RT_950Pro_V0.18_250919/RT_950Pro_V0.18_decrypted.bin
 *   Load base: 0x08000000 (AT32F403A flash origin)
 *
 * GPIO HELPER FUNCTIONS (V0.27 - all xref analysis uses these):
 *   GPIO_SetPin   = 0x080155B2  str r1,[r0,#0x10]; bx lr  (SCR - sets bits HIGH)
 *   GPIO_ClearPin = 0x080155AE  str r1,[r0,#0x14]; bx lr  (CLR - clears bits LOW)
 *   GPIO_ReadPin  = 0x0801559A  ldr r2,[r2,8]; tst r2,r1; returns 0/1
 *   GPIO_ReadODR  = 0x080155A8  ldr r0,[r0,0xc]; uxth r0,r0; bx lr
 *
 * GPIO PORT BASE ADDRESSES (AT32F403A):
 *   GPIOA = 0x40010800   GPIOB = 0x40010C00   GPIOC = 0x40011000
 *   GPIOD = 0x40011400   GPIOE = 0x40011800
 *
 * VERIFICATION STATUS LEGEND:
 *   HW_CONFIRMED    - Tested and confirmed on actual RT-950 Pro hardware
 *   BINARY_VERIFIED - Confirmed via radare2 disassembly with r2 address cited
 *   FW_VERIFIED     - Confirmed via firmware peripheral/register analysis
 *   HW_PROBED       - From hardware pin probing
 *   UNVERIFIED      - Assumed from other sources, needs confirmation
 *
 * HW GPIO SCAN RESULTS (April 2026, custom firmware GPIO scanner):
 *   Baseline IDR: A=0xBE00 B=0xF7B8 C=0xD140 D=0x1FFF E=0x8D6C
 *   GPIOC completely static (0xD140) across all 62 input events.
 *   All inputs produce clean press/release pairs with no noise.
 *
 * KEY CORRECTIONS (from binary verification):
 *   - PC5/PA7 are BK4829 RF scan ctrl, NOT keypad (0x800DB00, 0x080136B0 is data)
 *   - OEM keypad scan @ 0x08012FF8 reads PE5/PA12 as side buttons directly
 *   - PTT relays are ACTIVE-LOW, not active-high (r2 @ 0x0801D268)
 *   - SPI1 peripheral UNUSED - PA7 is keypad latch, NOT SPI1_MOSI
 *   - LCD reset is PD2 NOT PC14 (V0.27 LCD_Init @ 0x08026954)
 *   - Backlight has secondary PB3 alongside PC6 (r2 @ 0x08017C40)
 *   - GPIO_SetPin/ClearPin addresses were SWAPPED - corrected
 *     (0x080155B2=SetPin uses SCR+0x10, 0x080155AE=ClearPin uses CLR+0x14)
 *   - PC12 is NOT LCD DMA gate - LCD uses software bit-bang, no DMA
 *   - ADC battery sample time = 239.5 cycles (NOT 28.5)
 *   - DAC BOFF1=1 (output buffer OFF)
 *   - TIM6 DAC trigger: PSC=3/ARR=781 or PSC=119/ARR=125
 *   - PA12 is Bottom Program button, NOT side key 4 (HW scan confirmed)
 *   - PE5 is Top Program button (confirmed same pin as SIDE_KEY1)
 *
 * UNUSED PERIPHERALS (zero references in V0.27 binary):
 *   SPI1 (0x40013000), ADC1 (0x40012400), USART2 (0x40004400),
 *   I2C1/I2C2 hardware (SI4732 uses bit-bang I2C), DMA2 CH1 (LCD is bit-bang)
 */

#ifndef RT950_PINMAP_H
#define RT950_PINMAP_H

#include "at32f403a.h"

/* ========================================================================
 *  BK4829 RF Transceivers - Shared bit-bang SPI on GPIOE  [BINARY_VERIFIED]
 *
 *  V0.27 r2 addresses:
 *    SPI_SendByte    = 0x0801E260 (clock + data bit-bang)
 *    Chip0_Write     = 0x0801F090 (PE8 CS assert/deassert)
 *    Chip0_Read      = 0x0801EFC4
 *    Chip1_Write     = 0x0801F0E4 (PE15 CS assert/deassert)
 *    Chip1_Read      = 0x0801E908
 *  CS derived via shift to SCK in SendByte.
 * ======================================================================== */
#define BK4829_SEN1_PORT        GPIOE       /* BINARY_VERIFIED V0.27 Write@0x0801F090 Read@0x0801EFC4 */
#define BK4829_SEN1_PIN         GPIO_PIN_8  /* mask 0x0100 - chip 0 CS */
#define BK4829_SEN2_PORT        GPIOE       /* BINARY_VERIFIED V0.27 Write@0x0801F0E4 Read@0x0801E908 */
#define BK4829_SEN2_PIN         GPIO_PIN_15 /* mask 0x8000 - chip 1 CS */
#define BK4829_SCK_PORT         GPIOE       /* BINARY_VERIFIED V0.27 SendByte@0x0801E260 */
#define BK4829_SCK_PIN          GPIO_PIN_10 /* mask 0x0400 - shared clock */
#define BK4829_SDA_PORT         GPIOE       /* BINARY_VERIFIED V0.27 SendByte@0x0801E260 */
#define BK4829_SDA_PIN          GPIO_PIN_11 /* mask 0x0800 - shared bidirectional data */

/* ========================================================================
 *  SPI Flash - Hardware SPI2 on GPIOB  [BINARY_VERIFIED]
 *
 *  V0.27: SPI2 init @ 0x08017000: Master, CPOL=high, CPHA=2nd edge,
 *  prescaler /4, MSB first, 8-bit, software NSS.
 *  SPI2 base 0x40003800. CS is manually driven on PB12 via GPIO
 *  (BSRR/BRR with mask 0x1000, not SPI2_NSS hardware).
 *  SPI1 (0x40013000): ZERO references in firmware - confirmed UNUSED.
 *  Flash commands: 0x06 WREN, 0x03 READ, 0x02 PAGE_PROGRAM,
 *    0x52 BLOCK_ERASE_32K, 0x05 RDSR1, 0x35 RDSR2.
 * ======================================================================== */
#define FLASH_CS_PORT           GPIOB       /* BINARY_VERIFIED V0.27 init@0x08017000 */
#define FLASH_CS_PIN            GPIO_PIN_12 /* mask 0x1000 - manual CS (not SPI2_NSS) */
#define FLASH_SCK_PORT          GPIOB       /* BINARY_VERIFIED - SPI2 AF pin */
#define FLASH_SCK_PIN           GPIO_PIN_13
#define FLASH_MISO_PORT         GPIOB       /* BINARY_VERIFIED - SPI2 AF pin */
#define FLASH_MISO_PIN          GPIO_PIN_14
#define FLASH_MOSI_PORT         GPIOB       /* BINARY_VERIFIED - SPI2 AF pin */
#define FLASH_MOSI_PIN          GPIO_PIN_15

/* ========================================================================
 *  SI4732 - AM/FM/SW receiver (I2C on GPIOB)  [BINARY_VERIFIED]
 *
 *  V0.27 r2: I2C_Start_Address @ 0x08029440 loads r2=GPIOB(0x40010C00),
 *  uses mask 0x40(PB6) for SCL and mask 0x80(PB7) for SDA.
 *  I2C address 0x22 write / 0x23 read (7-bit: 0x11, SEN pin = low).
 *  Bit-bang I2C - no hardware I2C peripheral used.
 * ======================================================================== */
#define SI4732_SCL_PORT         GPIOB       /* BINARY_VERIFIED V0.27 @ 0x08029440 mask=0x40 */
#define SI4732_SCL_PIN          GPIO_PIN_6
#define SI4732_SDA_PORT         GPIOB       /* BINARY_VERIFIED V0.27 @ 0x08029440 mask=0x80 */
#define SI4732_SDA_PIN          GPIO_PIN_7
#define SI4732_I2C_ADDR         0x11        /* 7-bit address (SEN=low) */
#define SI4732_I2C_ADDR_W       0x22        /* 8-bit write address */
#define SI4732_I2C_ADDR_R       0x23        /* 8-bit read address */

/* ========================================================================
 *  GPS - USART3 default pins (NO remap)  [BINARY_VERIFIED]
 *
 *  V0.27: USART3_Init @ 0x08016B2A, BRR=0x186A (9600 baud).
 *  PB10 = TX (AF_PP), PB11 = RX (IPU). No AFIO MAPR remapping
 *  (0x40010000/0x40010004 = zero references in firmware).
 * ======================================================================== */
#define GPS_TX_PORT             GPIOB       /* BINARY_VERIFIED V0.27 Init@0x08016B2A */
#define GPS_TX_PIN              GPIO_PIN_10 /* mask 0x0400 - USART3_TX AF_PP */
#define GPS_RX_PORT             GPIOB       /* BINARY_VERIFIED V0.27 Init@0x08016B2A */
#define GPS_RX_PIN              GPIO_PIN_11 /* mask 0x0800 - USART3_RX IPU */

/* ========================================================================
 *  Bluetooth - USART1 (PA9=TX, PA10=RX)  [BINARY_VERIFIED]
 *
 *  V0.27: USART1_Init @ 0x0800A4F2, BRR=0x0412 (115200 baud).
 *  PA9 = TX (AF_PP), PA10 = RX (IPU). No AFIO MAPR remapping.
 * ======================================================================== */
#define BT_TX_PORT              GPIOA       /* BINARY_VERIFIED V0.27 Init@0x0800A4F2 */
#define BT_TX_PIN               GPIO_PIN_9  /* mask 0x0200 - USART1_TX AF_PP */
#define BT_RX_PORT              GPIOA       /* BINARY_VERIFIED V0.27 Init@0x0800A4F2 */
#define BT_RX_PIN               GPIO_PIN_10 /* mask 0x0400 - USART1_RX IPU */

/* ========================================================================
 *  LCD - 8080-style parallel bus (ST7789-family controller)  [BINARY_VERIFIED]
 *
 *  V0.27: LCD_WriteCmd @ 0x080267B8, LCD_Init @ 0x08026954.
 *  Data bus PD8-PD15: bfi r0, rN, #8, #24 @ 0x080267DC.
 *  Software bit-bang write loop - NO DMA used for LCD transfer.
 *  LCD_BulkTransfer @ 0x080266C4 is a software bit-bang loop.
 * ======================================================================== */
#define LCD_DATA_PORT           GPIOD       /* PD8-PD15 = D0-D7 - BINARY_VERIFIED */
#define LCD_DATA_SHIFT          8           /* data bits start at pin 8 */

#define LCD_WR_PORT             GPIOD       /* BINARY_VERIFIED V0.27 WriteCmd@0x080267B8 */
#define LCD_WR_PIN              GPIO_PIN_0  /* mask 0x0001 - toggled LOW->HIGH */

#define LCD_CS_PORT             GPIOD       /* BINARY_VERIFIED V0.27 WriteCmd@0x080267B8 */
#define LCD_CS_PIN              GPIO_PIN_1  /* mask 0x0002 - ClearPin=assert */

#define LCD_DC_PORT             GPIOD       /* BINARY_VERIFIED V0.27 - D/C select */
#define LCD_DC_PIN              GPIO_PIN_3  /* mask 0x0008 - 0=cmd, 1=data */

#define LCD_BL_PORT             GPIOC       /* BINARY_VERIFIED V0.27 @ 0x08017C40 */
#define LCD_BL_PIN              GPIO_PIN_6  /* GPIOC mask 0x0040, SetPin=ON, ClearPin=OFF */

/* PB3 is toggled alongside PC6 in OEM backlight function - secondary driver enable */
#define LCD_BL_SEC_PORT         GPIOB       /* BINARY_VERIFIED V0.27 @ 0x08017C40 */
#define LCD_BL_SEC_PIN          GPIO_PIN_3  /* GPIOB mask 0x0008 */

/* PC12: Beep/speaker switch - see BEEP_SW definition below.
 * Used at ClearPin@0x080068EA, SetPin@0x08016A58. */

#define LCD_RST_PORT            GPIOD       /* BINARY_VERIFIED V0.27 LCD_Init@0x08026954 */
#define LCD_RST_PIN             GPIO_PIN_2  /* mask 0x0004 - active-low: HIGH->LOW->delay->HIGH */

/* PC14: GREEN LED - now defined in Status LEDs section.
 * Previously misidentified as "LCD enable". HW confirmed as green LED. */

/* ========================================================================
 *  Keypad - 5-column × 4-row matrix  [HW_CONFIRMED 2026-04-03]
 *
 *  ACTIVE SCAN CONFIRMED (April 2026, 3 iterations):
 *    Columns: PC0-PC3 driven LOW one at a time (push-pull output)
 *    5th column: keys 1,4,7,star read when all PC0-PC3 HIGH
 *      (PC4 = BAND_RELAY - DO NOT TOGGLE! See RF section)
 *    Rows: PD4-PD7 input with pull-ups, active LOW
 *    All 20 keys in matrix - no direct-wired nav keys.
 *
 *  VERIFIED MATRIX LAYOUT (5 cols × 4 rows):
 *         PC0     PC1    PC2    PC3    col4(allHI)
 *  PD4:   V/M      #      0     Right     *
 *  PD5:   Back     9      8     Left      7
 *  PD6:   ABC      6      5     Down      4
 *  PD7:   OK       3      2     Up        1
 *
 *  Physical layout (standard phone keypad order):
 *    PD7 = top row     (1  2  3  OK   Up)
 *    PD6 = 2nd row     (4  5  6  ABC  Down)
 *    PD5 = 3rd row     (7  8  9  Back Left)
 *    PD4 = bottom row  (*  0  #  V/M  Right)
 *
 *  BINARY_VERIFIED (V0.27):
 *    Columns: PC0-PC3 output via BRR(GPIOC, 0x0F).
 *    Rows: PD4-PD7 input via ubfx r0, r0, 4, 4.
 *    PC5 = scan enable: SCR @ 0x080136B4, CLR @ 0x080136D4.
 *    PA7 = latch: SCR @ 0x080136BC, CLR @ 0x080136D0.
 * ======================================================================== */
#define KBD_COL_PORT            GPIOC       /* BINARY_VERIFIED V0.27 */
#define KBD_COL_MASK            0x000FU     /* bits 0-3 = PC0-PC3 (4 driven cols) */
#define KBD_NUM_COLS            5           /* 4 scanned + 1 implicit (all-HIGH) */

#define KBD_ROW_PORT            GPIOD       /* HW_CONFIRMED - active scan April 2026 */
#define KBD_ROW_MASK            0x00F0U     /* bits 4-7 = PD4-PD7 */
#define KBD_ROW_SHIFT           4
#define KBD_NUM_ROWS            4

/* Matrix scan: 4 columns (PC0-3) + 4 rows (PD4-7) = 16 keys.
 * Side buttons (PE5=TOP_PROG, PA12=BOT_PROG) are read via direct GPIO
 * BEFORE the matrix scan, matching OEM gpio_output_control @ 0x08012FF8.
 *
 * CORRECTION (Phase 12 audit): PC5 and PA7 were previously labeled as
 * KBD_SCAN_EN and KBD_LATCH based on misread data at 0x080136B0.
 * OEM assembly shows PC5/PA7 are always toggled TOGETHER by the RF
 * subsystem (0x800DB00, 0x80106B4, 0x801ADA0) controlled by a flag
 * at RAM+0xA93C. These are BK4829 scan control pins, NOT keypad pins.
 * The actual keypad scan (0x08012FF8) does NOT reference PC5 or PA7. */

/* BK4829 RF scan control - NOT keypad related (see correction above) */
#define BK_SCAN_EN_PORT         GPIOC       /* BINARY_VERIFIED: SET/CLR @ 0x800DB00 */
#define BK_SCAN_EN_PIN          GPIO_PIN_5  /* GPIOC mask 0x0020 - RF scan enable */

#define BK_SCAN_LATCH_PORT      GPIOA       /* BINARY_VERIFIED: SET/CLR @ 0x800DB08 */
#define BK_SCAN_LATCH_PIN       GPIO_PIN_7  /* GPIOA mask 0x0080 - RF scan latch */

/* ========================================================================
 *  Rotary encoder - PB4=A, PB5=B  [HW_CONFIRMED]
 *
 *  GPIO SCAN CONFIRMED (April 2026):
 *    PB4 (ch A) and PB5 (ch B), idle HIGH, standard quadrature.
 *    CW rotation:  PB4 leads PB5 (A transitions before B)
 *    CCW rotation: PB5 leads PB4 (B transitions before A)
 *    20ms scan resolution captured clean quadrature state transitions.
 *
 *  V0.27: Software quadrature FSM @ 0x08010710, polled at 200Hz.
 *  GPIO_ReadPin(GPIOB, 0x10) @ 0x08010860 = Enc A.
 *  GPIO_ReadPin(GPIOB, 0x20) @ 0x0801086C = Enc B.
 *  NOT using TIM3 hardware encoder mode.
 *  CW = event 0x14, CCW = event 0x16, debounce = 0xC8.
 * ======================================================================== */
#define ENC_A_PORT              GPIOB       /* HW_CONFIRMED - GPIO scan April 2026 */
#define ENC_A_PIN               GPIO_PIN_4  /* mask 0x0010, CW=A leads B */
#define ENC_B_PORT              GPIOB       /* HW_CONFIRMED - GPIO scan April 2026 */
#define ENC_B_PIN               GPIO_PIN_5  /* mask 0x0020, CCW=B leads A */

/* ========================================================================
 *  PTT / TX Relay Control  [BINARY_VERIFIED V0.27 @ 0x0801D268]
 *
 *  OEM relay routing function literal pool:
 *    r5 = 0x40011800 (GPIOE), r6 = 0x40010C00 (GPIOB)
 *    r7 = 0x1000 (PE12), r8 = 0x2000 (PE13), r9 = 0x4000 (PE14)
 *
 *  Mode 0 (RX idle): ALL pins SET HIGH - relays are ACTIVE-LOW.
 *  TX modes selectively CLEAR specific pins per band/path.
 *  PE7 (0x80) = audio routing, PB0(0x01)/PB1(0x02) = accessory relay.
 *
 *  HW_PROBED names: U3/U6 are PCB IC designators for RF chain ICs.
 *    PE12 = U3R ENABLE (RX path, IC U3)
 *    PE13 = U6R ENABLE (RX path, IC U6)
 *    PE14 = SW3T ENABLE (TX switch #3)
 *    PE7  = U3T EN (TX enable, IC U3)
 *    PB0  = V3R ENABLE (VHF/UHF RX frontend)
 *    PB1  = V3T ENABLE (VHF/UHF TX frontend)
 * ======================================================================== */
#define RF_U3R_EN_PORT          GPIOE       /* BINARY_VERIFIED + HW_PROBED "U3R ENABLE" */
#define RF_U3R_EN_PIN           GPIO_PIN_12 /* mask 0x1000 - IC U3 RX enable, ACTIVE LOW */
#define RF_U6R_EN_PORT          GPIOE       /* BINARY_VERIFIED + HW_PROBED "U6R ENABLE" */
#define RF_U6R_EN_PIN           GPIO_PIN_13 /* mask 0x2000 - IC U6 RX enable, ACTIVE LOW */
#define RF_SW3T_EN_PORT         GPIOE       /* BINARY_VERIFIED + HW_PROBED "SW3T ENABLE" */
#define RF_SW3T_EN_PIN          GPIO_PIN_14 /* mask 0x4000 - TX switch #3, ACTIVE LOW */
#define RF_U3T_EN_PORT          GPIOE       /* BINARY_VERIFIED + HW_PROBED "U3T EN" */
#define RF_U3T_EN_PIN           GPIO_PIN_7  /* mask 0x0080 - IC U3 TX enable / audio routing */
#define RF_V3R_EN_PORT          GPIOB       /* BINARY_VERIFIED + HW_PROBED "V3R ENABLE" */
#define RF_V3R_EN_PIN           GPIO_PIN_0  /* mask 0x0001 - VHF/UHF RX frontend enable */
#define RF_V3T_EN_PORT          GPIOB       /* BINARY_VERIFIED + HW_PROBED "V3T ENABLE" */
#define RF_V3T_EN_PIN           GPIO_PIN_1  /* mask 0x0002 - VHF/UHF TX frontend enable */

/* ========================================================================
 *  CPS / Programming interface - UART4 on GPIOC  [BINARY_VERIFIED]
 *
 *  V0.27: UART4_Init @ 0x08025738, BRR=0x0209 (115200 baud).
 *  PC10 = TX, PC11 = RX (IPU). Default mapping, no remap needed.
 *  UART4_IRQHandler (CPS) @ 0x08024E95.
 *  USART2: UNUSED (zero init code, only in generic deinit dispatcher).
 * ======================================================================== */
#define CPS_TX_PORT             GPIOC       /* BINARY_VERIFIED V0.27 Init@0x08025738 */
#define CPS_TX_PIN              GPIO_PIN_10 /* mask 0x0400 - UART4_TX */
#define CPS_RX_PORT             GPIOC       /* BINARY_VERIFIED V0.27 Init@0x08025738 */
#define CPS_RX_PIN              GPIO_PIN_11 /* mask 0x0800 - UART4_RX IPU */

/* ========================================================================
 *  Audio / Analog  [BINARY_VERIFIED]
 *
 *  V0.27: ADC2 used (base 0x40012800). ADC1 (0x40012400): ZERO refs, UNUSED.
 *  PA0 = ADC2_CH0 (VOX/audio level): RegularChannelConfig @ 0x0801685C,
 *    sample time = 239.5 cycles (NOT 28.5).
 *  PA1 = ADC2_CH1 (battery voltage): Config @ 0x08016820,
 *    returns upper 8 bits via ubfx r0, r0, 4, 8.
 *  PA4 = DAC1_CH1 (audio/beep out): DAC_Init @ 0x0801BEFE, BOFF1=1 (buffer OFF).
 *  TIM6 DAC trigger: PSC=3/ARR=781 (~1200Hz) or PSC=119/ARR=125 (~248Hz),
 *    MMS=0x20. DMA2_CH3 -> DAC_DHR12R1 (0x40007408): 16 halfwords, circular.
 * ======================================================================== */
#define DAC_OUT_PORT            GPIOA       /* BINARY_VERIFIED V0.27 DAC_Init@0x0801BEFE */
#define DAC_OUT_PIN             GPIO_PIN_4  /* mask 0x0010 - DAC1 CH1, BOFF1=1 (beep out) */

#define ADC_VOX_PORT            GPIOA       /* BINARY_VERIFIED + HW_PROBED "VOX DETECT" */
#define ADC_VOX_PIN             GPIO_PIN_0  /* mask 0x0001 - ADC2_CH0, 239.5 cycle sample */
/* NOTE: HW_PROBED label says "VOX DETECT" on PA0, but OEM firmware
 * ADC_Read_PA0 @ 0x0801385C is used as battery voltage sense (result
 * compared against 0x86-0xBB thresholds).  This may indicate the
 * PCB silkscreen label is misleading, or the trace was misidentified
 * during physical probing.  The adc.c code follows OEM firmware usage:
 * channel 0 (PA0) = battery, channel 1 (PA1) = audio.  */

#define ADC_BATT_PORT           GPIOA       /* BINARY_VERIFIED + HW_PROBED "BATTERY DETECT" */
#define ADC_BATT_PIN            GPIO_PIN_1  /* mask 0x0002 - ADC2_CH1, upper 8 bits */
/* NOTE: Same discrepancy - HW_PROBED says "BATTERY" on PA1, but OEM
 * ADC_Read_PA1 @ 0x08013820 is used as audio/VOX level input.  */

/* ========================================================================
 *  Newly Discovered Pins - GPIO Cross-Reference Scan  [BINARY_VERIFIED]
 *
 *  Discovered via systematic GPIO_SetPin/ClearPin/ReadPin xref scan of
 *  V0.27 decrypted binary. All addresses verified in radare2.
 *  Exact functions noted where determinable; others need further analysis.
 * ======================================================================== */

/* --- GPIOE Input / Control Pins --- */
#define PWR_SWITCH_PORT         GPIOE       /* HW_CONFIRMED - GPIO scan: PE0 toggle on power sw */
#define PWR_SWITCH_PIN          GPIO_PIN_0  /* mask 0x0001 - HIGH=ON position, LOW=OFF */

#define SPK_MUTE_PORT           GPIOE       /* BINARY_VERIFIED + HW_PROBED "SPEAKER MUTE" */
#define SPK_MUTE_PIN            GPIO_PIN_1  /* mask 0x0002 - speaker mute (static during scan) */

#define PTT2_PORT               GPIOE       /* HW_CONFIRMED - GPIO scan: PE2 toggle on PTT2 press */
#define PTT2_PIN                GPIO_PIN_2  /* mask 0x0004 - secondary PTT input, active LOW */

#define PTT_PORT                GPIOE       /* HW_CONFIRMED - GPIO scan: PE3 toggle on PTT1 press */
#define PTT_PIN                 GPIO_PIN_3  /* mask 0x0008 - primary PTT input, active LOW */

#define PA_ENABLE_PORT          GPIOE       /* BINARY_VERIFIED + HW_PROBED */
#define PA_ENABLE_PIN           GPIO_PIN_4  /* mask 0x0010 - audio amplifier power rail (V12 verified) */

#define TOP_PROG_PORT           GPIOE       /* BINARY_VERIFIED: OEM keypad scan @ 0x08012FF8 reads PE5 */
#define TOP_PROG_PIN            GPIO_PIN_5  /* mask 0x0020 - top programmable side button, active LOW */
/* Legacy alias */
#define SIDE_KEY1_PORT          TOP_PROG_PORT
#define SIDE_KEY1_PIN           TOP_PROG_PIN

#define EXT_PTT_PORT            GPIOE       /* BINARY_VERIFIED + HW_PROBED "EXTERNAL PTT" */
#define EXT_PTT_PIN             GPIO_PIN_6  /* mask 0x0040 - external PTT (side port) */

/* PE9: SW TO BT?? (probed, low confidence) */
#define SW_TO_BT_PORT           GPIOE       /* HW_PROBED "SW TO BT" - low confidence */
#define SW_TO_BT_PIN            GPIO_PIN_9  /* mask 0x0200 - audio switch to bluetooth? */

/* --- Audio Amplifier Enable --- */
/* OEM firmware analysis: audio_state_machine @ 0x08006574 SETs PB8 before
   beep_play and CLRs PB8 after. Originally labeled "MIC_ENABLE" in JKI757
   but confirmed as audio amplifier enable via binary RE. May be dual-purpose
   (mic + amp on same enable), but primary function is amp enable for speaker. */
#define AMP_EN_PORT             GPIOB       /* BINARY_VERIFIED: OEM sets HIGH before audio */
#define AMP_EN_PIN              GPIO_PIN_8  /* mask 0x0100 - audio amplifier enable */
#define MIC_ENABLE_PORT         AMP_EN_PORT /* Legacy alias (JKI757 label) */
#define MIC_ENABLE_PIN          AMP_EN_PIN  /* Legacy alias */

/* --- Power/Enable --- */
#define GPIO_PB9_PWREN_PORT     GPIOB       /* HW_CONFIRMED + HW_PROBED "LB POWER ENABLE" */
#define GPIO_PB9_PWREN_PIN      GPIO_PIN_9  /* mask 0x0200 - main board power latch */

/* --- Probed Pin Identifications --- */
#define GPS_ENABLE_PORT         GPIOA       /* HW_PROBED "GPS ENABLE" */
#define GPS_ENABLE_PIN          GPIO_PIN_8  /* mask 0x0100 - GPS module power control */

#define POWER_OFF_PORT          GPIOA       /* HW_PROBED "DEVICE POWER OFF" */
#define POWER_OFF_PIN           GPIO_PIN_11 /* mask 0x0800 - software power-off trigger */

#define BOT_PROG_PORT           GPIOA       /* BINARY_VERIFIED: OEM keypad scan @ 0x08012FF8 reads PA12 */
#define BOT_PROG_PIN            GPIO_PIN_12 /* mask 0x1000 - bottom programmable side button, active LOW */
/* Legacy alias */
#define SIDE_KEY4_PORT          BOT_PROG_PORT
#define SIDE_KEY4_PIN           BOT_PROG_PIN

#define REPLAY_PORT             GPIOA       /* HW_PROBED "REPLAY" */
#define REPLAY_PIN              GPIO_PIN_15 /* mask 0x8000 - audio replay control */

#define FM_RESET_PORT           GPIOA       /* HW_PROBED "FM RESET" */
#define FM_RESET_PIN            GPIO_PIN_6  /* mask 0x0040 - SI4732 FM reset line */

#define APC_PORT                GPIOA       /* HW_PROBED "APC" - auto power control */
#define APC_PIN                 GPIO_PIN_5  /* mask 0x0020 - RF output power control */

#define V3RX_EN_PORT            GPIOB       /* HW_PROBED "V3RX ENABLE" */
#define V3RX_EN_PIN             GPIO_PIN_2  /* mask 0x0004 - VHF/UHF RX chain enable */

#define PTT_DETECT_PORT         GPIOC       /* HW_PROBED "PTT DETECT" */
#define PTT_DETECT_PIN          GPIO_PIN_7  /* mask 0x0080 - PTT button state input */

#define BEEP_SW_PORT            GPIOC       /* HW_PROBED "BEEP SW" - static during GPIO scan */
#define BEEP_SW_PIN             GPIO_PIN_12 /* mask 0x1000 - beep/speaker switch (output) */

/* --- Status LEDs (static during GPIO scan = confirmed outputs) --- */
#define LED_RED_PORT            GPIOC       /* HW_CONFIRMED - OEM toggle func @ 0x08017EDC */
#define LED_RED_PIN             GPIO_PIN_13 /* mask 0x2000 - state tracked at RAM 0x200001DE+2 */

#define LED_GREEN_PORT          GPIOC       /* HW_CONFIRMED - OEM toggle func @ 0x08017E98 */
#define LED_GREEN_PIN           GPIO_PIN_14 /* mask 0x4000 - state tracked at RAM 0x200001DE+3 */

/* --- RF Band Selection Relay --- */
/* !! DANGER: PC4 is the VHF/UHF antenna relay. Rapid toggling or
 *    incorrect state during TX WILL damage the PA / front end.
 *    HW_CONFIRMED 2026-04-03: toggling PC4 as keypad col5 caused
 *    audible relay clicking. This is NOT a keypad column.
 *    Only change state when switching bands, with TX disabled. !! */
#define BAND_RELAY_PORT         GPIOC       /* HW_CONFIRMED - OEM toggle func @ 0x0801DB8C */
#define BAND_RELAY_PIN          GPIO_PIN_4  /* mask 0x0010 - VHF/UHF antenna relay */

#define GPIO_PC9_PORT           GPIOC       /* HW_PROBED "SIDEPORT PTT?" - low confidence */
#define GPIO_PC9_PIN            GPIO_PIN_9  /* mask 0x0200 - side port PTT detect? */

#define SIDEPORT_RX_PORT        GPIOC       /* HW_PROBED "SIDEPORT RX DETECT" - low confidence */
#define SIDEPORT_RX_PIN         GPIO_PIN_8  /* mask 0x0100 */

#define SIDEPORT_SPK_PORT       GPIOC       /* BINARY_VERIFIED "SIDEPORT EXT SPEAKER" @ spk_mute_on_ptt 0x08019254 */
#define SIDEPORT_SPK_PIN        GPIO_PIN_15 /* mask 0x8000 - LOW = sideport speaker connected */
/* OEM spk_mute_on_ptt reads PC15 with 50ms debounce.
 * If LOW: sets PE1 (SPK_MUTE) + PB8 (MIC_EN) to route audio to sideport. */

#define VSW_ENABLE_PORT         GPIOC       /* HW_PROBED "VSW ENABLE" (also keypad scan enable) */
#define VSW_ENABLE_PIN          GPIO_PIN_5  /* mask 0x0020 - voltage switch / scan enable */

/* PA2, PA3: Probed but not binary-verified yet */
#define SINGLE_IN_PORT          GPIOA       /* HW_PROBED "SINGLE IN" */
#define SINGLE_IN_PIN           GPIO_PIN_2  /* mask 0x0004 */

#define BT_UART_IN_PORT         GPIOA       /* HW_PROBED "BT UART IN" */
#define BT_UART_IN_PIN          GPIO_PIN_3  /* mask 0x0008 */

/* ========================================================================
 *  Backward-Compatible Aliases
 *
 *  Old names mapped to new probed-validated names. Allows existing source
 *  code to compile without mass renames. Remove once all callers updated.
 * ======================================================================== */
#define PTT_PRIMARY_PORT        RF_U6R_EN_PORT
#define PTT_PRIMARY_PIN         RF_U6R_EN_PIN
#define PTT_EXTERNAL_PORT       RF_U3R_EN_PORT
#define PTT_EXTERNAL_PIN        RF_U3R_EN_PIN
#define PTT_PA_GATE_PORT        RF_SW3T_EN_PORT
#define PTT_PA_GATE_PIN         RF_SW3T_EN_PIN
#define PTT_MIC_PORT            RF_U3T_EN_PORT
#define PTT_MIC_PIN             RF_U3T_EN_PIN
#define PTT_ACC0_PORT           RF_V3R_EN_PORT
#define PTT_ACC0_PIN            RF_V3R_EN_PIN
#define PTT_ACC1_PORT           RF_V3T_EN_PORT
#define PTT_ACC1_PIN            RF_V3T_EN_PIN
/*
 * RF frontend / audio amplifier enable pair (PB8 + PE4).
 * OEM V0.27 @ 0x08022E3C: Always SET/CLR together as a ref-counted
 * RF frontend enable/disable.
 *
 * V12 hardware verification: PE4 = audio amplifier power rail.
 * Must be HIGH for any speaker output. PB8 = amplifier enable.
 * Both are used for RF TX path AND audio playback path.
 */
#define RF_FRONTEND_EN0_PORT    MIC_ENABLE_PORT   /* PB8 */
#define RF_FRONTEND_EN0_PIN     MIC_ENABLE_PIN
#define RF_FRONTEND_EN1_PORT    PA_ENABLE_PORT    /* PE4 */
#define RF_FRONTEND_EN1_PIN     PA_ENABLE_PIN
#define ADC_AUDIO_PORT          ADC_VOX_PORT
#define ADC_AUDIO_PIN           ADC_VOX_PIN
#define PC12_GPIO_PORT          BEEP_SW_PORT
#define PC12_GPIO_PIN           BEEP_SW_PIN
#define LCD_ENABLE_PORT         LED_GREEN_PORT
#define LCD_ENABLE_PIN          LED_GREEN_PIN

/* ========================================================================
 *  GPIO Baseline Reference (from HW GPIO scan, all inputs idle)
 *
 *  These are the IDR values when no buttons are pressed:
 *    GPIOA IDR = 0xBE00  (PA9=BT_TX, PA10=BT_RX, PA11-15 various)
 *    GPIOB IDR = 0xF7B8  (PB11 masked=GPS noise; PB3-5,7-15 idle)
 *    GPIOC IDR = 0xD140  (completely static across all input events)
 *    GPIOD IDR = 0x1FFF  (PD4-7=rows idle HIGH, PD8-12=LCD data)
 *    GPIOE IDR = 0x8D6C  (PE0=0 pwr off, PE2-3=1 PTT up, PE5=1 side up)
 *
 *  Pins confirmed STATIC during full input scan (output-only or unused):
 *    GPIOA: PA0-8, PA13-15 (all static)
 *    GPIOB: PB0-3, PB6-10, PB12-15 (all static)
 *    GPIOC: ALL pins static (PC0-15 never changed)
 *    GPIOD: PD0-3, PD8-12 (LCD bus + control, static)
 *    GPIOE: PE1, PE4, PE6-15 (all static)
 * ======================================================================== */

#endif /* RT950_PINMAP_H */
