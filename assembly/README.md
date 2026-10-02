# V0.27 Disassembly Reference

Extracted from `RT_950Pro_V0.27_decrypted.bin` (380,316 bytes) using radare2.
ARM Thumb-2 (Cortex-M4F), AT32F403A MCU.

## Files

| File | Size | Description |
|------|------|-------------|
| `00_full_linear.asm` | 13 MB (~216K lines) | Complete linear disassembly of application firmware |
| `17_bootloader.asm` | 283 KB (~4.7K lines) | OEM bootloader (0x08000000-0x08002FFF, 12 KB) |

## Addressing

The correct flash base is **`0x08000000`**. The full linear disassembly was
generated with `-m 0x08003000` (addresses are +0x3000 too high). The
bootloader file uses the correct base.

To open the binary at the correct base in radare2:
```
r2 -a arm -b 16 -m 0x08000000 RT_950Pro_V0.27_decrypted.bin
```

## Function Map

All addresses are flash addresses using base 0x08000000. Verified against
the V0.27 binary. Most produce valid Thumb function prologues (push).

### BK4829 RF Transceiver

| Address | Function | C Source |
|---------|----------|----------|
| `0x08007144` | BK4829_Init | bk4829.c: bk4829_init() |
| `0x0800A7CC` | RF_Set_Frequency | bk4829.c: bk4829_set_frequency() |

### Radio Control

| Address | Function | C Source |
|---------|----------|----------|
| `0x0801A268` | PTT_Relay_Select | radio.c: radio_ptt_on/off() |
| `0x0801B908` | Band_Relay_ShiftIn | radio.c: (relay shift register) |
| `0x0801BFC4` | Band_Relay_Write1 | radio.c: (relay GPIO sequence) |
| `0x0801C090` | Band_Relay_Write2 | radio.c: (relay GPIO sequence) |

### APRS Modem

| Address | Function | C Source |
|---------|----------|----------|
| `0x08008C2C` | APRS_Frame_Builder | aprs.c: aprs_build_frame() |
| `0x0800E920` | APRS_SetPTT | aprs.c: aprs_tx_start() |
| `0x0800E94C` | APRS_SendCommand11 | aprs.c: (BK4829 FSK cmd) |
| `0x0800E9C4` | APRS_Modem_CheckReady | aprs.c: aprs_poll() |
| `0x0800E9F0` | APRS_Modem_ReadStatus | aprs.c: (status register read) |
| `0x0800EA78` | APRS_Modem_StartTX | aprs.c: aprs_tx_start() |

### GPS

| Address | Function | C Source |
|---------|----------|----------|
| `0x08009FB4` | GPS_Parse_NMEA | gps.c: gps_parse_nmea() |
| `0x08013B20` | GPS_USART3_Init | uart.c: uart_gps_init() |

### LCD Display

| Address | Function | C Source |
|---------|----------|----------|
| `0x08003808` | Display_BufferFlush | display.c: display_update() |
| `0x0800C30C` | ToneGraphic_Draw | display.c: (tone bar render) |
| `0x080121E0` | LCD_BlitRectangle | lcd.c: lcd_fill_rect() |
| `0x08012298` | LCD_PanelReset | lcd.c: lcd_panel_reset() |
| `0x0801946E` | LCD_SetWindow | lcd.c: lcd_set_window() |
| `0x08023604` | LCD_CommandPacket | lcd.c: (multi-byte cmd) |
| `0x080267B8` | LCD_WriteCommand | lcd.c: lcd_write_command() |
| `0x08026818` | LCD_WriteData | lcd.c: lcd_write_data() |
| `0x080268F8` | LCD_BlitHelper | lcd.c: (bulk pixel write) |
| `0x0802694C` | LCD_GRAMWrite | lcd.c: lcd_bulk_transfer() |

### Keypad and Encoder

| Address | Function | C Source |
|---------|----------|----------|
| `0x0800D710` | Encoder_HandleQuadrature | encoder.c: encoder_poll() |
| `0x08012FF8` | Keypad_ScanMatrix | keypad.c: keypad_scan() |

### Channel Configuration

| Address | Function | C Source |
|---------|----------|----------|
| `0x08008370` | Channel_Load_Config | channel.c: channel_load() |

### SPI Flash

| Address | Function | C Source |
|---------|----------|----------|
| `0x08007358` | Flash_ReadWrite_Block | spi_flash.c: spi_flash_read/write() |
| `0x08020E7C` | SPI_FlashErase32K | spi_flash.c: spi_flash_erase_32k() |
| `0x08020EEC` | SPI_FlashErase64K | spi_flash.c: spi_flash_erase_64k() |
| `0x08020FBC` | SPI_FlashErase4K | spi_flash.c: spi_flash_erase_4k() |
| `0x08021028` | spi_xfer_byte | spi_flash.c: spi_xfer() |

