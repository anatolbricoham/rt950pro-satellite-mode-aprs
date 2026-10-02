# RT-950 Pro Bootloader Analysis

Complete reverse engineering of the OEM bootloader extracted from V0.27 firmware.

**Binary**: `binary/rt950pro_bootloader.bin` (12,288 bytes / 12 KB)
**Flash region**: 0x08000000 - 0x08002FFF
**Disassembly**: `assembly/17_bootloader.asm`

---

## Memory Map

```
0x08000000 +-------------------+
           | Vector Table      |  192 bytes (48 entries)
0x080000C0 +-------------------+
           | Infinite loops    |  Unused vector stubs (b .)
0x08000184 +-------------------+
           | Code              |  ~9 KB bootloader logic
0x080022FF +-------------------+
           | Literal pools     |  Constants, addresses
0x08002CB4 +-------------------+
           | C init table      |  .data/.bss copy descriptors
0x08002D68 +-------------------+
           | Initialized data  |  Copied to SRAM at startup
0x08002E00 +-------------------+
           | Model data        |  12 bytes XOR-encoded identity
0x08003000 +-------------------+
           | Application start |  OEM firmware begins here
```

---

## Vector Table

| # | Handler | Address | Purpose |
|---|---------|---------|---------|
| 0 | Initial SP | 0x200008B8 | Main stack pointer (2,232 bytes) |
| 1 | Reset | 0x08000191 | Entry point (Thumb) |
| 2 | NMI | 0x080017DF | Non-maskable interrupt |
| 3 | HardFault | 0x08001391 | Hard fault handler |
| 4 | MemManage | 0x080017DD | Memory management fault |
| 5 | BusFault | 0x0800040B | Bus fault - shares code with checksum |
| 6 | UsageFault | 0x08002149 | Usage fault handler |
| 11 | SVCall | 0x08001AF3 | Supervisor call |
| 12 | DebugMon | 0x080007D9 | Debug monitor |
| 14 | PendSV | 0x080018B9 | Pending supervisor |
| 15 | SysTick | 0x08001E4D | System tick timer |
| 16-47 | IRQ0-31 | 0x080001AB | All peripheral IRQs - single default handler |

All 32 peripheral IRQ vectors point to 0x080001AB (default handler / infinite loop).
Reserved entries (7-10, 13) are zero as per ARM Cortex-M4 spec.

---

## Boot Sequence

### 1. Reset Handler (0x08000190)

```
Reset_Handler:
    ldr  r0, =SystemInit        ; 0x08001E91 (Thumb)
    blx  r0                     ; Call SystemInit
    ldr  r0, =_start            ; 0x0800017D (Thumb)
    bx   r0                     ; Jump to C startup
```

### 2. SystemInit (0x08001E90)

Configures minimum hardware for bootloader operation:

1. **Enable FPU** - Sets CP10/CP11 full access in CPACR (0xE000ED88)
2. **Reset RCC** - At 0x40021000:
   - Enable HSI oscillator (CR bit 0)
   - Clear PLL configuration (CFGR)
   - Disable clock security, PLL
   - Clear interrupt flags
3. **Configure PLL** - Calls clock_setup (0x08001AF4):
   - Wait for HSI ready (CR bit 1)
   - Configure CFGR: PLL source = HSI/2, PLL multiplier
   - Sets AHB/APB1/APB2 prescalers
   - Enable PLL, wait for PLL ready
   - Switch system clock to PLL
4. **Set VTOR** - Writes 0x08000000 to SCB_VTOR (0xE000ED08)

### 3. C Runtime Init (0x0800017C)

```
_start:
    ldr.w sp, =0x200008B8       ; Reinitialize stack pointer
    bl    __libc_init_array     ; Process C init table
    bl    main                  ; Jump to bootloader main
```

The init table at 0x08002CB4 contains two entries:
- Copy 0x34 bytes from flash 0x08002D68 to SRAM 0x20000000 (.data)
- Copy 0x884 bytes from flash 0x08002D9C to SRAM 0x20000034 (.bss/init)