### ADC / DAC / Audio

| Address | Function | C Source |
|---------|----------|----------|
| `0x080031D2` | ADC_ReadResult | power.c: power_read_battery_raw() |
| `0x080031D8` | ADC_WaitReady | (inline in ADC helpers) |
| `0x0800323C` | ADC_StartConversion | power.c: (ADC trigger) |
| `0x0800A508` | DAC_ChannelGate | audio.c: audio_dac_enable() |
| `0x0800A528` | DAC_BufferGate | audio.c: (DAC buffer ctrl) |
| `0x0800A548` | DAC_WaveformWrite | audio.c: audio_tone_play() |
| `0x0800D114` | AudioDMA_Trigger | audio.c: audio_dma_start() |
| `0x08013820` | ADC_Read_PA1 | power.c: (ADC ch1 read) |
| `0x0801385C` | ADC_Read_PA0 | power.c: power_read_battery_raw() |

### GPIO and Utilities

| Address | Function | C Source |
|---------|----------|----------|
| `0x0800091C` | Util_IntToString | (library / inline) |
| `0x08001064` | lib_strncpy | (C library) |
| `0x080079E0` | Util_XOR_Checksum | spi_flash.c: (config checksum) |
| `0x0800A946` | Util_Delay_ms | system.c: delay_ms() |
| `0x080120A0` | GPIO_ConfigPin | gpio.c: gpio_config() |
| `0x08012594` | GPIO_Helper1 | gpio.c: (pin mode helper) |
| `0x0801259A` | GPIO_ReadPin | gpio.c: gpio_read_pin() |
| `0x080125AE` | GPIO_SetPin | gpio.c: gpio_set() |
| `0x080125B2` | GPIO_ClearPin | gpio.c: gpio_clear() |

### UART / Bluetooth

| Address | Function | C Source |
|---------|----------|----------|
| `0x080074EC` | Bluetooth_UART1_Init | bluetooth.c: bt_init() |

### Menu / UI

| Address | Function | C Source |
|---------|----------|----------|
| `0x0800AF68` | ToneMenu_ShowEntry | menu.c: menu_draw() |

### I2C / SI4732

| Address | Function | C Source |
|---------|----------|----------|
| `0x08003C34` | I2C_Write_Command | si4732.c: si4732_send_cmd() |

### Data Tables

| Address | Function | C Source |
|---------|----------|----------|
| `0x0802BC36` | CTCSS_TABLE | radio.c: ctcss_tones[] (50 tones) |
| `0x0802BB2A` | DCS_TABLE | radio.c: dcs_codes[] (105 codes) |

## Additional Verified Addresses

| Address | Function | C Source |
|---------|----------|----------|
| `0x0801886C` | VTOR_Relocate | startup.c: (SCB->VTOR write) |
| `0x080062EA` | Menu_Dispatch | menu.c: (CMP chain on menu IDs) |
| `0x08011ADE` | SubMenu_Dispatch | menu.c: (sub-item handler) |
| `0x08026954` | LCD_Init | lcd.c: lcd_init() |
| `0x080266C4` | LCD_BulkTransfer | lcd.c: lcd_bulk_transfer() |
| `0x0800834C` | BT_UART1_Init_Alt | bluetooth.c: (alternate ref) |
| `0x08025826` | Font_RenderLarge | font.c: font_draw_char() |
| `0x0802592C` | Font_RenderMedium | font.c: font_draw_char() |
| `0x080258A0` | Font_RenderCJK_Sm | font.c: font_draw_char() |
| `0x08025974` | Font_RenderCJK_Ti | font.c: font_draw_char() |
| `0x080147C8` | Font_RenderDisplay | font.c: font_draw_char() |
| `0x08024E52` | CRC16_CCITT | (CRC-16 poly 0x8408) |
| `0x0800D1A4` | DMA_CmdDispatcher | startup.c: DMA1_CH1_IRQHandler |
| `0x080395xx` | NVIC_Config | (NVIC enable/disable/prio) |

## OEM Memory Layout

```
Flash:  0x08000000 - 0x0805CCEB  (380,316 bytes)
        0x08000000 - 0x08002FFF  Bootloader (12 KB)
        0x08003000 - 0x0805CCEB  Application firmware

SRAM:   0x20000000 - 0x20017FFF  (96 KB)
        0x20000C28 - 0x2000E524  BK4829 driver state + buffers
        0x2000A360              DMA command struct
        0x2000A8B0 - 0x2000B066  Audio/radio working memory
        0x20010D10 - 0x20015D10  .data section (20,480 bytes)
        Stack pointer: 0x20015D10 (OEM), 0x20017BB0 (our firmware)

String Table:
        0x0805A000 - 0x0805A43C  Pinyin input (Chinese IME)
        0x0805A440 - 0x0805BC00  Menu/UI strings (~7 KB)
```