### 4. Bootloader Main (0x080022A0)

```c
void main(void) {
    gpio_init();                    // 0x08001880 - Configure GPIO pins
    lcd_init();                     // 0x080003F8 - Initialize ST7789V LCD
    uart_init();                    // 0x0800214C - Configure UART4 at 115200
    __enable_irq();                 // CPSIE I

    if (check_update_button()) {    // 0x0800049C
        uart_update_mode();         // 0x08000224 - Enter UART firmware update
    }

    if (check_spi_firmware()) {     // 0x08000654 - Check SPI flash for FW
        if (verify_spi_firmware()) {// 0x08000640 - Validate stored firmware
            flash_from_spi();       // 0x08002D00 - Copy SPI -> internal flash
            clear_spi_flag();       // 0x080006F4 - Erase SPI update marker
        }
    }

    validate_and_jump_to_app();     // SP validation + jump
}
```

---

## Application Validation and Jump

The bootloader validates the application at 0x08003000 before jumping:

```c
// At 0x080022DC in bootloader
uint32_t app_sp = *(uint32_t*)0x08003000;    // App's initial SP
uint32_t app_reset = *(uint32_t*)0x08003004; // App's Reset_Handler

if ((app_sp & 0x2FFE0000) == 0x20000000) {
    // Valid SP - points into SRAM (0x20000000 - 0x2001FFFF)
    __disable_irq();                // CPSID I
    *(uint32_t*)0x20000004 = app_reset; // Store reset vector
    *(uint32_t*)0x20000000 = app_reset; // (redundant store)
    __set_MSP(app_sp);             // Set main stack pointer
    ((void(*)(void))app_reset)();  // Jump to application
}
// If invalid: return 0 (stays in bootloader forever)
```

**Key insight**: The SP validation mask 0x2FFE0000 accepts any SP in the range
0x20000000 - 0x2001FFFF (128 KB SRAM). There is **no CRC check**, **no
signature verification**, and **no encryption validation**. Any binary with a
valid SP at offset 0 can be booted.

---

## Update Mechanisms

### UART Update Mode (0x08000224)

Triggered when specific GPIO pins are held during boot:

1. **Button check** (0x0800049C):
   - Reads GPIOB pin 12 (0x40010800, bit 0x1000) - must be LOW
   - Reads GPIOE pin 5 (0x40011800, bit 0x20) - must be LOW
   - Both pins checked twice with LCD init between (debounce)
   - If both LOW: enters update mode, returns 1

2. **UART protocol**:
   - GPIOD configured for UART4 (0x40011000, 0x40004C00)
   - Baud rate: 115,200
   - Waits for command bytes from host
   - Command dispatch (by byte value):
     - 0x02: Model identification (sends "RT-950" + comparison)
     - 0x03: Firmware data transfer
     - 0x04: Flash programming
     - 0x0A: Acknowledge / version info
     - 0x45 ('E'): Erase flash command
   - LCD displays status: "UPDATE", " Upgrading... ", " Update Error! ",
     "Update Success!", "Firmware Error!"

3. **Programming flow**:
   - Receives firmware data in blocks via UART
   - CRC-CCITT verification (poly 0x1021) over received data
   - Programs to internal flash starting at 0x08003000
   - Erases sectors as needed (2 KB sectors on AT32F403A)

### SPI Flash Update (0x08000654)

For over-the-air or CPS-initiated updates stored in external W25Q16:

1. **Header check** at SPI address 0x300000 (8 bytes):
   - Bytes [0:2] = 0xA55A (magic marker)
   - Bytes [6:8] != 0xFFFF (not erased)
   - Bytes [2:6] = firmware length (must be < 0xFD000 = 1,036,288)

2. **Version check** at SPI address 0x300700 (8 bytes):
   - Bytes [0:3] = "Ver" (0x56, 0x65, 0x72) - version string marker

3. **CRC verification** (0x0800040C):
   - CRC-CCITT (poly 0x1021, init 0x0000)
   - Reads firmware data from SPI via SPI2 (0x40011400)
   - Uses GPIOC pin 12 (0x40010C00) as SPI chip select
   - SPI read command: 0x03 (standard read) + 24-bit address

4. **Flash programming** (0x080013A8):
   - Source: SPI flash at 0x300100
   - Destination: Internal flash at 0x08003000
   - Block size: 1,024 bytes (0x400)
   - **Decryption**: XOR with 16-byte rotating key at SRAM 0x20000034
     - Bytes 0x00 and 0xFF are NOT decrypted (preserved as-is)
     - Key XOR is applied byte-by-byte with `key[i % 16]`
   - Sector erase on 2 KB boundaries (bit 21 of offset)
   - Writes via FLASH controller at 0x40022000

5. **Model validation** (0x08000654):
   - Reads 32 bytes from SPI flash at 0x080037E0 (model string area)
   - Compares against "RT-950" stored at 0x080003A4 in bootloader
   - Mismatch: displays status code 4 on LCD, aborts

6. **W25Q16 identification** (alternative path in 0x0800049C):
   - Checks for JEDEC bytes: 0xAA 0x42 0x00 0x00 0x00 0x00 0x55 0xEB
   - This identifies the W25Q16 flash chip for SPI update readiness

---

## Hardware Peripherals Used

| Peripheral | Address | Purpose |
|------------|---------|---------|
| GPIOB | 0x40010800 | Update button (PB12) |
| GPIOC | 0x40010C00 | SPI2 chip select (PC12) |
| GPIOD | 0x40011000 | UART4 TX/RX, LCD data |
| SPI2 | 0x40011400 | External W25Q16 flash |
| GPIOE | 0x40011800 | Update button (PE5) |
| RCC | 0x40021000 | Clock configuration |
| FLASH | 0x40022000 | Internal flash controller |
| UART4 | 0x40004C00 | UART update interface (115200 baud) |
| SCB | 0xE000ED00 | VTOR, CPACR (FPU enable) |

---

## LCD Display in Bootloader

The bootloader contains a stripped-down LCD driver for status display:

- **LCD init** (0x08001588): ST7789V initialization sequence
  - SPI2 for data, GPIO for control (same as app firmware)
  - Commands: 0x11 (sleep out), 0xB2, 0xB7, 0x36, etc.
- **Font rendering**: Two font sets embedded in bootloader
  - ASCII font at SPI 0x15C000 (char - 0x20) * 39 bytes per glyph
  - Chinese GB2312 font at SPI 0x0E0000 (multi-byte lookup)
- **Display function** (0x08000818): Shows status by code:
  - Code 0: "UPDATE" header + " Upgrading... "
  - Code 1: " Upgrading... " (progress)
  - Code 2: " Update Error! " (red 0xF800)
  - Code 3: "Update Success!" (green 0x07E0)
  - Code 4: "Firmware Error!"
- **Dual language**: English and Chinese (GB2312) strings:
  - "正在升级中..." (Upgrading...)
  - "升级失败!" (Update failed!)
  - "升级成功!" (Update success!)

---

## Model Identity and UID

### Model String (0x080003A4)

12-byte padded ASCII string used for firmware compatibility check:
```
"RT-950      " (padded with spaces to 12 bytes)
```

### MCU UID XOR (0x08002E00)

The bootloader stores 12 bytes of XOR-encoded identity data at 0x08002E00
(flash offset within bootloader region). This is referenced alongside the
MCU unique ID register at 0x1FFFF7E8 (96-bit UID).

The `fcn.08002232` function performs Chinese character font address
calculations, while `fcn.08002212` handles ASCII font lookups - both
reading glyph data from SPI flash.

---

## Implications for Custom Firmware

### What Works

1. **No security barrier** - The bootloader performs SP validation only:
   `(*(uint32_t*)0x08003000 & 0x2FFE0000) == 0x20000000`
   Any binary with a valid initial SP passes this check.

2. **Standard vector table** - The app at 0x08003000 needs:
   - Offset 0x00: Initial stack pointer (must be in SRAM 0x20000000-0x2001FFFF)
   - Offset 0x04: Reset handler address (Thumb, bit 0 set)

3. **VTOR is set by bootloader** to 0x08000000. The application must
   relocate VTOR to 0x08003000 in its own startup code.

4. **Bootloader is preserved** during OEM updates (only 0x08003000+ is
   written). Our custom firmware coexists with the OEM bootloader.

### Linker Requirements

```
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08003000, LENGTH = 1012K  /* After bootloader */
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 96K
}
```

### Forcing Update Mode

Hold **PB12** and **PE5** LOW during power-on to enter UART update mode.
This provides a recovery path if custom firmware is non-functional.

### SPI Flash Update Path

The CPS application writes firmware to SPI flash at address 0x300000 with
the header structure, then the bootloader copies it to internal flash on
next boot. This path includes BTF decryption (XOR with rotating key).

For custom firmware via CPS: create a BTF file with the appropriate
encryption key, or write directly via SWD/JTAG (bypasses bootloader).

---

## Function Reference

| Address | Size | Name | Description |
|---------|------|------|-------------|
| 0x08000190 | 8 | Reset_Handler | Calls SystemInit, jumps to _start |
| 0x0800017C | 8 | _start | Sets SP, runs C init, calls main |
| 0x08000200 | 36 | __libc_init_array | Processes C init table entries |
| 0x08000224 | 410 | uart_update_mode | UART firmware update protocol |
| 0x080003C0 | 78 | uart_send_response | Send response byte pair |
| 0x080003F8 | 434 | lcd_gpio_init | LCD GPIO pin configuration |
| 0x0800040C | 134 | crc_ccitt_spi | CRC-CCITT over SPI flash data |
| 0x0800049C | 312 | check_update_button | GPIO check + SPI header validation |
| 0x080005EC | 78 | uart_receive_packet | Receive and validate UART packet |
| 0x08000640 | 16 | check_spi_flag | Check if SPI update pending |
| 0x08000654 | 68 | check_spi_model | Verify model string in SPI flash |
| 0x080006F4 | 136 | clear_spi_update | Erase SPI update marker sectors |
| 0x08000818 | 272 | lcd_show_status | Display update status on LCD |
| 0x080009A8 | 104 | crc_verify_spi | CRC-CCITT verify from RAM buffer |
| 0x08000A1C | 172 | flash_erase_sector | Erase internal flash sector |
| 0x08000B64 | 152 | flash_write_word | Write word to internal flash |
| 0x08000CA0 | 166 | gpio_configure | Configure GPIO pin mode |
| 0x08000D48 | 1078 | flash_program_block | Program flash block with verify |
| 0x080013A8 | 222 | flash_from_spi | Decrypt + program from SPI flash |
| 0x080014EC | 156 | lcd_draw_text | Render text string on LCD |
| 0x08001588 | 478 | lcd_init_st7789 | ST7789V initialization sequence |
| 0x08001758 | 132 | lcd_fill_rect | Fill rectangle with color |
| 0x08001AF4 | 180 | clock_setup | Configure PLL and clock tree |
| 0x08001C1C | 98 | spi_flash_read | Read bytes from W25Q16 |
| 0x08001CD0 | 86 | spi_transfer_byte | SPI send/receive single byte |
| 0x08001DB0 | 230 | spi_flash_write | Write bytes to W25Q16 |
| 0x08001E90 | 84 | SystemInit | FPU, RCC reset, clock, VTOR |
| 0x08002110 | 22 | uart_gpio_config | UART4 pin configuration |
| 0x0800214C | 138 | uart_init | UART4 init at 115200 baud |
| 0x08002212 | 32 | font_ascii_addr | Calculate ASCII glyph SPI address |
| 0x08002232 | 108 | font_chinese_addr | Calculate GB2312 glyph SPI address |
| 0x080022A0 | 94 | main | Bootloader main entry point |
| 0x08002320 | 72 | lcd_block_write | Write pixel block to LCD |
| 0x08002368 | 92 | spi2_send_cmd | SPI2 send command byte |
| 0x080023C8 | 92 | spi2_send_data | SPI2 send data byte |
