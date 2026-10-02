; ==========================================================================
; Radtel RT-950 Pro Bootloader - Full Linear Disassembly
; ==========================================================================
;
; Source: binary/rt950pro_bootloader.bin (12,288 bytes)
; Base address: 0x08000000
; End address:  0x08002FFF
; MCU: AT32F403A (ARM Cortex-M4)
; Disassembler: radare2 (Thumb-2 mode)
;
; This bootloader occupies the first 12KB of internal flash.
; Application firmware begins at 0x08003000.
;
; Features:
;   - UART serial firmware update (PROGRAMBT9000U + UPDATE handshake)
;   - SPI flash OTA update (magic 0xA55A at SPI address 0x300000)
;   - XOR decryption of firmware data (16-byte key, skip rules)
;   - CRC-CCITT packet validation
;   - LCD status display (ST7789V)
;   - Application validation (SP check at 0x08003000)
;   - Model string verification ("RT-950      ")
;
; ==========================================================================
; UART Update Protocol
; ==========================================================================
;
; Bootloader commands (UART):
;   0x0A - Version handshake → responds "BOOTLOADER_V3"
;   0x02 - Model verification → compares to "RT-950      " (12 bytes)
;          On match: sets state[1]=1 (model verified), ACK (0x06)
;          On mismatch: error 0xE6
;   0x04 - Package count setup → requires state[1]==1
;          Extracts block count from bytes [r5+6..7] (big-endian u16)
;          Stores at state[0xC], resets write offset (state[8]=0)
;          Sets state[1]=2 (ready for data), ACK
;   0x03 - Data write → requires state[1]==2
;          Validates data length == 0x400 (1024 bytes)
;          Block number from bytes [r5+2..3] (big-endian u16)
;          Block 1: calls set_decryption_key (key = data[6..21])
;          Block 0: writes directly to flash (plaintext vectors+metadata)
;          Block ≥2: calls flash_from_spi (decrypt + write at (block-1)*0x400)
;          Updates write offset: state[8] += 0x400
;   0x45 - End update → sets countdown timer, triggers reboot
;   (NO read/download commands - write-only protocol)
;
; State machine at [0x2000000C]:
;   state[0x00] = unused
;   state[0x01] = phase: 0=idle, 1=model verified, 2=ready for data
;   state[0x04] = write offset (bytes written so far)
;   state[0x08] = next expected write address
;   state[0x0C] = total block count
;   state[0x10] = reboot countdown
;
; Packet format: [0xAA, cmd, args_hi, args_lo, len_hi, len_lo, data..., crc_hi, crc_lo, 0x55]
; Response:      [0xAA, cmd, 0x00, result, 0x00, 0x00, crc_hi, crc_lo, 0x55]
; Result codes: 0x06=ACK, 0xE1=length error, 0xE2=verify error,
;               0xE3=flash error, 0xE5=unknown cmd, 0xE6=model mismatch
;
; ==========================================================================
; Firmware Decryption Algorithm (flash_from_spi @ 0x080013A8)
; ==========================================================================
;
; C pseudocode:
;
;   int flash_from_spi(uint32_t offset, uint8_t *data) {
;       if (offset >= 0xFD000) return 0;       // bounds check
;       uint32_t flash_addr = offset + 0x08003000;  // app base
;       CPSID_I();                              // disable interrupts
;
;       // If 2KB page-aligned, erase flash page first
;       if ((offset % 2048) == 0) {
;           if (flash_page_erase(flash_addr) != 4) return 2;
;       }
;
;       // Retry counter at 0x20000018; if < 2, set to 5
;       uint32_t *retry = (uint32_t *)0x20000018;
;       if (*retry < 2) *retry = 5;
;       uint32_t max_offset = (*retry - 1) * 0x400;
;
;       // Decrypt blocks from 0x400..max_offset (skip block 0 = plaintext)
;       if (offset >= 0x400 && offset < max_offset) {
;           uint8_t *key = (uint8_t *)0x20000034;   // 16-byte key
;           uint32_t key_idx = 0;
;           for (uint32_t i = 0; i < 1024; i++) {
;               uint8_t d = data[i];
;               // FOUR skip conditions (preserve 0x00/0xFF in output):
;               if (d == 0x00) goto skip;            // plaintext 0x00
;               if (d == 0xFF) goto skip;            // plaintext 0xFF
;               if (d == key[key_idx]) goto skip;    // XOR → 0x00
;               if (d == (key[key_idx] ^ 0xFF)) goto skip; // XOR → 0xFF
;               data[i] = d ^ key[key_idx];          // DECRYPT
;           skip:
;               key_idx = (key_idx + 1) & 0x0F;      // wrap at 16
;               if (key_idx == 0) {                   // rotate key every 16 bytes
;                   for (int j = 0; j < 8; j++) {
;                       key[j]   = ROL8(key[j], 1);   // bytes 0-7: rotate left
;                       key[j+8] = ROR8(key[j+8], 1); // bytes 8-15: rotate right
;                   }
;               }
;           }
;       }
;
;       // Write 256 words (1024 bytes) to flash, verify each
;       int ok = 1;
;       for (int i = 0; i < 256; i++) {
;           flash_program_word(flash_addr, *(uint32_t*)data);
;           if (*(uint32_t*)flash_addr != *(uint32_t*)data) ok = 0;
;           flash_addr += 4; data += 4;
;       }
;       flash_wait_busy();
;       CPSIE_I();
;       return ok;
;   }
;
; Key management:
;   set_decryption_key (0x080011C0): copies 16 bytes from packet to 0x20000034
;   Key schedule: in-place rotation every 16 bytes. After 128 bytes
;   (8 rotations), ROL8×8 = identity → key returns to original.
;   Equivalent to 128-byte expanded key with modulo-128 wrap.
;
; rotate_byte (0x080007BE):
;   uint8_t rotate_byte(uint8_t val, int direction) {
;       if (direction != 0) return ROL8(val, 1);  // (val<<1)|(val>>7)
;       else                return ROR8(val, 1);  // (val>>1)|(val<<7)
;   }
;
; 95 functions identified, 65+ named via reverse engineering.
; ==========================================================================


; ==========================================================================
; SECTION: Vector Table (0x08000000 - 0x0800017B)
; ==========================================================================
; 95 entries x 4 bytes = 380 bytes (0x17C)
; Default handler at 0x080001AB for unused IRQs
; Custom handlers: SPI1, SPI2, UART4, DMA2_CH4_5
;
    .word   0x200008B8    ; [0x08000000]  Initial_SP
    .word   0x08000191    ; [0x08000004]  Reset_Handler
    .word   0x080017DF    ; [0x08000008]  NMI_Handler
    .word   0x08001391    ; [0x0800000c]  HardFault_Handler
    .word   0x080017DD    ; [0x08000010]  MemManage_Handler
    .word   0x0800040B    ; [0x08000014]  BusFault_Handler
    .word   0x08002149    ; [0x08000018]  UsageFault_Handler
    .word   0x00000000    ; [0x0800001c]  Reserved
    .word   0x00000000    ; [0x08000020]  Reserved
    .word   0x00000000    ; [0x08000024]  Reserved
    .word   0x00000000    ; [0x08000028]  Reserved
    .word   0x08001AF3    ; [0x0800002c]  SVCall_Handler
    .word   0x080007D9    ; [0x08000030]  DebugMon_Handler
    .word   0x00000000    ; [0x08000034]  Reserved
    .word   0x080018B9    ; [0x08000038]  PendSV_Handler
    .word   0x08001E4D    ; [0x0800003c]  SysTick_Handler
    .word   0x080001AB    ; [0x08000040]  IRQ0
    .word   0x080001AB    ; [0x08000044]  IRQ1
    .word   0x080001AB    ; [0x08000048]  IRQ2
    .word   0x080001AB    ; [0x0800004c]  IRQ3
    .word   0x080001AB    ; [0x08000050]  IRQ4
    .word   0x080001AB    ; [0x08000054]  FLASH_IRQn
    .word   0x080001AB    ; [0x08000058]  IRQ6
    .word   0x080001AB    ; [0x0800005c]  IRQ7
    .word   0x080001AB    ; [0x08000060]  IRQ8
    .word   0x080001AB    ; [0x08000064]  IRQ9
    .word   0x080001AB    ; [0x08000068]  IRQ10
    .word   0x080001AB    ; [0x0800006c]  IRQ11
    .word   0x080001AB    ; [0x08000070]  IRQ12
    .word   0x080001AB    ; [0x08000074]  IRQ13
    .word   0x080001AB    ; [0x08000078]  IRQ14
    .word   0x080001AB    ; [0x0800007c]  TMR1_OVF_TMR10_IRQn
    .word   0x080001AB    ; [0x08000080]  IRQ16
    .word   0x080001AB    ; [0x08000084]  IRQ17
    .word   0x080001AB    ; [0x08000088]  IRQ18
    .word   0x080001AB    ; [0x0800008c]  IRQ19
    .word   0x080001AB    ; [0x08000090]  IRQ20
    .word   0x080001AB    ; [0x08000094]  IRQ21
    .word   0x080001AB    ; [0x08000098]  IRQ22
    .word   0x080001AB    ; [0x0800009c]  IRQ23
    .word   0x080001AB    ; [0x080000a0]  IRQ24
    .word   0x080001AB    ; [0x080000a4]  IRQ25
    .word   0x080001AB    ; [0x080000a8]  IRQ26
    .word   0x080001AB    ; [0x080000ac]  IRQ27
    .word   0x080001AB    ; [0x080000b0]  IRQ28
    .word   0x080001AB    ; [0x080000b4]  IRQ29
    .word   0x080001AB    ; [0x080000b8]  IRQ30
    .word   0x080001AB    ; [0x080000bc]  IRQ31
    .word   0x080001AB    ; [0x080000c0]  IRQ32
    .word   0x080001AB    ; [0x080000c4]  IRQ33
    .word   0x080001AB    ; [0x080000c8]  IRQ34
    .word   0x080001AB    ; [0x080000cc]  IRQ35
    .word   0x080001AB    ; [0x080000d0]  IRQ36
    .word   0x08001F03    ; [0x080000d4]  SPI1_IRQn  (custom)
    .word   0x08001F05    ; [0x080000d8]  SPI2_IRQn  (custom)
    .word   0x080001AB    ; [0x080000dc]  USART1_IRQn
    .word   0x080001AB    ; [0x080000e0]  IRQ40
    .word   0x080001AB    ; [0x080000e4]  USART3_IRQn
    .word   0x080001AB    ; [0x080000e8]  IRQ42
    .word   0x080001AB    ; [0x080000ec]  IRQ43
    .word   0x080001AB    ; [0x080000f0]  IRQ44
    .word   0x080001AB    ; [0x080000f4]  IRQ45
    .word   0x080001AB    ; [0x080000f8]  IRQ46
    .word   0x080001AB    ; [0x080000fc]  IRQ47
    .word   0x080001AB    ; [0x08000100]  IRQ48
    .word   0x080001AB    ; [0x08000104]  IRQ49
    .word   0x080001AB    ; [0x08000108]  IRQ50
    .word   0x080001AB    ; [0x0800010c]  IRQ51
    .word   0x08001EFD    ; [0x08000110]  UART4_IRQn  (custom)
    .word   0x080001AB    ; [0x08000114]  IRQ53
    .word   0x080001AB    ; [0x08000118]  IRQ54
    .word   0x080001AB    ; [0x0800011c]  IRQ55
    .word   0x080001AB    ; [0x08000120]  DMA2_CH3_IRQn
    .word   0x080001AB    ; [0x08000124]  IRQ57
    .word   0x080001AB    ; [0x08000128]  IRQ58
    .word   0x080001AB    ; [0x0800012c]  IRQ59
    .word   0x080001AB    ; [0x08000130]  IRQ60
    .word   0x080001AB    ; [0x08000134]  IRQ61
    .word   0x080001AB    ; [0x08000138]  IRQ62
    .word   0x080001AB    ; [0x0800013c]  IRQ63
    .word   0x00000000    ; [0x08000140]  IRQ64
    .word   0x00000000    ; [0x08000144]  IRQ65
    .word   0x00000000    ; [0x08000148]  IRQ66
    .word   0x00000000    ; [0x0800014c]  IRQ67
    .word   0x080001AB    ; [0x08000150]  IRQ68
    .word   0x080001AB    ; [0x08000154]  IRQ69
    .word   0x080001AB    ; [0x08000158]  IRQ70
    .word   0x080001AB    ; [0x0800015c]  IRQ71
    .word   0x080001AB    ; [0x08000160]  IRQ72
    .word   0x080001AB    ; [0x08000164]  IRQ73
    .word   0x080001AB    ; [0x08000168]  IRQ74
    .word   0x080001AB    ; [0x0800016c]  IRQ75
    .word   0x080001AB    ; [0x08000170]  IRQ76
    .word   0x080001AB    ; [0x08000174]  IRQ77
    .word   0x08001F01    ; [0x08000178]  DMA2_CH4_5_IRQn  (custom)


; ==========================================================================
; SECTION: Code (0x0800017C - 0x080024FF)
; ==========================================================================
;
; Functions sorted by address. Radare2 pdf output with named labels.
; Box-drawing characters from radare2 control flow visualization.
;

; CALL XREF from libc_init_array @ 0x8000218(x)
/ 4: stack_init ();
|           0x08000184      0048           ldr r0, [0x08000188]        ; [0x8000188:4]=0x80022a1
\           0x08000186      0047           bx r0

; --------------------------------------------------------------------------
; memset: Fill memory region with a byte value.
; C pseudocode: memset(r0, r2, r1)  - fills r1 bytes at r0 with value r2
; --------------------------------------------------------------------------
; CODE XREF from fcn.080001c2 @ 0x80001c4(x)
            ; CALL XREF from fcn.080001c6 @ 0x80001d0(x)
/ 14: fcn.080001b4 (int16_t arg1, int16_t arg2, int16_t arg3);
| `- args(r0, r1, r2)
|           0x080001b4      d2b2           uxtb r2, r2                 ; arg3
|       ,=< 0x080001b6      01e0           b 0x80001bc
|       |   ; CODE XREF from fcn.080001b4 @ 0x80001be(x)
|      .--> 0x080001b8      00f8012b       strb r2, [r0], 1            ; arg3
|      :|   ; CODE XREF from fcn.080001b4 @ 0x80001b6(x)
|      :`-> 0x080001bc      491e           subs r1, r1, 1              ; arg2
|      `==< 0x080001be      fbd2           bhs 0x80001b8
\           0x080001c0      7047           bx lr

:   ; CALL XREF from flash_program @ 0x8001502(x)
; --------------------------------------------------------------------------
; bzero: Zero-fill memory. Calls memset with r2=0.
; C pseudocode: memset(r0, 0, r1)
; --------------------------------------------------------------------------
/ 4: fcn.080001c2 ();
|       :   0x080001c2      0022           movs r2, 0
\       `=< 0x080001c4      f6e7           b fcn.080001b4

; --------------------------------------------------------------------------
; memcpy: Copy memory. Wraps memset as memmove(r0, r2, r1).
; C pseudocode: memcpy(dst=r0, src=r1, len=r2)  - returns dst
; --------------------------------------------------------------------------
/ 18: fcn.080001c6 (int16_t arg1, int16_t arg2, int16_t arg3);
| `- args(r0, r1, r2)
|           0x080001c6      10b5           push {r4, lr}
|           0x080001c8      1346           mov r3, r2                  ; arg3
|           0x080001ca      0a46           mov r2, r1                  ; arg2
|           0x080001cc      0446           mov r4, r0                  ; arg1
|           0x080001ce      1946           mov r1, r3
|           0x080001d0      fff7f0ff       bl fcn.080001b4
|           0x080001d4      2046           mov r0, r4
\           0x080001d6      10bd           pop {r4, pc}

; --------------------------------------------------------------------------
; memcmp_model: Compute string length (strlen equivalent).
; C pseudocode: int memcmp_model(char *s) { return strlen(s); }
; Scans for null terminator and returns count.
; --------------------------------------------------------------------------
; CALL XREF from uart_update_mode @ 0x80002b2(x)
            ; CALL XREF from check_spi_model @ 0x8000674(x)
/ 14: memcmp_model (int16_t arg1);
| `- args(r0)
|           0x080001d8      421c           adds r2, r0, 1              ; arg1
|           ; CODE XREF from memcmp_model @ 0x80001e0(x)
|       .-> 0x080001da      10f8011b       ldrb r1, [r0], 1            ; arg1
|       :   0x080001de      0029           cmp r1, 0
|       `=< 0x080001e0      fbd1           bne 0x80001da
|           0x080001e2      801a           subs r0, r0, r2             ; arg1
\           0x080001e4      7047           bx lr

; --------------------------------------------------------------------------
; memcmp_buf: Compare two byte buffers.
; C pseudocode:
;   int memcmp_buf(uint8_t *a, uint8_t *b, uint32_t len) {
;       for (int i = 0; i < len; i++)
;           if (a[i] != b[i]) return a[i] - b[i];
;       return 0;  // buffers match
;   }
; --------------------------------------------------------------------------
; CALL XREF from uart_update_mode @ 0x80002bc(x)
            ; CALL XREF from check_spi_model @ 0x800067e(x)
/ 26: memcmp_buf (int16_t arg1, int16_t arg2, uint32_t arg3);
| `- args(r0, r1, r2)
|           0x080001e6      30b5           push {r4, r5, lr}
|           0x080001e8      0446           mov r4, r0                  ; arg1
|           0x080001ea      0020           movs r0, 0
|           0x080001ec      0346           mov r3, r0
|       ,=< 0x080001ee      00e0           b 0x80001f2
|       |   ; CODE XREF from memcmp_buf @ 0x80001fc(x)
|      .--> 0x080001f0      5b1c           adds r3, r3, 1
|      :|   ; CODE XREF from memcmp_buf @ 0x80001ee(x)
|      :`-> 0x080001f2      9342           cmp r3, r2                  ; arg3
|      :,=< 0x080001f4      03d2           bhs 0x80001fe
|      :|   0x080001f6      e05c           ldrb r0, [r4, r3]
|      :|   0x080001f8      cd5c           ldrb r5, [r1, r3]           ; arg2
|      :|   0x080001fa      401b           subs r0, r0, r5
|      `==< 0x080001fc      f8d0           beq 0x80001f0
|       |   ; CODE XREF from memcmp_buf @ 0x80001f4(x)
\       `-> 0x080001fe      30bd           pop {r4, r5, pc}

; CALL XREF from fcn.08000000 @ 0x8000180(x)
/ 36: libc_init_array ();
|           0x08000200      064c           ldr r4, [0x0800021c]        ; [0x800021c:4]=0x8002cb4 "h-"
|           0x08000202      074d           ldr r5, [0x08000220]        ; [0x8000220:4]=0x8002cd4
|       ,=< 0x08000204      06e0           b 0x8000214
|       |   ; CODE XREF from libc_init_array @ 0x8000216(x)
|      .--> 0x08000206      e068           ldr r0, [r4, 0xc]           ; 0x80021f4
|      :|                                                              ; memcpy_init
|      :|   0x08000208      40f00103       orr r3, r0, 1
|      :|   0x0800020c      94e80700       ldm.w r4, {r0, r1, r2}
|      :|   0x08000210      9847           blx r3
|      :|   0x08000212      1034           adds r4, 0x10
|      :|   ; CODE XREF from libc_init_array @ 0x8000204(x)
|      :`-> 0x08000214      ac42           cmp r4, r5
|      `==< 0x08000216      f6d3           blo 0x8000206
|           0x08000218      fff7b4ff       bl stack_init
|           ; DATA XREF from libc_init_array @ 0x8000200(r)
|           0x0800021c      b42c           cmp r4, 0xb4                ; 180
|           0x0800021e      0008           lsrs r0, r0, 0x20
|           ; DATA XREF from libc_init_array @ 0x8000202(r)
|           0x08000220      d42c           cmp r4, 0xd4                ; 212
\           0x08000222      0008           lsrs r0, r0, 0x20

; CALL XREF from main @ 0x80022be(x)
; --------------------------------------------------------------------------
; uart_update_mode: Main UART firmware update command dispatcher.
;
; State machine (r6 → 0x2000000C):
;   state[1] = phase: 0=idle → 1=model verified → 2=ready for data
;   state[4] = write offset counter
;   state[8] = next expected flash write address
;   state[C] = total block count
;   state[10] = reboot countdown timer
;
; r5 = 0x20000044 (UART receive buffer base)
; r6 = 0x2000000C (state structure)
; r7 = 0 (constant)
; r8 = 0xFFFF (mask for big-endian 16-bit extraction)
; sb = 0x400 (expected data block size: 1024 bytes)
; --------------------------------------------------------------------------
/ 410: uart_update_mode ();
| afv: vars(1:sp[0xc..0xc])
|           0x08000224      1fb5           push {r0, r1, r2, r3, r4, lr}
|           ; Load model string "RT-950      " (12 bytes + null) onto stack
|           0x08000226      5fa0           adr r0, 0x17c               ; "RT-950      "
|                                                                      ; 0x80003a4
|           0x08000228      90e80e00       ldm.w r0, {r1, r2, r3}
|           0x0800022c      c068           ldr r0, [r0, 0xc]
|           0x0800022e      8de80e00       stm.w sp, {r1, r2, r3}
|           0x08000232      0390           str r0, [var_ch]
|           ; sb = 0x400 = expected data payload size (1024 bytes)
|           0x08000234      4ff48069       mov.w sb, 0x400
|           0x08000238      4946           mov r1, sb
|           ; Configure UART4 (0x40011800) with baud divisor 0x400
|           0x0800023a      5e48           ldr r0, [0x080003b4]        ; [0x80003b4:4]=0x40011800
|           0x0800023c      00f0b3ff       bl fcn.080011a6
|           0x08000240      01f062fe       bl timer_reset
|           ; r6 = state structure base at 0x2000000C
|           0x08000244      5c4e           ldr r6, [0x080003b8]        ; [0x80003b8:4]=0x2000000c
|           0x08000246      0027           movs r7, 0
|           ; Initialize state: phase=0 (idle), countdown=0, offset=0
|           0x08000248      7770           strb r7, [r6, 1]
|           0x0800024a      3761           str r7, [r6, 0x10]
|           0x0800024c      7760           str r7, [r6, 4]
|           0x0800024e      4ff6ff78       movw r8, 0xffff
|           ; r5 = UART receive buffer at 0x20000044
|           0x08000252      5a4d           ldr r5, [0x080003bc]        ; [0x80003bc:4]=0x20000044
|           ; === MAIN LOOP: wait for complete UART packet ===
|           ; CODE XREFS from uart_update_mode @ 0x800025e(x), 0x8000280(x)
|      ..-> 0x08000254      00f02afa       bl uart_rx_handler
|      ::   ; Check if a complete packet has been received (flag at rx_buf+0x412)
|      ::   0x08000258      95f81204       ldrb.w r0, [r5, 0x412]
|      ::   0x0800025c      0128           cmp r0, 1                   ; 1
|      `==< 0x0800025e      f9d1           bne 0x8000254
|       :   ; Packet received - toggle UART RX enable bit (bit 10 of CR1)
|       :   0x08000260      5448           ldr r0, [0x080003b4]        ; [0x80003b4:4]=0x40011800
|       :   0x08000262      0c30           adds r0, 0xc
|       :   0x08000264      0168           ldr r1, [r0]
|       :   0x08000266      81f48061       eor r1, r1, 0x400
|       :   0x0800026a      0160           str r1, [r0]
|       :   ; Validate packet framing (0xAA header, 0x55 trailer) and CRC
|       :   0x0800026c      00f0bef9       bl packet_validate
|       :   0x08000270      0128           cmp r0, 1                   ; 1
|      ,==< 0x08000272      06d0           beq 0x8000282
|      |:   ; Packet validation failed → send error 0xE1 (length/CRC error)
|      |:   0x08000274      6878           ldrb r0, [r5, 1]
|      |:   0x08000276      e121           movs r1, 0xe1
|      |:   0x08000278      00f0a2f8       bl send_response
|      |:   ; XREFS: CODE 0x080002a4  CODE 0x080002ae  CODE 0x080002ca  CODE 0x080002d8  CODE 0x080002e8  
|      |:   ; XREFS: CODE 0x08000302  CODE 0x08000312  CODE 0x0800032c  CODE 0x08000356  CODE 0x08000366  
|      |:   ; XREFS: CODE 0x08000380  CODE 0x0800038c  
|      |:   ; --- Loop back: reset timer and wait for next packet ---
| .....---> 0x0800027c      01f044fe       bl timer_reset
| :::::|`=< 0x08000280      e8e7           b 0x8000254
| :::::|    ; CODE XREF from uart_update_mode @ 0x8000272(x)
| :::::|    ; === COMMAND DISPATCH on cmd byte at rx_buf[1] ===
| :::::`--> 0x08000282      6c78           ldrb r4, [r5, 1]
| :::::     ; r4 = command byte
| :::::     0x08000284      042c           cmp r4, 4                   ; 4
| ::::: ,=< 0x08000286      28d0           beq 0x80002da               ; → cmd 0x04: package count
| :::::,==< 0x08000288      04dc           bgt 0x8000294
| :::::||   0x0800028a      022c           cmp r4, 2                   ; 2
| ========< 0x0800028c      10d0           beq 0x80002b0               ; → cmd 0x02: model verify
| :::::||   0x0800028e      032c           cmp r4, 3                   ; 3
| ========< 0x08000290      04d1           bne 0x800029c
| ========< 0x08000292      37e0           b 0x8000304                  ; → cmd 0x03: data write
| :::::||   ; CODE XREF from uart_update_mode @ 0x8000288(x)
| :::::`--> 0x08000294      0a2c           cmp r4, 0xa                 ; 10
| :::::,==< 0x08000296      06d0           beq 0x80002a6               ; → cmd 0x0A: version query
| :::::||   0x08000298      452c           cmp r4, 0x45                ; 69
| ========< 0x0800029a      78d0           beq 0x800038e               ; → cmd 0x45: end update
| :::::||   ; CODE XREF from uart_update_mode @ 0x8000290(x)
|           ; --- Unknown command: respond with error 0xE5 ---
| --------> 0x0800029c      e521           movs r1, 0xe5
| :::::||   0x0800029e      2046           mov r0, r4
| :::::||   0x080002a0      00f08ef8       bl send_response
| ========< 0x080002a4      eae7           b 0x800027c
| :::::||   ; CODE XREF from uart_update_mode @ 0x8000296(x)
|           ; --- CMD 0x0A: Version query → ACK with "BOOTLOADER_V3" ---
| :::::`--> 0x080002a6      0621           movs r1, 6                  ; ACK
| ::::: |   0x080002a8      2046           mov r0, r4
| ::::: |   0x080002aa      00f089f8       bl send_response
| ::::: |   ; CODE XREF from uart_update_mode @ 0x800039e(x)
| =====.--> 0x080002ae      e5e7           b 0x800027c
| ::::::|   ; CODE XREF from uart_update_mode @ 0x800028c(x)
|           ; --- CMD 0x02: Model verification ---
|           ; Compare received string (rx_buf+6) against "RT-950      " on stack
| --------> 0x080002b0      6846           mov r0, sp                  ; model string on stack
| ::::::|   0x080002b2      fff791ff       bl memcmp_model             ; r0 = strlen(model)
| ::::::|   0x080002b6      0246           mov r2, r0                  ; r2 = length to compare
| ::::::|   0x080002b8      6946           mov r1, sp                  ; r1 = expected model
| ::::::|   0x080002ba      a81d           adds r0, r5, 6              ; r0 = received model (rx_buf+6)
| ::::::|   0x080002bc      fff793ff       bl memcmp_buf               ; returns 0 if match
| ========< 0x080002c0      20b1           cbz r0, 0x80002cc           ; match → set state=1
| ::::::|   ; Model mismatch → error 0xE6
| ::::::|   0x080002c2      e621           movs r1, 0xe6
| ::::::|   0x080002c4      2046           mov r0, r4
| ::::::|   0x080002c6      00f07bf8       bl send_response
| ========< 0x080002ca      d7e7           b 0x800027c
| ::::::|   ; CODE XREF from uart_update_mode @ 0x80002c0(x)
|           ; Model match → state[1] = 1 (model verified), ACK
| --------> 0x080002cc      0120           movs r0, 1
| ::::::|   0x080002ce      7070           strb r0, [r6, 1]            ; state[1] = 1 (verified)
| ::::::|   0x080002d0      0621           movs r1, 6                  ; ACK
| ::::::|   0x080002d2      2046           mov r0, r4
| ::::::|   0x080002d4      00f074f8       bl send_response
| ========< 0x080002d8      d0e7           b 0x800027c
| ::::::|   ; CODE XREF from uart_update_mode @ 0x8000286(x)
|           ; --- CMD 0x04: Package count setup (requires state[1]==1) ---
| ::::::`-> 0x080002da      7078           ldrb r0, [r6, 1]            ; check current phase
| ::::::    0x080002dc      0128           cmp r0, 1                   ; must be 1 (model verified)
| ::::::,=< 0x080002de      04d0           beq 0x80002ea               ; proceed if verified
| ::::::|   ; State not ready → error 0xE5
| ::::::|   0x080002e0      e521           movs r1, 0xe5
| ::::::|   0x080002e2      2046           mov r0, r4
| ::::::|   0x080002e4      00f06cf8       bl send_response
| ========< 0x080002e8      c8e7           b 0x800027c
| ::::::|   ; CODE XREF from uart_update_mode @ 0x80002de(x)
|           ; Extract block count from rx_buf[6..7] (big-endian u16)
| ::::::`-> 0x080002ea      a879           ldrb r0, [r5, 6]            ; high byte
| ::::::    0x080002ec      0002           lsls r0, r0, 8
| ::::::    0x080002ee      e979           ldrb r1, [r5, 7]            ; low byte
| ::::::    0x080002f0      0143           orrs r1, r0                 ; r1 = block count
| ::::::    0x080002f2      f160           str r1, [r6, 0xc]           ; state[0xC] = total blocks
| ::::::    0x080002f4      b760           str r7, [r6, 8]             ; state[8] = 0 (reset write offset)
| ::::::    ; Advance to phase 2 (ready for data)
| ::::::    0x080002f6      0220           movs r0, 2
| ::::::    0x080002f8      7070           strb r0, [r6, 1]            ; state[1] = 2
| ::::::    0x080002fa      0621           movs r1, 6                  ; ACK
| ::::::    0x080002fc      2046           mov r0, r4
| ::::::    0x080002fe      00f05ff8       bl send_response
| ========< 0x08000302      bbe7           b 0x800027c
| ::::::    ; CODE XREF from uart_update_mode @ 0x8000292(x)
|           ; --- CMD 0x03: Data write (requires state[1]==2) ---
| --------> 0x08000304      7078           ldrb r0, [r6, 1]            ; check current phase
| ::::::    0x08000306      0228           cmp r0, 2                   ; must be 2 (ready for data)
| ::::::,=< 0x08000308      04d0           beq 0x8000314               ; proceed if ready
| ::::::|   ; State not ready → error 0xE5
| ::::::|   0x0800030a      e521           movs r1, 0xe5
| ::::::|   0x0800030c      2046           mov r0, r4
| ::::::|   0x0800030e      00f057f8       bl send_response
| ========< 0x08000312      b3e7           b 0x800027c
| ::::::|   ; CODE XREF from uart_update_mode @ 0x8000308(x)
|           ; Validate data length == 0x400 (1024 bytes)
| ::::::`-> 0x08000314      2879           ldrb r0, [r5, 4]            ; len_hi from rx_buf[4]
| ::::::    0x08000316      2946           mov r1, r5
| ::::::    0x08000318      08ea0020       and.w r0, r8, r0, lsl 8     ; (len_hi << 8) & 0xFFFF
| ::::::    0x0800031c      4a79           ldrb r2, [r1, 5]            ; len_lo from rx_buf[5]
| ::::::    0x0800031e      1044           add r0, r2                  ; r0 = data length
| ::::::    0x08000320      4845           cmp r0, sb                  ; must equal 0x400
| ::::::,=< 0x08000322      04d0           beq 0x800032e               ; length OK
| ::::::|   ; Length mismatch → error 0xE1
| ::::::|   0x08000324      e121           movs r1, 0xe1
| ::::::|   0x08000326      0320           movs r0, 3
| ::::::|   0x08000328      00f04af8       bl send_response
| `=======< 0x0800032c      a6e7           b 0x800027c
|  :::::|   ; CODE XREF from uart_update_mode @ 0x8000322(x)
|           ; Extract block number from rx_buf[2..3] (big-endian u16)
|  :::::`-> 0x0800032e      8878           ldrb r0, [r1, 2]            ; block_hi
|  :::::    0x08000330      ca78           ldrb r2, [r1, 3]            ; block_lo
|  :::::    0x08000332      08ea0020       and.w r0, r8, r0, lsl 8
|  :::::    0x08000336      1044           add r0, r2                  ; r0 = block number
|  :::::    ; Block 1 → extract decryption key (special case)
|  :::::    0x08000338      0128           cmp r0, 1                   ; 1
|  :::::,=< 0x0800033a      0dd0           beq 0x8000358               ; → set_decryption_key
|           ; Block 0 → write directly (no decryption, contains vectors)
| ,=======< 0x0800033c      00b1           cbz r0, 0x8000340           ; block 0: skip subtract
| |:::::|   ; Block ≥ 2 → r0 = block_num - 1 (flash offset = (block-1)*0x400)
| |:::::|   0x0800033e      401e           subs r0, r0, 1
| |:::::|   ; CODE XREF from uart_update_mode @ 0x800033c(x)
| `-------> 0x08000340      8202           lsls r2, r0, 0xa            ; r2 = offset = r0 * 0x400
|  :::::|   ; Verify sequential ordering: state[8] must match expected offset
|  :::::|   0x08000342      b068           ldr r0, [r6, 8]             ; r0 = state[8] (next expected)
|  :::::|   0x08000344      9042           cmp r0, r2
| ,=======< 0x08000346      02d1           bne 0x800034e               ; mismatch → error 0xE2
| |:::::|   ; Bounds check: offset must be < 0xFD000 (firmware region)
| |:::::|   0x08000348      b0f57d2f       cmp.w r0, 0xfd000
| ========< 0x0800034c      0cd3           blo 0x8000368               ; in bounds → flash write
| |:::::|   ; CODE XREF from uart_update_mode @ 0x8000346(x)
|           ; Sequence or bounds error → error 0xE2 (verify error)
| `-------> 0x0800034e      e221           movs r1, 0xe2
|  :::::|   0x08000350      2046           mov r0, r4
|  :::::|   0x08000352      00f035f8       bl send_response
|  `======< 0x08000356      91e7           b 0x800027c
|   ::::|   ; CODE XREF from uart_update_mode @ 0x800033a(x)
|           ; --- Block 1: Extract 16-byte decryption key from data[6..21] ---
|   ::::`-> 0x08000358      881d           adds r0, r1, 6              ; r0 = &rx_buf[6] (key data)
|   ::::    0x0800035a      00f031ff       bl set_decryption_key       ; copy 16 bytes → 0x20000034
|   ::::    0x0800035e      0621           movs r1, 6                  ; ACK
|   ::::    0x08000360      2046           mov r0, r4
|   ::::    0x08000362      00f02df8       bl send_response
|   `=====< 0x08000366      89e7           b 0x800027c
|    :::    ; CODE XREF from uart_update_mode @ 0x800034c(x)
|           ; --- Block 0 or ≥2: Decrypt (if needed) and write to flash ---
|           ; r0 = state[8] (write offset), r1 = &rx_buf[6] (data payload)
| --------> 0x08000368      891d           adds r1, r1, 6              ; r1 = data pointer
|    :::    ; flash_from_spi(offset=state[8], data=rx_buf+6)
|    :::    ; Block 0 skips decryption; block ≥2 decrypts then writes
|    :::    0x0800036a      01f01df8       bl flash_from_spi
|    :::,=< 0x0800036e      48b1           cbz r0, 0x8000384           ; 0 = flash error
|    :::|   ; Flash write succeeded → advance write offset by 0x400
|    :::|   0x08000370      b068           ldr r0, [r6, 8]
|    :::|   0x08000372      00f58060       add.w r0, r0, 0x400         ; state[8] += 1024
|    :::|   0x08000376      b060           str r0, [r6, 8]
|    :::|   0x08000378      0621           movs r1, 6                  ; ACK
|    :::|   0x0800037a      2046           mov r0, r4
|    :::|   0x0800037c      00f020f8       bl send_response
|    `====< 0x08000380      7ce7           b 0x800027c
..
|    |::|   ; CODE XREF from uart_update_mode @ 0x800036e(x)
|           ; Flash write failed → error 0xE3
|    |::`-> 0x08000384      e321           movs r1, 0xe3
|    |::    0x08000386      2046           mov r0, r4
|    |::    0x08000388      00f01af8       bl send_response
|    |`===< 0x0800038c      76e7           b 0x800027c
|    | :    ; CODE XREF from uart_update_mode @ 0x800029a(x)
|    | :    ; CODE XREF from uart_update_mode @ +0x15e(x)
|           ; --- CMD 0x45: End update → ACK, set countdown, reboot ---
| ---`----> 0x0800038e      0621           movs r1, 6                  ; ACK
|      :    0x08000390      2046           mov r0, r4
|      :    0x08000392      00f015f8       bl send_response
|      :    ; Set reboot countdown timer = 0x14 (20 ticks)
|      :    0x08000396      1420           movs r0, 0x14
|      :    0x08000398      3061           str r0, [r6, 0x10]          ; state[0x10] = countdown
|      :    ; Wait for countdown to reach 0 (decremented by SysTick ISR)
|      :    0x0800039a      3069           ldr r0, [r6, 0x10]
|      :    0x0800039c      0028           cmp r0, 0
|      `==< 0x0800039e      86d0           beq 0x80002ae               ; done → return to main loop
|           ; System reset via AIRCR register (SCB→AIRCR = 0x05FA0004)
|           0x080003a0      01f078fa       bl systick_delay
|           ; DATA XREFS from uart_update_mode @ 0x8000226(r), 0x8000228(r)
|           0x080003a4      5254           strb r2, [r2, r1]
|           0x080003a6      2d39           subs r1, 0x2d
|           ; DATA XREF from uart_update_mode @ 0x8000228(r)
|           0x080003a8      3530           adds r0, 0x35
|           0x080003aa      2020           movs r0, 0x20
|           ; DATA XREF from uart_update_mode @ 0x8000228(r)
|           0x080003ac      2020           movs r0, 0x20
|           0x080003ae      2020           movs r0, 0x20
|           ; DATA XREF from uart_update_mode @ 0x800022c(r)
|           0x080003b0      0000           movs r0, r0
|           0x080003b2      0000           movs r0, r0
|           ; DATA XREFS from uart_update_mode @ 0x800023a(r), 0x8000260(r)
|           0x080003b4      0018           adds r0, r0, r0
|           0x080003b6      0140           ands r1, r0
|           ; DATA XREF from uart_update_mode @ 0x8000244(r)
|           0x080003b8      0c00           movs r4, r1
|           0x080003ba      0020           movs r0, 0
|           ; DATA XREF from uart_update_mode @ 0x8000252(r)
|           0x080003bc      4400           lsls r4, r0, 1
\           0x080003be      0020           movs r0, 0

; CALL XREF from main @ 0x80022ac(x)
/ 434: lcd_gpio_init ();
| afv: vars(3:sp[0x1d..0x20])
|           0x080003f8      10b5           push {r4, lr}
|           0x080003fa      01f029fd       bl systick_handler
|           0x080003fe      01f0eff9       bl fcn.080017e0
|           0x08000402      bde81040       pop.w {r4, lr}
|       ,=< 0x08000406      00f0e7be       b.w 0x80011d8
..
        |   ; CALL XREF from check_update_button @ 0x8000530(x)
|      ||   ; CODE XREF from crc_ccitt_spi @ 0x800047c(x)
|     :||   ; CODE XREF from crc_ccitt_spi @ 0x8000472(x)
|  ||::||   ; CODE XREF from crc_ccitt_spi @ 0x800045e(x)
|  | ::||   ; CODE XREF from crc_ccitt_spi @ 0x8000466(x)
|     :||   ; CODE XREF from crc_ccitt_spi @ 0x800044e(x)
        |   ; DATA XREF from crc_ccitt_spi @ 0x8000412(r)
        |   ; DATA XREF from crc_ccitt_spi @ 0x800044a(r)
        |   ; CALL XREF from main @ 0x80022b6(x)
|   |||||   ; CODE XREFS from check_update_button @ 0x80004ac(x), 0x80004b8(x), 0x80004c6(x)
| |||||||   ; CODE XREF from check_update_button @ 0x80004d0(x)
| || ||||   ; CODE XREF from check_update_button @ 0x8000514(x)
| |||||||   ; CODE XREFS from check_update_button @ 0x8000512(x), 0x8000524(x)
|  | ||||   ; CODE XREF from check_update_button @ 0x800052a(x)
| |||||||   ; CODE XREF from check_update_button @ 0x8000548(x)
|  ||||||   ; CODE XREFS from check_update_button @ 0x80005bc(x), 0x80005d2(x)
| :||||||   ; CODE XREF from check_update_button @ 0x80004f6(x)
| :|| |||   ; CODE XREFS from check_update_button @ 0x80004ea(x), 0x80004f4(x), 0x8000500(x), 0x8000538(x), 0x800056c(x)
| :    ||   ; CODE XREF from check_update_button @ 0x80005ba(x)
| :|||:||   ; XREFS: CODE 0x08000588  CODE 0x0800058e  CODE 0x08000594  CODE 0x08000598  CODE 0x0800059c  
| :|||:||   ; XREFS: CODE 0x080005a0  CODE 0x080005a4  CODE 0x080005aa  
| :  |:||   ; CODE XREFS from check_update_button @ 0x800057a(x), 0x8000580(x)
| :     |   ; CODE XREF from check_update_button @ 0x80005b0(x)
        |   ; DATA XREF from check_update_button @ 0x800049e(r)
        |   ; DATA XREF from check_update_button @ 0x80004ae(r)
        |   ; DATA XREFS from check_update_button @ 0x80004d8(r), 0x80004e0(r), 0x8000550(r)
        |   ; DATA XREF from check_update_button @ 0x8000508(r)
        |   ; DATA XREF from check_update_button @ 0x800056e(r)
        |   ; DATA XREF from check_update_button @ 0x8000578(r)
        |   ; CALL XREF from uart_update_mode @ 0x800026c(x)
|     |||   ; CODE XREF from packet_validate @ 0x80005f4(x)
|     | |   ; CODE XREF from packet_validate @ 0x8000600(x)
|      ||   ; CODE XREF from packet_validate @ 0x8000630(x)
        |   ; DATA XREFS from packet_validate @ 0x80005ee(r), 0x8000618(r), 0x8000622(r)
        |   ; CALL XREF from main @ 0x80022c8(x)
|      ||   ; CODE XREF from check_spi_flag @ 0x8000646(x)
        |   ; DATA XREF from check_spi_flag @ 0x8000640(r)
        |   ; CALL XREF from main @ 0x80022c2(x)
|      ||   ; CODE XREF from check_spi_model @ 0x8000696(x)
|     :||   ; CODE XREF from check_spi_model @ 0x8000682(x)
        |   ; DATA XREFS from check_spi_model @ 0x8000658(r), 0x800065a(r)
        |   ; DATA XREF from check_spi_model @ 0x800065a(r)
        |   ; DATA XREF from check_spi_model @ 0x800065a(r)
        |   ; DATA XREF from check_spi_model @ 0x800065e(r)
        |   ; DATA XREF from check_spi_model @ 0x800066c(r)
        |   ; CALL XREF from uart_update_mode @ 0x8000254(x)
|     |||   ; CODE XREF from uart_rx_handler @ 0x80006b4(x)
|     | |   ; CODE XREF from uart_rx_handler @ 0x80006c4(x)
|      ||   ; CODE XREF from uart_rx_handler @ 0x80006cc(x)
        |   ; DATA XREF from uart_rx_handler @ 0x80006ae(r)
        |   ; DATA XREF from uart_rx_handler @ 0x80006bc(r)
        |   ; DATA XREF from uart_rx_handler @ 0x80006d2(r)
        |   ; CALL XREF from main @ 0x80022d8(x)
        |   ; XREFS: DATA 0x080006f6  DATA 0x08000706  DATA 0x08000710  DATA 0x0800071a  DATA 0x08000724  
        |   ; XREFS: DATA 0x0800072e  DATA 0x08000738  DATA 0x08000742  DATA 0x0800074c  DATA 0x08000756  
        |   ; XREFS: DATA 0x08000760  DATA 0x0800076a  
        |   ; CALL XREF from send_response @ 0x80003d8(x)
        |   ; CALL XREF from packet_validate @ 0x8000610(x)
|      ||   ; CODE XREF from crc_ccitt_calc @ 0x80007ba(x)
|     :||   ; CODE XREF from crc_ccitt_calc @ 0x80007b2(x)
|  ||::||   ; CODE XREF from crc_ccitt_calc @ 0x800079e(x)
|  | ::||   ; CODE XREF from crc_ccitt_calc @ 0x80007a6(x)
|     :||   ; CODE XREF from crc_ccitt_calc @ 0x8000790(x)
        |   ; CALL XREFS from flash_from_spi @ 0x800142e(x), 0x8001440(x)
|      ||   ; CODE XREF from fcn.080007be @ 0x80007c2(x)
        |   ; CALL XREF from model_xor_decode @ 0x8002388(x)
        |   ; CALL XREF from model_xor_encode @ 0x80023e8(x)
        |   ; XREFS: CALL 0x080006da  CODE 0x080008b6  CODE 0x080008e8  CALL 0x08001596  CALL 0x080015a4  
        |   ; XREFS: CALL 0x080015b2  CALL 0x080015be  
|    |::|   ; CODE XREF from gpio_read_pin @ 0x8000806(x)
|   :|::|   ; CODE XREF from gpio_read_pin @ 0x80007f8(x)
|     ::|   ;-- aav.0x08000808:
|     ::|   ; NULL XREF from set_decryption_key @ +0x1ba(r)
      ::|   ; XREFS: CALL 0x08000424  CALL 0x08000488  CALL 0x080007fc  CALL 0x08001c38  CODE 0x08001c7a  
      ::|   ; XREFS: CALL 0x08001ca0  CALL 0x08001cba  CALL 0x08001d50  CALL 0x08001d8e  CALL 0x08001d96  
      ::|   ; XREFS: CALL 0x08001e2a  CODE 0x08001e42  
|     ::|   ; CODE XREF from fcn.0800080a @ 0x8000814(x)
      ::|   ; XREFS: CALL 0x08000518  CALL 0x08000540  CALL 0x0800055a  CALL 0x08000564  CALL 0x080005cc  
      ::|   ; XREFS: CALL 0x0800068a  
| ||||::|   ; CODE XREF from lcd_show_status @ 0x8000848(x)
| |||  :|   ; CODE XREF from lcd_show_status @ 0x800084c(x)
| ||  :||   ; CODE XREF from lcd_show_status @ +0x120(x)
| || :: |   ; CODE XREF from lcd_show_status @ 0x8000850(x)
| |  ::||   ; CODE XREF from lcd_show_status @ 0x8000854(x)
    |::||   ; DATA XREF from lcd_show_status @ 0x8000828(r)
    |::||   ; DATA XREF from lcd_show_status @ 0x800083a(r)
    |::||   ; STRN XREF from lcd_show_status @ 0x800085c(r)
    | :||   ; STRN XREF from lcd_show_status @ 0x800086c(r)
    | :||   ; STRN XREF from lcd_show_status @ 0x800088e(r)
    |  ||   ; STRN XREF from lcd_show_status @ 0x800089e(r)
    |  ||   ; STRN XREF from lcd_show_status @ 0x80008c0(r)
    |  ||   ; STRN XREF from lcd_show_status @ 0x80008d0(r)
    |  ||   ; STRN XREF from lcd_show_status @ 0x80008f2(r)
    |  ||   ; STRN XREF from lcd_show_status @ 0x8000910(r)
    |  ||   ; CALL XREF from check_update_button @ 0x8000544(x)
|   | |||   ; CODE XREF from spi_cmd_read_status @ 0x8000a02(x)
|   |:|||   ; CODE XREF from spi_cmd_read_status @ 0x80009fc(x)
| |:|:|||   ; CODE XREF from spi_cmd_read_status @ 0x80009e8(x)
|  :|:|||   ; CODE XREF from spi_cmd_read_status @ 0x80009f0(x)
|   | |||   ; CODE XREF from spi_cmd_read_status @ 0x80009be(x)
|   | |||   ; CODE XREF from spi_cmd_read_status @ 0x8000a06(x)
    |  ||   ; DATA XREF from spi_cmd_read_status @ 0x80009ac(r)
    |  ||   ; DATA XREFS from spi_cmd_read_status @ 0x80009b2(r), 0x80009c0(r), 0x80009d0(r)
    |  ||   ; DATA XREF from spi_cmd_read_status @ 0x80009c8(r)
    |  ||   ; CALL XREF from flash_from_spi @ 0x80013d0(x)
|   |||||   ; CODE XREFS from spi_flash_read @ 0x8000a34(x), 0x8000a6c(x)
|  :| |||   ; CODE XREF from spi_flash_read @ 0x8000a28(x)
|   | |||   ; CODE XREFS from spi_flash_read @ 0x8000a9a(x), 0x8000aa4(x)
|  :|:|||   ; CODE XREF from spi_flash_read @ 0x8000a62(x)
    |  ||   ; DATA XREF from spi_flash_read @ 0x8000a22(r)
    |  ||   ; DATA XREFS from spi_flash_read @ 0x8000a5a(r), 0x8000a90(r)
    |  ||   ; CALL XREFS from spi_xfer_byte_00 @ 0x8000c24(x), 0x8000c2a(x)
    |  ||   ; CALL XREFS from spi_xfer_byte_02 @ 0x8000c84(x), 0x8000c8a(x)
|   | |||   ; CODE XREF from spi_flash_erase @ 0x8000ad8(x)
|   | |||   ; CODE XREF from spi_flash_erase @ 0x8000aec(x)
|   |:|||   ; CODE XREF from spi_flash_erase @ 0x8000ae2(x)
    |  ||   ; DATA XREF from spi_flash_erase @ 0x8000ad2(r)
    |  ||   ; CALL XREFS from spi_xfer_byte_03 @ 0x8000c44(x), 0x8000c4a(x)
|   | |||   ; CODE XREF from spi_flash_write_enable @ 0x8000b00(x)
|   | |||   ; CODE XREF from spi_flash_write_enable @ 0x8000b14(x)
|   |:|||   ; CODE XREF from spi_flash_write_enable @ 0x8000b0a(x)
    |  ||   ; DATA XREF from spi_flash_write_enable @ 0x8000afa(r)
    |  ||   ; CALL XREFS from spi_xfer_byte_0b @ 0x8000c64(x), 0x8000c6a(x)
|   | |||   ; CODE XREF from spi_flash_write @ 0x8000b2a(x)
|   | |||   ; CODE XREF from spi_flash_write @ 0x8000b42(x)
|   |:|||   ; CODE XREF from spi_flash_write @ 0x8000b36(x)
    |  ||   ; DATA XREF from spi_flash_write @ 0x8000b22(r)
    |  ||   ; CALL XREF from clear_spi_update @ 0x8000774(x)
    |  ||   ; CALL XREF from flash_from_spi @ 0x800147c(x)
    |  ||   ; CALL XREF from aav.0x08002d00 @ +0x54(x)
    |  ||   ; DATA XREF from spi_wait_busy @ 0x8000b4c(r)
    |  ||   ; XREFS: CALL 0x08000702  CALL 0x0800070c  CALL 0x08000716  CALL 0x08000720  CALL 0x0800072a  
    |  ||   ; XREFS: CALL 0x08000734  CALL 0x0800073e  CALL 0x08000748  CALL 0x08000752  CALL 0x0800075c  
    |  ||   ; XREFS: CALL 0x08000766  CALL 0x08000770  CALL 0x0800145e  CALL 0x08002d3c  CALL 0x08002d46  
    |  ||   ; XREFS: CALL 0x08002d50  
|   |||||   ; CODE XREFS from spi_transaction @ 0x8000b84(x), 0x8000bb0(x)
|  :| |||   ; CODE XREF from spi_transaction @ 0x8000b7a(x)
|   | |||   ; CODE XREFS from spi_transaction @ 0x8000bd6(x), 0x8000be0(x)
|  :|:|||   ; CODE XREF from spi_transaction @ 0x8000ba6(x)
    |  ||   ; DATA XREF from spi_transaction @ 0x8000b70(r)
    |  ||   ; DATA XREFS from spi_transaction @ 0x8000ba2(r), 0x8000bcc(r)
    |  ||   ; CALL XREF from clear_spi_update @ 0x80006fa(x)
    |  ||   ; CALL XREF from flash_from_spi @ 0x80013c6(x)
    |  ||   ; CALL XREF from aav.0x08002d00 @ +0x34(x)
    |  ||   ; DATA XREF from spi_cs_low @ 0x8000c06(r)
    |  ||   ; DATA XREF from spi_cs_low @ 0x8000c04(r)
    |  ||   ; DATA XREF from spi_cs_low @ 0x8000c0a(r)
    |  ||   ; CALL XREFS from spi_flash_read @ 0x8000a66(x), 0x8000a82(x)
    |  ||   ; CALL XREF from spi_transaction @ 0x8000baa(x)
|   | |||   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c36(x)
|   |:|||   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c28(x)
|   | |||   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c32(x)
|   | |||   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c3a(x)
    |  ||   ; CALL XREFS from spi_flash_read @ 0x8000a9e(x), 0x8000aba(x)
    |  ||   ; CALL XREFS from spi_transaction @ 0x8000bda(x), 0x8000bee(x)
|   | |||   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c56(x)
|   |:|||   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c48(x)
|   | |||   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c52(x)
|   | |||   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c5a(x)
    |  ||   ; DATA XREF from lcd_show_status @ +0x160(r)
    |  ||   ; CALL XREFS from spi_flash_read @ 0x8000a2e(r), 0x8000a4c(x)
    |  ||   ; CALL XREFS from spi_transaction @ 0x8000b7e(r), 0x8000b94(x)
|   | |||   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c76(x)
|   |:|||   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c68(x)
|   | |||   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c72(x)
|   | |||   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c7a(x)
    |  ||   ; DATA XREF from lcd_show_status @ +0x13e(r)
    |  ||   ; CALL XREF from spi_transaction @ 0x8000bbe(r)
|   | |||   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c96(x)
|   |:|||   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c88(x)
|   | |||   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c92(x)
|   | |||   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c9a(x)
    |  ||   ; XREFS: CALL 0x08001214  CALL 0x08001236  CALL 0x0800125a  CALL 0x0800127a  CALL 0x08001294  
    |  ||   ; XREFS: CALL 0x080012a8  CALL 0x080012ca  CALL 0x080012f6  CALL 0x08001316  CALL 0x08001330  
    |  ||   ; XREFS: CALL 0x08001350  CALL 0x0800136c  CALL 0x08002178  CALL 0x0800218c  
|   | |||   ; CODE XREF from spi_flash_read_id @ 0x8000cae(x)
|   | |||   ; CODE XREF from spi_flash_read_id @ 0x8000cfa(x)
| |||:|||   ; CODE XREF from spi_flash_read_id @ 0x8000ce8(x)
| |||:|||   ; CODE XREF from spi_flash_read_id @ 0x8000cec(x)
| |||:|||   ; CODE XREFS from spi_flash_read_id @ 0x8000cd0(x), 0x8000cee(x), 0x8000cf2(x)
|   | |||   ; CODE XREF from spi_flash_read_id @ 0x8000cc2(x)
|   | |||   ; CODE XREF from spi_flash_read_id @ 0x8000d3e(x)
| |||:|||   ; CODE XREF from spi_flash_read_id @ 0x8000d2e(x)
| |||:|||   ; CODE XREFS from spi_flash_read_id @ 0x8000d16(x), 0x8000d36(x)
|   | |||   ; CODE XREF from spi_flash_read_id @ 0x8000d02(x)
    |  ||   ; CALL XREF from lcd_gpio_init @ 0x80011f0(x)
|   |||||   ;-- switch:
    |||||   ; DATA XREF from uart_irq_handler @ 0x8000d6a(r)
|   |||||   ;-- case 0:                                                ; from 0x08000d6a
|   |||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|   |||||   ;-- case 1:                                                ; from 0x08000d6a
|   |||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|  ||||||   ;-- case 2:                                                ; from 0x08000d6a
|  ||||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
| |||||||   ;-- case 3:                                                ; from 0x08000d6a
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
| |||||||   ;-- case 4:                                                ; from 0x08000d6a
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
| |||||||   ;-- case 5:                                                ; from 0x08000d6a
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
| |||||||   ; XREFS: CODE 0x08000d68  CODE 0x08000d76  CODE 0x08000d7c  CODE 0x08000d82  CODE 0x08000d88  
| |||||||   ; XREFS: CODE 0x08000d8e  
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d94(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d98(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000dc0(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000dce(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d9c(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000da0(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000da4(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000da8(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000dac(x)
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000db0(x)
| |||||||   ; XREFS: CODE 0x08000db2  CODE 0x08000dbc  CODE 0x08000dca  CODE 0x08000dd8  CODE 0x08000de2  
| |||||||   ; XREFS: CODE 0x08000dec  CODE 0x08000df6  CODE 0x08000e00  CODE 0x08000e0a  CODE 0x08000e14  
|   |||||   ; CODE XREFS from uart_irq_handler @ 0x8000e20(x), 0x8000e54(x), 0x8000e68(x), 0x8000e74(x)
| ::| |||   ; CODE XREF from uart_irq_handler @ 0x8000d4e(x)
| ::|||||   ; CODE XREFS from uart_irq_handler @ 0x8000e34(x), 0x8000e4c(x), 0x8000e50(x)
| ::|| ||   ; CODE XREF from uart_irq_handler @ 0x8000e44(x)
|   |||||   ; CODE XREFS from uart_irq_handler @ 0x8000e9e(x), 0x8000eb8(x), 0x8000ee2(x), 0x8000eee(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e3e(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e86(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e36(x)
|   |||||   ; CODE XREFS from uart_irq_handler @ 0x8000f10(x), 0x8000f1c(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000eca(x)
|   |||||   ; XREFS(32)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e3c(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e42(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e48(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e5a(x)
| ::|| ||   ; CODE XREF from uart_irq_handler @ 0x8000e5e(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000ec2(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e62(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e84(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f3a(x)
| ::|||||   ; CODE XREFS from uart_irq_handler @ 0x8000f38(x), 0x8000f48(x), 0x8000f4e(x), 0x8000f56(x)
| ::|||||   ; CODE XREFS from uart_irq_handler @ 0x8001026(x), 0x8001042(x), 0x800105a(x), 0x8001070(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e8c(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e92(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000e98(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000ea6(x)
| ::|||||   ; XREFS: CODE 0x08000e56  CODE 0x08000ea0  CODE 0x08000eac  CODE 0x08000eb2  CODE 0x08000eba  
| ::|||||   ; XREFS: CODE 0x08000ed0  CODE 0x08000ed6  
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8001086(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000ec0(x)
| ::| |||   ; CODE XREF from uart_irq_handler @ 0x8000ec8(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f2c(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x800109c(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f2a(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x80010b2(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000edc(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000efe(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f04(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f2e(x)
| ::|||||   ; CODE XREFS from uart_irq_handler @ 0x80010c8(x), 0x80010de(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f0a(x)
| ::|||||   ; CODE XREF from uart_irq_handler @ 0x8000f30(x)
| ::|| ||   ; CODE XREF from uart_irq_handler @ 0x80010f4(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f58(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x800110a(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f28(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8001120(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f40(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f5e(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f64(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f88(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8001136(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f6a(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f8a(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x800114c(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f8c(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f5a(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8001172(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000f8e(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8001188(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000fd4(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000fd6(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8000fee(x)
| ::||:||   ; CODE XREF from uart_irq_handler @ 0x8001032(x)
| ::|| ||   ; CODE XREF from uart_irq_handler @ 0x8000fa2(x)
| ::|| ||   ; CODE XREF from uart_irq_handler @ 0x8000fa4(x)
| ::|  ||   ; CODE XREF from uart_irq_handler @ 0x8000fd2(x)
| ::|  ||   ; CODE XREF from uart_irq_handler @ 0x8001034(x)
  ::|  ||   ; XREFS: DATA 0x08000d74  DATA 0x08000d78  DATA 0x08000d7e  DATA 0x08000d84  DATA 0x08000d8a  
  ::|  ||   ; XREFS: DATA 0x08000d90  DATA 0x08000e2a  
  ::|  ||   ; DATA XREF from uart_irq_handler @ 0x8000f32(r)
  ::|  ||   ; DATA XREF from uart_irq_handler @ 0x8000f50(r)
| ::|  ||   ; CODE XREF from uart_irq_handler @ 0x8001036(x)
|  :|  ||   ; CODE XREF from uart_irq_handler @ 0x800104e(x)
    |  ||   ; CALL XREFS from check_update_button @ 0x80004a8(x), 0x80004b4(x), 0x80004c2(x), 0x80004cc(x)
    |  ||   ; CALL XREF from uart_rx_handler @ 0x80006be(x)
|   | |||   ; CODE XREF from flash_unlock @ 0x800119c(x)
    |  ||   ; XREFS: CALL 0x0800041e  CALL 0x080006d4  CALL 0x080012d2  CALL 0x080012da  CALL 0x080012e2  
    |  ||   ; XREFS: CALL 0x0800159e  CALL 0x08001c32  CALL 0x08001c9a  CALL 0x08001d4a  CALL 0x08001e24  
    |  ||   ; XREFS: CALL 0x08002372  CALL 0x0800237a  CALL 0x08002382  CALL 0x080023d2  CALL 0x080023e2  
    |  ||   ; XREFS: CALL 0x0800023c  CALL 0x08000482  CODE 0x08000880  CALL 0x080008ac  CALL 0x080008de  
    |  ||   ; XREFS: CODE 0x08000906  CODE 0x08000924  CALL 0x0800121c  CALL 0x08001590  CALL 0x080015ac  
    |  ||   ; XREFS: CALL 0x08001c70  CALL 0x08001cb4  CALL 0x08001d88  CALL 0x08001e38  CALL 0x080023b4  
    |  ||   ; XREFS: CODE 0x080023c0  CALL 0x080023da  CALL 0x08002414  CODE 0x08002420  
        |   ; XREFS: CALL 0x080011f6  CALL 0x0800123c  CALL 0x080012ae  CALL 0x080012fc  CALL 0x08001336  
        |   ; XREFS: CALL 0x0800215a  
        |   ; CALL XREFS from lcd_gpio_init @ 0x8001264(x), 0x800131e(x), 0x800135a(x)
        |   ; CALL XREF from model_xor_decode @ 0x8002394(x)
        |   ; CALL XREF from model_xor_encode @ 0x80023f4(x)
        |   ; CALL XREF from uart_update_mode @ 0x800035a(x)
|       |   ; CODE XREF from set_decryption_key @ 0x80011ce(x)
        |   ; DATA XREF from set_decryption_key @ 0x80011c2(r)
|       |   ; CODE XREF from lcd_gpio_init @ 0x8000406(x)
|       `-> 0x080011d8      2de9f843       push.w {r3, r4, r5, r6, r7, r8, sb, lr} ; arg1
|           0x080011dc      0121           movs r1, 1
|           0x080011de      7d20           movs r0, 0x7d               ; '}'
|           0x080011e0      00f07afb       bl systick_deinit
|           0x080011e4      0121           movs r1, 1
|           0x080011e6      6448           ldr r0, [0x08001378]        ; [0x8001378:4]=0x8084000
|           0x080011e8      00f068fb       bl systick_init
|           0x080011ec      0121           movs r1, 1
|           0x080011ee      4806           lsls r0, r1, 0x19
|           0x080011f0      fff7aafd       bl uart_irq_handler
|           0x080011f4      6846           mov r0, sp
|           0x080011f6      fff7d8ff       bl fcn.080011aa
|           0x080011fa      48f6c850       movw r0, 0x8dc8
|           0x080011fe      adf80000       strh.w r0, [sp]
|           0x08001202      0126           movs r6, 1
|           0x08001204      8df80260       strb.w r6, [var_2h]
|           0x08001208      1024           movs r4, 0x10
|           0x0800120a      8df80340       strb.w r4, [var_3h]
|           0x0800120e      5b4f           ldr r7, [0x0800137c]        ; [0x800137c:4]=0x40010800
|           0x08001210      6946           mov r1, sp
|           0x08001212      3846           mov r0, r7
|           0x08001214      fff744fd       bl spi_flash_read_id
|           0x08001218      e101           lsls r1, r4, 7
|           0x0800121a      3846           mov r0, r7
|           0x0800121c      fff7c3ff       bl fcn.080011a6
|           0x08001220      4ff48058       mov.w r8, 0x1000
|           0x08001224      adf80080       strh.w r8, [sp]
|           0x08001228      8df80260       strb.w r6, [var_2h]
|           0x0800122c      4825           movs r5, 0x48               ; 'H'
|           0x0800122e      8df80350       strb.w r5, [var_3h]
|           0x08001232      6946           mov r1, sp
|           0x08001234      3846           mov r0, r7
|           0x08001236      fff733fd       bl spi_flash_read_id
|           0x0800123a      6846           mov r0, sp
|           0x0800123c      fff7b5ff       bl fcn.080011aa
|           0x08001240      41f6cf30       movw r0, 0x1bcf
|           0x08001244      adf80000       strh.w r0, [sp]
|           0x08001248      0227           movs r7, 2
|           0x0800124a      8df80270       strb.w r7, [var_2h]
|           0x0800124e      8df80340       strb.w r4, [var_3h]
|           0x08001252      dff82c91       ldr.w sb, [0x08001380]      ; [0x8001380:4]=0x40010c00
|           0x08001256      6946           mov r1, sp
|           0x08001258      4846           mov r0, sb
|           0x0800125a      fff721fd       bl spi_flash_read_id
|           0x0800125e      4ff48d51       mov.w r1, 0x11a0
|           0x08001262      4846           mov r0, sb
|           0x08001264      fff7a9ff       bl fcn.080011ba
|           0x08001268      3020           movs r0, 0x30               ; '0'
|           0x0800126a      adf80000       strh.w r0, [sp]
|           0x0800126e      8df80260       strb.w r6, [var_2h]
|           0x08001272      8df80350       strb.w r5, [var_3h]
|           0x08001276      6946           mov r1, sp
|           0x08001278      4846           mov r0, sb
|           0x0800127a      fff711fd       bl spi_flash_read_id
|           0x0800127e      4ff42040       mov.w r0, 0xa000
|           0x08001282      adf80000       strh.w r0, [sp]
|           0x08001286      8df80270       strb.w r7, [var_2h]
|           0x0800128a      1820           movs r0, 0x18
|           0x0800128c      8df80300       strb.w r0, [var_3h]
|           0x08001290      6946           mov r1, sp
|           0x08001292      4846           mov r0, sb
|           0x08001294      fff704fd       bl spi_flash_read_id
|           0x08001298      a002           lsls r0, r4, 0xa
|           0x0800129a      adf80000       strh.w r0, [sp]
|           0x0800129e      0420           movs r0, 4
|           0x080012a0      8df80300       strb.w r0, [var_3h]
|           0x080012a4      6946           mov r1, sp
|           0x080012a6      4846           mov r0, sb
|           0x080012a8      fff7fafc       bl spi_flash_read_id
|           0x080012ac      6846           mov r0, sp
|           0x080012ae      fff77cff       bl fcn.080011aa
|           0x080012b2      47f67f20       movw r0, 0x7a7f             ; '\x7fz'
|           0x080012b6      adf80000       strh.w r0, [sp]
|           0x080012ba      8df80270       strb.w r7, [var_2h]
|           0x080012be      8df80340       strb.w r4, [var_3h]
|           0x080012c2      dff8c090       ldr.w sb, [0x08001384]      ; [0x8001384:4]=0x40011000
|           0x080012c6      6946           mov r1, sp
|           0x080012c8      4846           mov r0, sb
|           0x080012ca      fff7e9fc       bl spi_flash_read_id
|           0x080012ce      1021           movs r1, 0x10
|           0x080012d0      4846           mov r0, sb
|           0x080012d2      fff766ff       bl fcn.080011a2
|           0x080012d6      4021           movs r1, 0x40               ; '@'
|           0x080012d8      4846           mov r0, sb
|           0x080012da      fff762ff       bl fcn.080011a2
|           0x080012de      4146           mov r1, r8
|           0x080012e0      4846           mov r0, sb
|           0x080012e2      fff75eff       bl fcn.080011a2
|           0x080012e6      48f28011       movw r1, 0x8180
|           0x080012ea      adf80010       strh.w r1, [sp]
|           0x080012ee      8df80350       strb.w r5, [var_3h]
|           0x080012f2      6946           mov r1, sp
|           0x080012f4      4846           mov r0, sb
|           0x080012f6      fff7d3fc       bl spi_flash_read_id
|           0x080012fa      6846           mov r0, sp
|           0x080012fc      fff755ff       bl fcn.080011aa
|           0x08001300      4ff60f78       movw r8, 0xff0f
|           0x08001304      adf80080       strh.w r8, [sp]
|           0x08001308      8df80270       strb.w r7, [var_2h]
|           0x0800130c      8df80340       strb.w r4, [var_3h]
|           0x08001310      1d4f           ldr r7, [0x08001388]        ; [0x8001388:4]=0x40011400
|           0x08001312      6946           mov r1, sp
|           0x08001314      3846           mov r0, r7
|           0x08001316      fff7c3fc       bl spi_flash_read_id
|           0x0800131a      4146           mov r1, r8
|           0x0800131c      3846           mov r0, r7
|           0x0800131e      fff74cff       bl fcn.080011ba
|           0x08001322      f021           movs r1, 0xf0
|           0x08001324      adf80010       strh.w r1, [sp]
|           0x08001328      8df80350       strb.w r5, [var_3h]
|           0x0800132c      6946           mov r1, sp
|           0x0800132e      3846           mov r0, r7
|           0x08001330      fff7b6fc       bl spi_flash_read_id
|           0x08001334      6846           mov r0, sp
|           0x08001336      fff738ff       bl fcn.080011aa
|           0x0800133a      4ff69270       movw r0, 0xff92
|           0x0800133e      adf80000       strh.w r0, [sp]
|           0x08001342      8df80260       strb.w r6, [var_2h]
|           0x08001346      8df80340       strb.w r4, [var_3h]
|           0x0800134a      104c           ldr r4, [0x0800138c]        ; [0x800138c:4]=0x40011800
|           0x0800134c      6946           mov r1, sp
|           0x0800134e      2046           mov r0, r4
|           0x08001350      fff7a6fc       bl spi_flash_read_id
|           0x08001354      4ff40d41       mov.w r1, 0x8d00
|           0x08001358      2046           mov r0, r4
|           0x0800135a      fff72eff       bl fcn.080011ba
|           0x0800135e      6d21           movs r1, 0x6d               ; 'm'
|           0x08001360      adf80010       strh.w r1, [sp]
|           0x08001364      8df80350       strb.w r5, [var_3h]
|           0x08001368      6946           mov r1, sp
|           0x0800136a      2046           mov r0, r4
|           0x0800136c      fff798fc       bl spi_flash_read_id
|           0x08001370      00f090f8       bl fcn.08001494
\           0x08001374      bde8f883       pop.w {r3, r4, r5, r6, r7, r8, sb, pc}

; CALL XREF from check_update_button @ 0x8000530(x)
/ 134: crc_ccitt_spi ();
|           0x0800040c      2de9f047       push.w {r4, r5, r6, r7, r8, sb, sl, lr}
|           0x08000410      0024           movs r4, 0
|           0x08000412      dff880a0       ldr.w sl, [0x08000494]      ; [0x8000494:4]=0x40010c00
|           0x08000416      4ff48059       mov.w sb, 0x1000
|           0x0800041a      4946           mov r1, sb
|           0x0800041c      5046           mov r0, sl
|           0x0800041e      00f0c0fe       bl fcn.080011a2
|           0x08000422      0120           movs r0, 1
|           0x08000424      00f0f1f9       bl fcn.0800080a
|           0x08000428      0320           movs r0, 3
|           0x0800042a      01f051fc       bl lcd_fill_color
|           0x0800042e      3020           movs r0, 0x30               ; '0'
|           0x08000430      01f04efc       bl lcd_fill_color
|           0x08000434      0120           movs r0, 1
|           0x08000436      01f04bfc       bl lcd_fill_color
|           0x0800043a      0020           movs r0, 0
|           0x0800043c      01f048fc       bl lcd_fill_color
|           0x08000440      0025           movs r5, 0
|           0x08000442      41f22106       movw r6, 0x1021             ; '!\x10'
|           0x08000446      4ff6ff77       movw r7, 0xffff
|           0x0800044a      dff84c80       ldr.w r8, [0x08000498]      ; [0x8000498:4]=0x20000468
|       ,=< 0x0800044e      12e0           b 0x8000476
|       |   ; CODE XREF from crc_ccitt_spi @ 0x800047c(x)
|      .--> 0x08000450      01f0bafb       bl lcd_spi_write_cmd
|      :|   0x08000454      84ea0020       eor.w r0, r4, r0, lsl 8
|      :|   0x08000458      84b2           uxth r4, r0
|      :|   0x0800045a      0020           movs r0, 0
|      :|   ; CODE XREF from crc_ccitt_spi @ 0x8000472(x)
|     .---> 0x0800045c      2104           lsls r1, r4, 0x10
|    ,====< 0x0800045e      03d5           bpl 0x8000468
|    |::|   0x08000460      86ea4401       eor.w r1, r6, r4, lsl 1
|    |::|   0x08000464      8cb2           uxth r4, r1
|   ,=====< 0x08000466      01e0           b 0x800046c
|   ||::|   ; CODE XREF from crc_ccitt_spi @ 0x800045e(x)
|   |`----> 0x08000468      07ea4404       and.w r4, r7, r4, lsl 1
|   | ::|   ; CODE XREF from crc_ccitt_spi @ 0x8000466(x)
|   `-----> 0x0800046c      401c           adds r0, r0, 1
|     ::|   0x0800046e      c0b2           uxtb r0, r0
|     ::|   0x08000470      0828           cmp r0, 8                   ; 8
|     `===< 0x08000472      f3d3           blo 0x800045c
|      :|   0x08000474      6d1c           adds r5, r5, 1
|      :|   ; CODE XREF from crc_ccitt_spi @ 0x800044e(x)
|      :`-> 0x08000476      d8f80200       ldr.w r0, [r8, 2]
|      :    0x0800047a      a842           cmp r0, r5
|      `==< 0x0800047c      e8d8           bhi 0x8000450
|           0x0800047e      4946           mov r1, sb
|           0x08000480      5046           mov r0, sl
|           0x08000482      00f090fe       bl fcn.080011a6
|           0x08000486      0120           movs r0, 1
|           0x08000488      00f0bff9       bl fcn.0800080a
|           0x0800048c      2046           mov r0, r4
\           0x0800048e      bde8f087       pop.w {r4, r5, r6, r7, r8, sb, sl, pc}

; CALL XREF from main @ 0x80022b6(x)
/ 312: check_update_button ();
|           0x0800049c      70b5           push {r4, r5, r6, lr}
|           0x0800049e      4d4d           ldr r5, [0x080005d4]        ; [0x80005d4:4]=0x40010800
|           0x080004a0      4ff48054       mov.w r4, 0x1000
|           0x080004a4      2146           mov r1, r4
|           0x080004a6      2846           mov r0, r5
|           0x080004a8      00f074fe       bl flash_unlock
|       ,=< 0x080004ac      88b9           cbnz r0, 0x80004d2
|       |   0x080004ae      4a4e           ldr r6, [0x080005d8]        ; [0x80005d8:4]=0x40011800
|       |   0x080004b0      2021           movs r1, 0x20
|       |   0x080004b2      3046           mov r0, r6
|       |   0x080004b4      00f06efe       bl flash_unlock
|      ,==< 0x080004b8      58b9           cbnz r0, 0x80004d2
|      ||   0x080004ba      01f065f8       bl flash_erase_sector
|      ||   0x080004be      2146           mov r1, r4
|      ||   0x080004c0      2846           mov r0, r5
|      ||   0x080004c2      00f067fe       bl flash_unlock
|     ,===< 0x080004c6      20b9           cbnz r0, 0x80004d2
|     |||   0x080004c8      2021           movs r1, 0x20
|     |||   0x080004ca      3046           mov r0, r6
|     |||   0x080004cc      00f062fe       bl flash_unlock
|    ,====< 0x080004d0      08b3           cbz r0, 0x8000516
|    ||||   ; CODE XREFS from check_update_button @ 0x80004ac(x), 0x80004b8(x), 0x80004c6(x)
|    |```-> 0x080004d2      4ff44015       mov.w r5, 0x300000
|    |      0x080004d6      0822           movs r2, 8
|    |      0x080004d8      4049           ldr r1, [0x080005dc]        ; [0x80005dc:4]=0x20000468
|    |      0x080004da      2846           mov r0, r5
|    |      0x080004dc      01f09efb       bl lcd_spi_write_data
|    |      0x080004e0      3e4c           ldr r4, [0x080005dc]        ; [0x80005dc:4]=0x20000468
|    |      0x080004e2      2188           ldrh r1, [r4]
|    |      0x080004e4      a1f52540       sub.w r0, r1, 0xa500
|    |      0x080004e8      5a38           subs r0, 0x5a
|    |  ,=< 0x080004ea      40d1           bne 0x800056e
|    |  |   0x080004ec      e088           ldrh r0, [r4, 6]
|    |  |   0x080004ee      a0f57f41       sub.w r1, r0, 0xff00
|    |  |   0x080004f2      ff39           subs r1, 0xff
|    | ,==< 0x080004f4      3bd0           beq 0x800056e
|    |,===< 0x080004f6      c8b3           cbz r0, 0x800056c
|    ||||   0x080004f8      d4f80200       ldr.w r0, [r4, 2]
|    ||||   0x080004fc      b0f57d2f       cmp.w r0, 0xfd000
|   ,=====< 0x08000500      35d2           bhs 0x800056e
|   |||||   0x08000502      0822           movs r2, 8
|   |||||   0x08000504      04f10801       add.w r1, r4, 8
|   |||||   0x08000508      3548           ldr r0, [0x080005e0]        ; [0x80005e0:4]=0x300700
|   |||||   0x0800050a      01f087fb       bl lcd_spi_write_data
|   |||||   0x0800050e      207a           ldrb r0, [r4, 8]
|   |||||   0x08000510      5628           cmp r0, 0x56                ; 86
|  ,======< 0x08000512      0bd1           bne 0x800052c
| ,=======< 0x08000514      04e0           b 0x8000520
| |||||||   ; CODE XREF from check_update_button @ 0x80004d0(x)
| |||`----> 0x08000516      0120           movs r0, 1
| ||| |||   0x08000518      00f07ef9       bl lcd_show_status
| ||| |||   0x0800051c      0120           movs r0, 1
| ||| |||   0x0800051e      70bd           pop {r4, r5, r6, pc}
| ||| |||   ; CODE XREF from check_update_button @ 0x8000514(x)
| `-------> 0x08000520      607a           ldrb r0, [r4, 9]
|  || |||   0x08000522      6528           cmp r0, 0x65                ; 101
|  ||,====< 0x08000524      02d1           bne 0x800052c
|  ||||||   0x08000526      a07a           ldrb r0, [r4, 0xa]
|  ||||||   0x08000528      7228           cmp r0, 0x72                ; 114
| ,=======< 0x0800052a      01d0           beq 0x8000530
| |||||||   ; CODE XREFS from check_update_button @ 0x8000512(x), 0x8000524(x)
| |`-`----> 0x0800052c      0020           movs r0, 0
| | | |||   0x0800052e      70bd           pop {r4, r5, r6, pc}
| | | |||   ; CODE XREF from check_update_button @ 0x800052a(x)
| `-------> 0x08000530      fff76cff       bl crc_ccitt_spi
|   | |||   0x08000534      e188           ldrh r1, [r4, 6]
|   | |||   0x08000536      8842           cmp r0, r1
|   |,====< 0x08000538      19d1           bne 0x800056e
|   |||||   0x0800053a      01f025f8       bl flash_erase_sector
|   |||||   0x0800053e      0020           movs r0, 0
|   |||||   0x08000540      00f06af9       bl lcd_show_status
|   |||||   0x08000544      00f030fa       bl spi_cmd_read_status
|  ,======< 0x08000548      58b1           cbz r0, 0x8000562
|  ||||||   0x0800054a      0020           movs r0, 0
|  ||||||   0x0800054c      2080           strh r0, [r4]
|  ||||||   0x0800054e      0822           movs r2, 8
|  ||||||   0x08000550      2249           ldr r1, [0x080005dc]        ; [0x80005dc:4]=0x20000468
|  ||||||   0x08000552      2846           mov r0, r5
|  ||||||   0x08000554      01f02cfc       bl lcd_init_st7789
|  ||||||   0x08000558      0320           movs r0, 3
|  ||||||   0x0800055a      00f05df9       bl lcd_show_status
|  ||||||   0x0800055e      01f099f9       bl systick_delay
|  ||||||   ; CODE XREF from check_update_button @ 0x8000548(x)
|  `------> 0x08000562      0220           movs r0, 2
|   |||||   0x08000564      00f058f9       bl lcd_show_status
|   |||||   0x08000568      0020           movs r0, 0
|   |||||   ; CODE XREFS from check_update_button @ 0x80005bc(x), 0x80005d2(x)
| ..------> 0x0800056a      70bd           pop {r4, r5, r6, pc}
| ::|||||   ; CODE XREF from check_update_button @ 0x80004f6(x)
| ====`---> 0x0800056c      ffe7           b 0x800056e
| ::|| ||   ; CODE XREFS from check_update_button @ 0x80004ea(x), 0x80004f4(x), 0x8000500(x), 0x8000538(x), 0x800056c(x)
| --``-``-> 0x0800056e      1d4d           ldr r5, [0x080005e4]        ; [0x80005e4:4]=0x2000000c
| ::        0x08000570      c820           movs r0, 0xc8
| ::        0x08000572      2861           str r0, [r5, 0x10]
| ::        0x08000574      01f0c8fc       bl timer_reset
| ::        0x08000578      1b4c           ldr r4, [0x080005e8]        ; [0x80005e8:4]=0x20000044
| ::    ,=< 0x0800057a      1ce0           b 0x80005b6
| ::    |   ; CODE XREF from check_update_button @ 0x80005ba(x)
| ::   .--> 0x0800057c      94f81204       ldrb.w r0, [r4, 0x412]
| ::  ,===< 0x08000580      c8b1           cbz r0, 0x80005b6
| ::  |:|   0x08000582      b4f81004       ldrh.w r0, [r4, 0x410]
| ::  |:|   0x08000586      0928           cmp r0, 9                   ; 9
| :: ,====< 0x08000588      13d1           bne 0x80005b2
| :: ||:|   0x0800058a      2078           ldrb r0, [r4]
| :: ||:|   0x0800058c      aa28           cmp r0, 0xaa                ; 170
| ::,=====< 0x0800058e      10d1           bne 0x80005b2
| ::|||:|   0x08000590      6078           ldrb r0, [r4, 1]
| ::|||:|   0x08000592      4228           cmp r0, 0x42                ; 66
| ========< 0x08000594      0dd1           bne 0x80005b2
| ::|||:|   0x08000596      a078           ldrb r0, [r4, 2]
| ========< 0x08000598      58b9           cbnz r0, 0x80005b2
| ::|||:|   0x0800059a      e078           ldrb r0, [r4, 3]
| ========< 0x0800059c      48b9           cbnz r0, 0x80005b2
| ::|||:|   0x0800059e      2079           ldrb r0, [r4, 4]
| ========< 0x080005a0      38b9           cbnz r0, 0x80005b2
| ::|||:|   0x080005a2      6079           ldrb r0, [r4, 5]
| ========< 0x080005a4      28b9           cbnz r0, 0x80005b2
| ::|||:|   0x080005a6      a079           ldrb r0, [r4, 6]
| ::|||:|   0x080005a8      5528           cmp r0, 0x55                ; 85
| ========< 0x080005aa      02d1           bne 0x80005b2
| ::|||:|   0x080005ac      e079           ldrb r0, [r4, 7]
| ::|||:|   0x080005ae      eb28           cmp r0, 0xeb                ; 235
| ========< 0x080005b0      05d0           beq 0x80005be
| ::|||:|   ; XREFS: CODE 0x08000588  CODE 0x0800058e  CODE 0x08000594  CODE 0x08000598  CODE 0x0800059c  
| ::|||:|   ; XREFS: CODE 0x080005a0  CODE 0x080005a4  CODE 0x080005aa  
| --``----> 0x080005b2      01f0a9fc       bl timer_reset
| ::  |:|   ; CODE XREFS from check_update_button @ 0x800057a(x), 0x8000580(x)
| ::  `-`-> 0x080005b6      2869           ldr r0, [r5, 0x10]
| ::   :    0x080005b8      0028           cmp r0, 0
| ::   `==< 0x080005ba      dfd1           bne 0x800057c
| `=======< 0x080005bc      d5e7           b 0x800056a
|  :        ; CODE XREF from check_update_button @ 0x80005b0(x)
| --------> 0x080005be      0621           movs r1, 6
|  :        0x080005c0      4220           movs r0, 0x42               ; 'B'
|  :        0x080005c2      fff7fdfe       bl send_response
|  :        0x080005c6      00f0dfff       bl flash_erase_sector
|  :        0x080005ca      0120           movs r0, 1
|  :        0x080005cc      00f024f9       bl lcd_show_status
|  :        0x080005d0      0120           movs r0, 1
\  `======< 0x080005d2      cae7           b 0x800056a

; CALL XREF from uart_update_mode @ 0x800026c(x)
; --------------------------------------------------------------------------
; packet_validate: Validate received UART packet framing and CRC.
;
; C pseudocode:
;   int packet_validate(void) {
;       uint8_t *buf = rx_buf;             // 0x20000044
;       uint16_t len = *(uint16_t*)(buf + 0x410);  // packet length
;       if (buf[0] != 0xAA) return 0;      // header check
;       if (buf[len - 1] != 0x55) return 0;// trailer check
;       uint16_t crc = crc_ccitt_calc(&buf[1], len - 4);
;       uint16_t pkt_crc = (buf[len-3] << 8) | buf[len-2];
;       return (crc == pkt_crc) ? 1 : 0;
;   }
; --------------------------------------------------------------------------
/ 78: packet_validate ();
|           0x080005ec      10b5           push {r4, lr}
|           0x080005ee      134c           ldr r4, [0x0800063c]        ; [0x800063c:4]=0x20000044
|           ; Check header byte == 0xAA
|           0x080005f0      2078           ldrb r0, [r4]
|           0x080005f2      aa28           cmp r0, 0xaa                ; 170
|       ,=< 0x080005f4      05d1           bne 0x8000602               ; not 0xAA → fail
|       |   ; Check trailer byte == 0x55 at buf[len-1]
|       |   0x080005f6      b4f81014       ldrh.w r1, [r4, 0x410]
|       |   0x080005fa      601e           subs r0, r4, 1
|       |   0x080005fc      085c           ldrb r0, [r1, r0]
|       |   0x080005fe      5528           cmp r0, 0x55                ; 85
|      ,==< 0x08000600      01d0           beq 0x8000606               ; trailer OK → check CRC
|      ||   ; CODE XREF from packet_validate @ 0x80005f4(x)
|           ; Framing failed → return 0
|      |`-> 0x08000602      0020           movs r0, 0
|      |    0x08000604      10bd           pop {r4, pc}
|      |    ; CODE XREF from packet_validate @ 0x8000600(x)
|           ; Compute CRC-CCITT over buf[1..len-4], compare to packet CRC
|      `--> 0x08000606      b4f81004       ldrh.w r0, [r4, 0x410]
|           0x0800060a      001f           subs r0, r0, 4              ; CRC covers len-4 bytes
|           0x0800060c      81b2           uxth r1, r0
|           0x0800060e      601c           adds r0, r4, 1              ; skip header byte (0xAA)
|           0x08000610      00f0b6f8       bl crc_ccitt_calc           ; r0 = computed CRC
|           ; Extract packet CRC from buf[len-3] (hi) and buf[len-2] (lo)
|           0x08000614      b4f81024       ldrh.w r2, [r4, 0x410]
|           0x08000618      0849           ldr r1, [0x0800063c]        ; [0x800063c:4]=0x20000044
|           0x0800061a      c91e           subs r1, r1, 3
|           0x0800061c      515c           ldrb r1, [r2, r1]
|           0x0800061e      b4f81034       ldrh.w r3, [r4, 0x410]
|           0x08000622      064a           ldr r2, [0x0800063c]        ; [0x800063c:4]=0x20000044
|           0x08000624      921e           subs r2, r2, 2
|           0x08000626      9a5c           ldrb r2, [r3, r2]
|           0x08000628      02eb0121       add.w r1, r2, r1, lsl 8     ; r1 = (crc_hi << 8) | crc_lo
|           0x0800062c      89b2           uxth r1, r1
|           ; Compare computed CRC vs packet CRC
|           0x0800062e      8842           cmp r0, r1
|       ,=< 0x08000630      01d0           beq 0x8000636               ; match → return 1
|       |   0x08000632      0020           movs r0, 0                  ; CRC mismatch → return 0
|       |   0x08000634      10bd           pop {r4, pc}
|       |   ; CODE XREF from packet_validate @ 0x8000630(x)
|       `-> 0x08000636      0120           movs r0, 1
\           0x08000638      10bd           pop {r4, pc}

; CALL XREF from main @ 0x80022c8(x)
/ 16: check_spi_flag ();
|           0x08000640      0348           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000642      0068           ldr r0, [r0]                ; 0x8002d00
|                                                                      ; aav.0x08002d00
|           0x08000644      0028           cmp r0, 0
|       ,=< 0x08000646      01d0           beq 0x800064c
|       |   0x08000648      0120           movs r0, 1
|       |   0x0800064a      7047           bx lr
|       |   ; CODE XREF from check_spi_flag @ 0x8000646(x)
|       `-> 0x0800064c      0020           movs r0, 0
\           0x0800064e      7047           bx lr

; CALL XREF from main @ 0x80022c2(x)
/ 68: check_spi_model (int16_t arg2, int16_t arg3, int16_t arg4);
| `- args(r1, r2, r3) vars(5:sp[0x8..0x34])
|           0x08000654      00b5           push {lr}
|           0x08000656      8db0           sub sp, 0x34
|           0x08000658      0fa0           adr r0, 0x3c                ; "RT-950      "
|                                                                      ; 0x8000698
|           0x0800065a      90e80e00       ldm.w r0, {r1, r2, r3}
|           0x0800065e      c068           ldr r0, [r0, 0xc]
|           0x08000660      cde90a23       strd r2, r3, [var_28h]      ; arg4
|           0x08000664      0991           str r1, [var_24h]           ; arg2
|           0x08000666      0c90           str r0, [var_30h]
|           0x08000668      2022           movs r2, 0x20
|           0x0800066a      01a9           add r1, var_4h
|           0x0800066c      0e48           ldr r0, [0x080006a8]        ; [0x80006a8:4]=0x80037e0
|           0x0800066e      00f090fe       bl hardfault_handler
|           0x08000672      09a8           add r0, var_24h
|           0x08000674      fff7b0fd       bl memcmp_model
|           0x08000678      0246           mov r2, r0
|           0x0800067a      01a9           add r1, var_4h
|           0x0800067c      09a8           add r0, var_24h
|           0x0800067e      fff7b2fd       bl memcmp_buf
|       ,=< 0x08000682      38b1           cbz r0, 0x8000694
|       |   0x08000684      00f080ff       bl flash_erase_sector
|       |   0x08000688      0420           movs r0, 4
|       |   0x0800068a      00f0c5f8       bl lcd_show_status
|       |   0x0800068e      0020           movs r0, 0
|       |   ; CODE XREF from check_spi_model @ 0x8000696(x)
|      .--> 0x08000690      0db0           add sp, 0x34
|      :|   0x08000692      00bd           pop {pc}
|      :|   ; CODE XREF from check_spi_model @ 0x8000682(x)
|      :`-> 0x08000694      0120           movs r0, 1
\      `==< 0x08000696      fbe7           b 0x8000690

; --------------------------------------------------------------------------
; uart_rx_handler: UART receive state machine / watchdog handler.
;
; C pseudocode:
;   void uart_rx_handler(void) {
;       uint32_t *state = (uint32_t *)0x2000000C;
;       if (state[1] < 100) return;         // not enough ticks yet
;       state[1] = 0;                       // reset tick counter
;       if (uart_check_data(UART4, 1) != 1) { state[0] = 0; return; }
;       if (state[0] != 0) {                // already initialized
;           gpio_set(GPIOA, 0x800);         // toggle LED
;           gpio_read_pin(100);             // debounce
;           systick_delay();
;       }
;       state[0] = 1;                       // mark UART active
;   }
; --------------------------------------------------------------------------
/ 60: uart_rx_handler ();
|           0x080006ac      70b5           push {r4, r5, r6, lr}
|           ; r4 = state structure at 0x2000000C
|           0x080006ae      0e4c           ldr r4, [0x080006e8]        ; [0x80006e8:4]=0x2000000c
|           ; Check timeout counter at state[4]
|           0x080006b0      6068           ldr r0, [r4, 4]
|           0x080006b2      6428           cmp r0, 0x64                ; 100
|       ,=< 0x080006b4      08d3           blo 0x80006c8               ; < 100 ticks → return
|       |   0x080006b6      0025           movs r5, 0
|       |   0x080006b8      6560           str r5, [r4, 4]
|       |   0x080006ba      0121           movs r1, 1                  ; uint32_t arg2
|       |   0x080006bc      0b48           ldr r0, [0x080006ec]        ; [0x80006ec:4]=0x40011800 ; int16_t arg1
|       |   0x080006be      00f069fd       bl flash_unlock
|       |   0x080006c2      0128           cmp r0, 1                   ; 1
|      ,==< 0x080006c4      01d0           beq 0x80006ca
|      ||   0x080006c6      2570           strb r5, [r4]
|      ||   ; CODE XREF from uart_rx_handler @ 0x80006b4(x)
|      |`-> 0x080006c8      70bd           pop {r4, r5, r6, pc}
|      |    ; CODE XREF from uart_rx_handler @ 0x80006c4(x)
|      `--> 0x080006ca      2078           ldrb r0, [r4]
|       ,=< 0x080006cc      48b1           cbz r0, 0x80006e2
|       |   0x080006ce      4ff40061       mov.w r1, 0x800             ; int16_t arg2
|       |   0x080006d2      0748           ldr r0, [0x080006f0]        ; [0x80006f0:4]=0x40010800 ; int16_t arg1
|       |   0x080006d4      00f065fd       bl fcn.080011a2
|       |   0x080006d8      6420           movs r0, 0x64               ; 'd' ; int16_t arg1
|       |   0x080006da      00f088f8       bl gpio_read_pin
|       |   0x080006de      01f0d9f8       bl systick_delay
|       |   ; CODE XREF from uart_rx_handler @ 0x80006cc(x)
|       `-> 0x080006e2      0120           movs r0, 1
|           0x080006e4      2070           strb r0, [r4]
\           0x080006e6      70bd           pop {r4, r5, r6, pc}

; CALL XREF from main @ 0x80022d8(x)
/ 136: clear_spi_update ();
|           0x080006f4      10b5           push {r4, lr}
|           0x080006f6      214c           ldr r4, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x080006f8      72b6           cpsid i
|           0x080006fa      00f083fa       bl spi_cs_low
|           0x080006fe      0021           movs r1, 0
|           0x08000700      2046           mov r0, r4
|           0x08000702      00f02ffa       bl spi_transaction
|           0x08000706      1d48           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000708      0021           movs r1, 0
|           0x0800070a      001d           adds r0, r0, 4
|           0x0800070c      00f02afa       bl spi_transaction
|           0x08000710      1a48           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000712      0021           movs r1, 0
|           0x08000714      0830           adds r0, 8
|           0x08000716      00f025fa       bl spi_transaction
|           0x0800071a      1848           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x0800071c      0021           movs r1, 0
|           0x0800071e      0c30           adds r0, 0xc
|           0x08000720      00f020fa       bl spi_transaction
|           0x08000724      1548           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000726      0021           movs r1, 0
|           0x08000728      1030           adds r0, 0x10
|           0x0800072a      00f01bfa       bl spi_transaction
|           0x0800072e      1348           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000730      0021           movs r1, 0
|           0x08000732      1430           adds r0, 0x14
|           0x08000734      00f016fa       bl spi_transaction
|           0x08000738      1048           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x0800073a      0021           movs r1, 0
|           0x0800073c      1830           adds r0, 0x18
|           0x0800073e      00f011fa       bl spi_transaction
|           0x08000742      0e48           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000744      0021           movs r1, 0
|           0x08000746      1c30           adds r0, 0x1c
|           0x08000748      00f00cfa       bl spi_transaction
|           0x0800074c      0b48           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x0800074e      0021           movs r1, 0
|           0x08000750      2030           adds r0, 0x20
|           0x08000752      00f007fa       bl spi_transaction
|           0x08000756      0948           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000758      0021           movs r1, 0
|           0x0800075a      2430           adds r0, 0x24
|           0x0800075c      00f002fa       bl spi_transaction
|           0x08000760      0648           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x08000762      0021           movs r1, 0
|           0x08000764      2830           adds r0, 0x28
|           0x08000766      00f0fdf9       bl spi_transaction
|           0x0800076a      0448           ldr r0, aav.0x08002d00      ; [0x8002d00:4]=0
|           0x0800076c      0021           movs r1, 0
|           0x0800076e      2c30           adds r0, 0x2c
|           0x08000770      00f0f8f9       bl spi_transaction
|           0x08000774      00f0eaf9       bl spi_wait_busy
|           0x08000778      62b6           cpsie i
\           0x0800077a      10bd           pop {r4, pc}

; CALL XREF from send_response @ 0x80003d8(x)
            ; CALL XREF from packet_validate @ 0x8000610(x)
; --------------------------------------------------------------------------
; crc_ccitt_calc: CRC-CCITT (0xFFFF initial, polynomial 0x1021) calculator.
;
; C pseudocode:
;   uint16_t crc_ccitt_calc(uint8_t *data, uint16_t len) {
;       uint16_t crc = 0;
;       for (uint16_t i = 0; i < len; i++) {
;           crc ^= (uint16_t)data[i] << 8;
;           for (int bit = 0; bit < 8; bit++) {
;               if (crc & 0x8000)
;                   crc = (crc << 1) ^ 0x1021;
;               else
;                   crc = (crc << 1) & 0xFFFF;
;           }
;       }
;       return crc;
;   }
; --------------------------------------------------------------------------
/ 62: crc_ccitt_calc (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x08000780      f0b5           push {r4, r5, r6, r7, lr}
|           0x08000782      0646           mov r6, r0                  ; r6 = data pointer
|           0x08000784      0020           movs r0, 0                  ; crc = 0
|           0x08000786      0023           movs r3, 0                  ; byte index = 0
|           ; r4 = polynomial 0x1021, r5 = mask 0xFFFF
|           0x08000788      41f22104       movw r4, 0x1021             ; '!\x10'
|           0x0800078c      4ff6ff75       movw r5, 0xffff
|       ,=< 0x08000790      12e0           b 0x80007b8
|       |   ; CODE XREF from crc_ccitt_calc @ 0x80007ba(x)
|           ; Outer loop: for each byte
|      .--> 0x08000792      f25c           ldrb r2, [r6, r3]           ; data[i]
|      :|   0x08000794      80ea0220       eor.w r0, r0, r2, lsl 8    ; crc ^= data[i] << 8
|      :|   0x08000798      80b2           uxth r0, r0
|      :|   0x0800079a      0022           movs r2, 0                  ; bit counter
|      :|   ; CODE XREF from crc_ccitt_calc @ 0x80007b2(x)
|           ; Inner loop: 8 bits per byte
|     .---> 0x0800079c      0704           lsls r7, r0, 0x10          ; test bit 15
|    ,====< 0x0800079e      03d5           bpl 0x80007a8               ; bit 15 clear → shift only
|    |::|   ; bit 15 set → crc = (crc << 1) ^ 0x1021
|    |::|   0x080007a0      84ea4000       eor.w r0, r4, r0, lsl 1
|    |::|   0x080007a4      80b2           uxth r0, r0
|   ,=====< 0x080007a6      01e0           b 0x80007ac
|   ||::|   ; CODE XREF from crc_ccitt_calc @ 0x800079e(x)
|           ; bit 15 clear → crc = (crc << 1) & 0xFFFF
|   |`----> 0x080007a8      05ea4000       and.w r0, r5, r0, lsl 1
|   | ::|   ; CODE XREF from crc_ccitt_calc @ 0x80007a6(x)
|   `-----> 0x080007ac      521c           adds r2, r2, 1
|     ::|   0x080007ae      d2b2           uxtb r2, r2
|     ::|   0x080007b0      082a           cmp r2, 8                   ; 8
|     `===< 0x080007b2      f3d3           blo 0x800079c
|      :|   0x080007b4      5b1c           adds r3, r3, 1
|      :|   0x080007b6      9bb2           uxth r3, r3
|      :|   ; CODE XREF from crc_ccitt_calc @ 0x8000790(x)
|      :`-> 0x080007b8      8b42           cmp r3, r1                  ; arg2
|      `==< 0x080007ba      ead3           blo 0x8000792
\           0x080007bc      f0bd           pop {r4, r5, r6, r7, pc}

; CALL XREFS from flash_from_spi @ 0x800142e(x), 0x8001440(x)
; --------------------------------------------------------------------------
; rotate_byte (fcn.080007be): Single-bit byte rotation for key schedule.
;
; C pseudocode:
;   uint8_t rotate_byte(uint8_t val, int direction) {
;       if (direction != 0)
;           return (val << 1) | (val >> 7);   // ROL8 by 1
;       else
;           return (val >> 1) | (val << 7);   // ROR8 by 1
;   }
;
; Called from flash_from_spi key rotation loop:
;   - direction=1 for key bytes 0-7  (ROL8)
;   - direction=0 for key bytes 8-15 (ROR8)
; --------------------------------------------------------------------------
/ 26: fcn.080007be (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x080007be      0246           mov r2, r0                  ; r2 = val
|           0x080007c0      0029           cmp r1, 0                   ; direction: 0=ROR, nonzero=ROL
|       ,=< 0x080007c2      04d0           beq 0x80007ce               ; → ROR path
|       |   ; ROL8: (val << 1) | (val >> 7) - bits 0-6 shift up, bit 7 wraps to bit 0
|       |   0x080007c4      5006           lsls r0, r2, 0x19           ; val << 25 (isolate top bits)
|       |   0x080007c6      000e           lsrs r0, r0, 0x18           ; >> 24 = effectively (val << 1) & 0xFE
|       |   0x080007c8      d109           lsrs r1, r2, 7              ; val >> 7 (carry bit)
|       |   0x080007ca      0843           orrs r0, r1                 ; combine
|       |   0x080007cc      7047           bx lr
|       |   ; CODE XREF from fcn.080007be @ 0x80007c2(x)
|       |   ; ROR8: (val >> 1) | (val << 7) - bits 1-7 shift down, bit 0 wraps to bit 7
|       `-> 0x080007ce      5008           lsrs r0, r2, 1              ; val >> 1
|           0x080007d0      d107           lsls r1, r2, 0x1f           ; val << 31 (isolate bit 0)
|           0x080007d2      090e           lsrs r1, r1, 0x18           ; >> 24 = bit 0 moved to bit 7
|           0x080007d4      0843           orrs r0, r1                 ; combine
\           0x080007d6      7047           bx lr

; CALL XREF from model_xor_decode @ 0x8002388(x)
            ; CALL XREF from model_xor_encode @ 0x80023e8(x)
/ 6: fcn.080007da (int16_t arg1);
| `- args(r0)
|           0x080007da      c068           ldr r0, [r0, 0xc]           ; arg1
|           0x080007dc      80b2           uxth r0, r0                 ; arg1
\           0x080007de      7047           bx lr

; XREFS: CALL 0x080006da  CODE 0x080008b6  CODE 0x080008e8  CALL 0x08001596  CALL 0x080015a4  
            ; XREFS: CALL 0x080015b2  CALL 0x080015be  
/ 28: gpio_read_pin (int16_t arg1);
| `- args(r0)
|           0x080007ee      10b5           push {r4, lr}
|           0x080007f0      0346           mov r3, r0                  ; arg1
|           0x080007f2      0022           movs r2, 0
|           0x080007f4      4ff47a74       mov.w r4, 0x3e8
|       ,=< 0x080007f8      04e0           b 0x8000804
|       |   ; CODE XREF from gpio_read_pin @ 0x8000806(x)
|      .--> 0x080007fa      2046           mov r0, r4                  ; int16_t arg1
|      :|   0x080007fc      00f005f8       bl fcn.0800080a
|      :|   0x08000800      521c           adds r2, r2, 1
|      :|   0x08000802      92b2           uxth r2, r2
|      :|   ; CODE XREF from gpio_read_pin @ 0x80007f8(x)
|      :`-> 0x08000804      9a42           cmp r2, r3
|      `==< 0x08000806      f8d3           blo 0x80007fa
|           ;-- aav.0x08000808:
|           ; NULL XREF from set_decryption_key @ +0x1ba(r)
\           0x08000808      10bd           pop {r4, pc}

; XREFS: CALL 0x08000424  CALL 0x08000488  CALL 0x080007fc  CALL 0x08001c38  CODE 0x08001c7a  
            ; XREFS: CALL 0x08001ca0  CALL 0x08001cba  CALL 0x08001d50  CALL 0x08001d8e  CALL 0x08001d96  
            ; XREFS: CALL 0x08001e2a  CODE 0x08001e42  
/ 14: fcn.0800080a (int16_t arg1);
| `- args(r0)
|           0x0800080a      c0eb4010       rsb r0, r0, r0, lsl 5
|           0x0800080e      80b2           uxth r0, r0                 ; arg1
|           ; CODE XREF from fcn.0800080a @ 0x8000814(x)
|       .-> 0x08000810      401e           subs r0, r0, 1              ; arg1
|       :   0x08000812      80b2           uxth r0, r0                 ; arg1
|       `=< 0x08000814      fcd2           bhs 0x8000810
\           0x08000816      7047           bx lr

::   ; XREFS: CALL 0x08000518  CALL 0x08000540  CALL 0x0800055a  CALL 0x08000564  CALL 0x080005cc  
       ::   ; XREFS: CALL 0x0800068a  
/ 272: lcd_show_status (int16_t arg1);
| `- args(r0) vars(3:sp[0x28..0x30])
|      ::   0x08000818      2de9fe4f       push.w {r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, lr}
|      ::   0x0800081c      0446           mov r4, r0                  ; arg1
|      ::   0x0800081e      4ff6ff75       movw r5, 0xffff
|      ::   0x08000822      0027           movs r7, 0
|      ::   0x08000824      cde90175       strd r7, r5, [var_4h]
|      ::   0x08000828      3f48           ldr r0, aav.0x080024a2      ; [0x80024a2:4]=0
|      ::   0x0800082a      8023           movs r3, 0x80               ; int16_t arg4
|      ::   0x0800082c      0090           str r0, [sp]
|      ::   0x0800082e      1a46           mov r2, r3                  ; int16_t arg3
|      ::   0x08000830      3821           movs r1, 0x38               ; '8' ; int16_t arg2
|      ::   0x08000832      5420           movs r0, 0x54               ; 'T' ; int16_t arg1
|      ::   0x08000834      00f090ff       bl lcd_draw_text
|      ::   0x08000838      0126           movs r6, 1
|      ::   0x0800083a      dff8f0a0       ldr.w sl, [0x0800092c]      ; [0x800092c:4]=0x40011000
|      ::   0x0800083e      4ff48478       mov.w r8, 0x108
|      ::   0x08000842      40f6ac59       movw sb, 0xdac
|      ::   0x08000846      022c           cmp r4, 2                   ; 2
|     ,===< 0x08000848      1cd0           beq 0x8000884
|     |::   0x0800084a      032c           cmp r4, 3                   ; 3
|    ,====< 0x0800084c      35d0           beq 0x80008ba
|    ||::   0x0800084e      012c           cmp r4, 1                   ; 1
|   ,=====< 0x08000850      4cd0           beq 0x80008ec
|   |||::   0x08000852      042c           cmp r4, 4                   ; 4
|  ,======< 0x08000854      59d0           beq 0x800090a
|  ||||::   0x08000856      cde90057       strd r5, r7, [sp]
|  ||||::   0x0800085a      0023           movs r3, 0                  ; int16_t arg4
|  ||||::   0x0800085c      34a2           adr r2, 0xd0                ; 0x8000930 ; int16_t arg3
|  ||||::   0x0800085e      2721           movs r1, 0x27               ; '\'' ; int16_t arg2
|  ||||::   0x08000860      e820           movs r0, 0xe8               ; int16_t arg1
|  ||||::   0x08000862      00f043fe       bl flash_program
|  ||||::   0x08000866      cde90056       strd r5, r6, [sp]
|  ||||::   0x0800086a      0023           movs r3, 0                  ; int16_t arg4
|  ||||::   0x0800086c      34a2           adr r2, 0xd0                ; " Upgrading... "
|  ||||::                                                              ; 0x8000940 ; int16_t arg3
|  ||||::   0x0800086e      1721           movs r1, 0x17               ; int16_t arg2
|  ||||::   0x08000870      4046           mov r0, r8                  ; int16_t arg1
|  ||||::   0x08000872      00f03bfe       bl flash_program
|  ||||::   0x08000876      03b0           add sp, 0xc
|  ||||::   0x08000878      5046           mov r0, sl
|  ||||::   0x0800087a      bde8f04f       pop.w {r4, r5, r6, r7, r8, sb, sl, fp, lr}
|  ||||::   0x0800087e      4021           movs r1, 0x40               ; '@'
| ,=======< 0x08000880      00f091bc       b.w fcn.080011a6
| |||||::   ; CODE XREF from lcd_show_status @ 0x8000848(x)
| ||||`---> 0x08000884      4ff47844       mov.w r4, 0xf800
| |||| ::   0x08000888      cde90047       strd r4, r7, [sp]
| |||| ::   0x0800088c      0023           movs r3, 0                  ; int16_t arg4
| |||| ::   0x0800088e      30a2           adr r2, 0xc0                ; 0x8000950 ; int16_t arg3
| |||| ::   0x08000890      2721           movs r1, 0x27               ; '\'' ; int16_t arg2
| |||| ::   0x08000892      e820           movs r0, 0xe8               ; int16_t arg1
| |||| ::   0x08000894      00f02afe       bl flash_program
| |||| ::   0x08000898      cde90046       strd r4, r6, [sp]
| |||| ::   0x0800089c      0023           movs r3, 0                  ; int16_t arg4
| |||| ::   0x0800089e      30a2           adr r2, 0xc0                ; " Update Error! "
| |||| ::                                                              ; 0x8000960 ; int16_t arg3
| |||| ::   0x080008a0      1721           movs r1, 0x17               ; int16_t arg2
| |||| ::   0x080008a2      4046           mov r0, r8                  ; int16_t arg1
| |||| ::   0x080008a4      00f022fe       bl flash_program
| |||| ::   0x080008a8      4021           movs r1, 0x40               ; '@'
| |||| ::   0x080008aa      5046           mov r0, sl
| |||| ::   0x080008ac      00f07bfc       bl fcn.080011a6
| |||| ::   0x080008b0      4846           mov r0, sb
| |||| ::   0x080008b2      bde8fe4f       pop.w {r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, lr}
| |||| `==< 0x080008b6      fff79abf       b.w gpio_read_pin
| ||||  :   ; CODE XREF from lcd_show_status @ 0x800084c(x)
| |||`----> 0x080008ba      cde90057       strd r5, r7, [sp]
| |||   :   0x080008be      0023           movs r3, 0                  ; int16_t arg4
| |||   :   0x080008c0      2ba2           adr r2, 0xac                ; 0x8000970 ; int16_t arg3
| |||   :   0x080008c2      2721           movs r1, 0x27               ; '\'' ; int16_t arg2
| |||   :   0x080008c4      e820           movs r0, 0xe8               ; int16_t arg1
| |||   :   0x080008c6      00f011fe       bl flash_program
| |||   :   0x080008ca      cde90056       strd r5, r6, [sp]
| |||   :   0x080008ce      0023           movs r3, 0                  ; int16_t arg4
| |||   :   0x080008d0      2ba2           adr r2, 0xac                ; "Update Success!"
| |||   :                                                              ; 0x8000980 ; int16_t arg3
| |||   :   0x080008d2      1721           movs r1, 0x17               ; int16_t arg2
| |||   :   0x080008d4      4046           mov r0, r8                  ; int16_t arg1
| |||   :   0x080008d6      00f009fe       bl flash_program
| |||   :   0x080008da      4021           movs r1, 0x40               ; '@'
| |||   :   0x080008dc      5046           mov r0, sl
| |||   :   0x080008de      00f062fc       bl fcn.080011a6
| |||   :   0x080008e2      4846           mov r0, sb
| |||   :   0x080008e4      bde8fe4f       pop.w {r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, lr}
| |||   |   ; CODE XREF from lcd_show_status @ +0x120(x)
| |||   `=< 0x080008e8      fff781bf       b.w gpio_read_pin
| |||       ; CODE XREF from lcd_show_status @ 0x8000850(x)
| ||`-----> 0x080008ec      cde90056       strd r5, r6, [sp]
| ||        0x080008f0      0023           movs r3, 0                  ; int16_t arg4
| ||        0x080008f2      27a2           adr r2, 0x9c                ; "UPDATE"
| ||                                                                   ; 0x8000990 ; int16_t arg3
| ||        0x080008f4      4d21           movs r1, 0x4d               ; 'M' ; int16_t arg2
| ||        0x080008f6      e820           movs r0, 0xe8               ; int16_t arg1
| ||        0x080008f8      00f0f8fd       bl flash_program
| ||        0x080008fc      03b0           add sp, 0xc
| ||        0x080008fe      5046           mov r0, sl
| ||        0x08000900      bde8f04f       pop.w {r4, r5, r6, r7, r8, sb, sl, fp, lr}
| ||        0x08000904      4021           movs r1, 0x40               ; '@'
| ||    ,=< 0x08000906      00f04ebc       b.w fcn.080011a6
| ||    |   ; CODE XREF from lcd_show_status @ 0x8000854(x)
| |`------> 0x0800090a      cde90056       strd r5, r6, [sp]
| |     |   0x0800090e      0023           movs r3, 0                  ; int16_t arg4
| |     |   0x08000910      21a2           adr r2, 0x84                ; "Firmware Error!"
| |     |                                                              ; 0x8000998 ; int16_t arg3
| |     |   0x08000912      1721           movs r1, 0x17               ; int16_t arg2
| |     |   0x08000914      e820           movs r0, 0xe8               ; int16_t arg1
| |     |   0x08000916      00f0e9fd       bl flash_program
| |     |   0x0800091a      03b0           add sp, 0xc
| |     |   0x0800091c      5046           mov r0, sl
| |     |   0x0800091e      bde8f04f       pop.w {r4, r5, r6, r7, r8, sb, sl, fp, lr}
| |     |   0x08000922      4021           movs r1, 0x40               ; '@'
\ |    ,==< 0x08000924      00f03fbc       b.w fcn.080011a6

; CALL XREF from check_update_button @ 0x8000544(x)
/ 104: spi_cmd_read_status ();
|           0x080009a8      70b5           push {r4, r5, r6, lr}
|           0x080009aa      0024           movs r4, 0
|           0x080009ac      1849           ldr r1, [0x08000a10]        ; [0x8000a10:4]=0x2000000c
|           0x080009ae      0020           movs r0, 0
|           0x080009b0      8860           str r0, [r1, 8]
|           0x080009b2      184a           ldr r2, [0x08000a14]        ; [0x8000a14:4]=0x20000468
|           0x080009b4      d2f80200       ldr.w r0, [r2, 2]
|           0x080009b8      800a           lsrs r0, r0, 0xa
|           0x080009ba      c860           str r0, [r1, 0xc]
|           0x080009bc      0028           cmp r0, 0
|       ,=< 0x080009be      21d9           bls 0x8000a04
|       |   0x080009c0      1449           ldr r1, [0x08000a14]        ; [0x8000a14:4]=0x20000468
|       |   0x080009c2      4ff48062       mov.w r2, 0x400
|       |   0x080009c6      1031           adds r1, 0x10
|       |   0x080009c8      1348           ldr r0, [0x08000a18]        ; [0x8000a18:4]=0x300100
|       |   0x080009ca      01f027f9       bl lcd_spi_write_data
|       |   0x080009ce      0021           movs r1, 0
|       |   0x080009d0      104d           ldr r5, [0x08000a14]        ; [0x8000a14:4]=0x20000468
|       |   0x080009d2      41f22102       movw r2, 0x1021             ; '!\x10'
|       |   0x080009d6      1035           adds r5, 0x10
|       |   0x080009d8      4ff6ff73       movw r3, 0xffff
|       |   ; CODE XREF from spi_cmd_read_status @ 0x8000a02(x)
|      .--> 0x080009dc      685c           ldrb r0, [r5, r1]
|      :|   0x080009de      84ea0020       eor.w r0, r4, r0, lsl 8
|      :|   0x080009e2      84b2           uxth r4, r0
|      :|   0x080009e4      0020           movs r0, 0
|      :|   ; CODE XREF from spi_cmd_read_status @ 0x80009fc(x)
|     .---> 0x080009e6      2604           lsls r6, r4, 0x10
|    ,====< 0x080009e8      03d5           bpl 0x80009f2
|    |::|   0x080009ea      82ea4404       eor.w r4, r2, r4, lsl 1
|    |::|   0x080009ee      a4b2           uxth r4, r4
|   ,=====< 0x080009f0      01e0           b 0x80009f6
|   ||::|   ; CODE XREF from spi_cmd_read_status @ 0x80009e8(x)
|   |`----> 0x080009f2      03ea4404       and.w r4, r3, r4, lsl 1
|   | ::|   ; CODE XREF from spi_cmd_read_status @ 0x80009f0(x)
|   `-----> 0x080009f6      401c           adds r0, r0, 1
|     ::|   0x080009f8      c0b2           uxtb r0, r0
|     ::|   0x080009fa      0828           cmp r0, 8                   ; 8
|     `===< 0x080009fc      f3d3           blo 0x80009e6
|      :|   0x080009fe      491c           adds r1, r1, 1
|      :|   0x08000a00      c9b2           uxtb r1, r1
|      `==< 0x08000a02      ebe7           b 0x80009dc
|       |   ; CODE XREF from spi_cmd_read_status @ 0x80009be(x)
|       `-> 0x08000a04      d088           ldrh r0, [r2, 6]
|       ,=< 0x08000a06      08b1           cbz r0, 0x8000a0c
|       |   0x08000a08      0020           movs r0, 0
|       |   0x08000a0a      70bd           pop {r4, r5, r6, pc}
|       |   ; CODE XREF from spi_cmd_read_status @ 0x8000a06(x)
|       `-> 0x08000a0c      0120           movs r0, 1
\           0x08000a0e      70bd           pop {r4, r5, r6, pc}

; CALL XREF from flash_from_spi @ 0x80013d0(x)
/ 172: spi_flash_read (int16_t arg1);
| `- args(r0)
|           0x08000a1c      70b5           push {r4, r5, r6, lr}
|           0x08000a1e      0446           mov r4, r0                  ; arg1
|           0x08000a20      0420           movs r0, 4
|           0x08000a22      294d           ldr r5, [0x08000ac8]        ; [0x8000ac8:4]=0x40022000
|           0x08000a24      b4f1046f       cmp.w r4, 0x8400000
|       ,=< 0x08000a28      17d3           blo 0x8000a5a
|       |   0x08000a2a      461f           subs r6, r0, 5
|       |   0x08000a2c      3046           mov r0, r6                  ; int16_t arg1
|       |   0x08000a2e      00f017f9       bl spi_xfer_byte_0b
|       |   0x08000a32      0428           cmp r0, 4                   ; 4
|      ,==< 0x08000a34      10d1           bne 0x8000a58
|      ||   0x08000a36      55f8900f       ldr r0, [r5, 0x90]!
|      ||   0x08000a3a      40f00200       orr r0, r0, 2
|      ||   0x08000a3e      2860           str r0, [r5]
|      ||   0x08000a40      6c60           str r4, [r5, 4]
|      ||   0x08000a42      2868           ldr r0, [r5]
|      ||   0x08000a44      40f04000       orr r0, r0, 0x40
|      ||   0x08000a48      2860           str r0, [r5]
|      ||   0x08000a4a      3046           mov r0, r6                  ; int16_t arg1
|      ||   0x08000a4c      00f008f9       bl spi_xfer_byte_0b
|      ||   0x08000a50      2968           ldr r1, [r5]
|      ||   0x08000a52      21f00201       bic r1, r1, 2
|      ||   0x08000a56      2960           str r1, [r5]
|      ||   ; CODE XREFS from spi_flash_read @ 0x8000a34(x), 0x8000a6c(x)
|     .`--> 0x08000a58      70bd           pop {r4, r5, r6, pc}
|     : |   ; CODE XREF from spi_flash_read @ 0x8000a28(x)
|     : `-> 0x08000a5a      1c49           ldr r1, [0x08000acc]        ; [0x8000acc:4]=0x807ffff
|     :     0x08000a5c      4ff08056       mov.w r6, 0x10000000
|     :     0x08000a60      8c42           cmp r4, r1
|     : ,=< 0x08000a62      15d8           bhi 0x8000a90
|     : |   0x08000a64      3046           mov r0, r6                  ; int16_t arg1
|     : |   0x08000a66      00f0dbf8       bl spi_xfer_byte_00
|     : |   0x08000a6a      0428           cmp r0, 4                   ; 4
|     `===< 0x08000a6c      f4d1           bne 0x8000a58
|       |   0x08000a6e      2869           ldr r0, [r5, 0x10]
|       |   0x08000a70      40f00200       orr r0, r0, 2
|       |   0x08000a74      2861           str r0, [r5, 0x10]
|       |   0x08000a76      6c61           str r4, [r5, 0x14]
|       |   0x08000a78      2869           ldr r0, [r5, 0x10]
|       |   0x08000a7a      40f04000       orr r0, r0, 0x40
|       |   0x08000a7e      2861           str r0, [r5, 0x10]
|       |   0x08000a80      3046           mov r0, r6                  ; int16_t arg1
|       |   0x08000a82      00f0cdf8       bl spi_xfer_byte_00
|       |   0x08000a86      2969           ldr r1, [r5, 0x10]
|       |   0x08000a88      21f00201       bic r1, r1, 2
|       |   0x08000a8c      2961           str r1, [r5, 0x10]
|       |   ; CODE XREFS from spi_flash_read @ 0x8000a9a(x), 0x8000aa4(x)
|     ..--> 0x08000a8e      70bd           pop {r4, r5, r6, pc}
|     ::|   ; CODE XREF from spi_flash_read @ 0x8000a62(x)
|     ::`-> 0x08000a90      0e49           ldr r1, [0x08000acc]        ; [0x8000acc:4]=0x807ffff
|     ::    0x08000a92      c943           mvns r1, r1
|     ::    0x08000a94      2144           add r1, r4
|     ::    0x08000a96      b1f5002f       cmp.w r1, 0x80000
|     `===< 0x08000a9a      f8d2           bhs 0x8000a8e
|      :    0x08000a9c      3046           mov r0, r6                  ; int16_t arg1
|      :    0x08000a9e      00f0cff8       bl spi_xfer_byte_03
|      :    0x08000aa2      0428           cmp r0, 4                   ; 4
|      `==< 0x08000aa4      f3d1           bne 0x8000a8e
|           0x08000aa6      286d           ldr r0, [r5, 0x50]
|           0x08000aa8      40f00200       orr r0, r0, 2
|           0x08000aac      2865           str r0, [r5, 0x50]
|           0x08000aae      6c65           str r4, [r5, 0x54]
|           0x08000ab0      286d           ldr r0, [r5, 0x50]
|           0x08000ab2      40f04000       orr r0, r0, 0x40
|           0x08000ab6      2865           str r0, [r5, 0x50]
|           0x08000ab8      3046           mov r0, r6                  ; int16_t arg1
|           0x08000aba      00f0c1f8       bl spi_xfer_byte_03
|           0x08000abe      296d           ldr r1, [r5, 0x50]
|           0x08000ac0      21f00201       bic r1, r1, 2
|           0x08000ac4      2965           str r1, [r5, 0x50]
\           0x08000ac6      70bd           pop {r4, r5, r6, pc}

; CALL XREFS from spi_xfer_byte_00 @ 0x8000c24(x), 0x8000c2a(x)
            ; CALL XREFS from spi_xfer_byte_02 @ 0x8000c84(x), 0x8000c8a(x)
/ 34: spi_flash_erase ();
|           0x08000ad0      0420           movs r0, 4
|           0x08000ad2      0849           ldr r1, [0x08000af4]        ; [0x8000af4:4]=0x40022000
|           0x08000ad4      ca68           ldr r2, [r1, 0xc]
|           0x08000ad6      d207           lsls r2, r2, 0x1f
|       ,=< 0x08000ad8      01d0           beq 0x8000ade
|       |   0x08000ada      0120           movs r0, 1
|       |   0x08000adc      7047           bx lr
|       |   ; CODE XREF from spi_flash_erase @ 0x8000ad8(x)
|       `-> 0x08000ade      ca68           ldr r2, [r1, 0xc]
|           0x08000ae0      5207           lsls r2, r2, 0x1d
|       ,=< 0x08000ae2      01d5           bpl 0x8000ae8
|       |   0x08000ae4      0220           movs r0, 2
|       |   ; CODE XREF from spi_flash_erase @ 0x8000aec(x)
|      .--> 0x08000ae6      7047           bx lr
|      :|   ; CODE XREF from spi_flash_erase @ 0x8000ae2(x)
|      :`-> 0x08000ae8      c968           ldr r1, [r1, 0xc]
|      :    0x08000aea      c906           lsls r1, r1, 0x1b
|      `==< 0x08000aec      fbd5           bpl 0x8000ae6
|           0x08000aee      0320           movs r0, 3
\           0x08000af0      7047           bx lr

; CALL XREFS from spi_xfer_byte_03 @ 0x8000c44(x), 0x8000c4a(x)
/ 34: spi_flash_write_enable ();
|           0x08000af8      0420           movs r0, 4
|           0x08000afa      0849           ldr r1, [0x08000b1c]        ; [0x8000b1c:4]=0x40022000
|           0x08000afc      ca6c           ldr r2, [r1, 0x4c]
|           0x08000afe      d207           lsls r2, r2, 0x1f
|       ,=< 0x08000b00      01d0           beq 0x8000b06
|       |   0x08000b02      0120           movs r0, 1
|       |   0x08000b04      7047           bx lr
|       |   ; CODE XREF from spi_flash_write_enable @ 0x8000b00(x)
|       `-> 0x08000b06      ca6c           ldr r2, [r1, 0x4c]
|           0x08000b08      5207           lsls r2, r2, 0x1d
|       ,=< 0x08000b0a      01d5           bpl 0x8000b10
|       |   0x08000b0c      0220           movs r0, 2
|       |   ; CODE XREF from spi_flash_write_enable @ 0x8000b14(x)
|      .--> 0x08000b0e      7047           bx lr
|      :|   ; CODE XREF from spi_flash_write_enable @ 0x8000b0a(x)
|      :`-> 0x08000b10      c96c           ldr r1, [r1, 0x4c]
|      :    0x08000b12      c906           lsls r1, r1, 0x1b
|      `==< 0x08000b14      fbd5           bpl 0x8000b0e
|           0x08000b16      0320           movs r0, 3
\           0x08000b18      7047           bx lr

; CALL XREFS from spi_xfer_byte_0b @ 0x8000c64(x), 0x8000c6a(x)
/ 40: spi_flash_write ();
|           0x08000b20      0420           movs r0, 4
|           0x08000b22      0949           ldr r1, [0x08000b48]        ; [0x8000b48:4]=0x40022000
|           0x08000b24      d1f88c20       ldr.w r2, [r1, 0x8c]
|           0x08000b28      d207           lsls r2, r2, 0x1f
|       ,=< 0x08000b2a      01d0           beq 0x8000b30
|       |   0x08000b2c      0120           movs r0, 1
|       |   0x08000b2e      7047           bx lr
|       |   ; CODE XREF from spi_flash_write @ 0x8000b2a(x)
|       `-> 0x08000b30      d1f88c20       ldr.w r2, [r1, 0x8c]
|           0x08000b34      5207           lsls r2, r2, 0x1d
|       ,=< 0x08000b36      01d5           bpl 0x8000b3c
|       |   0x08000b38      0220           movs r0, 2
|       |   ; CODE XREF from spi_flash_write @ 0x8000b42(x)
|      .--> 0x08000b3a      7047           bx lr
|      :|   ; CODE XREF from spi_flash_write @ 0x8000b36(x)
|      :`-> 0x08000b3c      d1f88c10       ldr.w r1, [r1, 0x8c]
|      :    0x08000b40      c906           lsls r1, r1, 0x1b
|      `==< 0x08000b42      fad5           bpl 0x8000b3a
|           0x08000b44      0320           movs r0, 3
\           0x08000b46      7047           bx lr

; CALL XREF from clear_spi_update @ 0x8000774(x)
            ; CALL XREF from flash_from_spi @ 0x800147c(x)
            ; CALL XREF from aav.0x08002d00 @ +0x54(x)
/ 20: spi_wait_busy ();
|           0x08000b4c      0448           ldr r0, [0x08000b60]        ; [0x8000b60:4]=0x40022000
|           0x08000b4e      0169           ldr r1, [r0, 0x10]
|           0x08000b50      41f08001       orr r1, r1, 0x80
|           0x08000b54      0161           str r1, [r0, 0x10]
|           0x08000b56      016d           ldr r1, [r0, 0x50]
|           0x08000b58      41f08001       orr r1, r1, 0x80
|           0x08000b5c      0165           str r1, [r0, 0x50]
\           0x08000b5e      7047           bx lr

; XREFS: CALL 0x08000702  CALL 0x0800070c  CALL 0x08000716  CALL 0x08000720  CALL 0x0800072a  
            ; XREFS: CALL 0x08000734  CALL 0x0800073e  CALL 0x08000748  CALL 0x08000752  CALL 0x0800075c  
            ; XREFS: CALL 0x08000766  CALL 0x08000770  CALL 0x0800145e  CALL 0x08002d3c  CALL 0x08002d46  
            ; XREFS: CALL 0x08002d50  
/ 152: spi_transaction (int16_t arg1, int16_t arg2);
| `- args(r0, r1) vars(1:sp[0x18..0x18])
|           0x08000b64      f8b5           push {r3, r4, r5, r6, r7, lr}
|           0x08000b66      0446           mov r4, r0                  ; arg1
|           0x08000b68      0d46           mov r5, r1                  ; arg2
|           0x08000b6a      0420           movs r0, 4
|           0x08000b6c      0021           movs r1, 0
|           0x08000b6e      0091           str r1, [sp]
|           0x08000b70      224e           ldr r6, [0x08000bfc]        ; [0x8000bfc:4]=0x40022000
|           0x08000b72      4ff47047       mov.w r7, 0xf000
|           0x08000b76      b4f1046f       cmp.w r4, 0x8400000
|       ,=< 0x08000b7a      12d3           blo 0x8000ba2
|       |   0x08000b7c      3846           mov r0, r7
|       |   0x08000b7e      00f06ff8       bl spi_xfer_byte_0b
|       |   0x08000b82      0428           cmp r0, 4                   ; 4
|      ,==< 0x08000b84      0cd1           bne 0x8000ba0
|      ||   0x08000b86      56f8900f       ldr r0, [r6, 0x90]!
|      ||   0x08000b8a      40f00100       orr r0, r0, 1
|      ||   0x08000b8e      3060           str r0, [r6]
|      ||   0x08000b90      2560           str r5, [r4]
|      ||   0x08000b92      3846           mov r0, r7
|      ||   0x08000b94      00f064f8       bl spi_xfer_byte_0b
|      ||   0x08000b98      3168           ldr r1, [r6]
|      ||   0x08000b9a      21f00101       bic r1, r1, 1
|      ||   0x08000b9e      3160           str r1, [r6]
|      ||   ; CODE XREFS from spi_transaction @ 0x8000b84(x), 0x8000bb0(x)
|     .`--> 0x08000ba0      f8bd           pop {r3, r4, r5, r6, r7, pc}
|     : |   ; CODE XREF from spi_transaction @ 0x8000b7a(x)
|     : `-> 0x08000ba2      1749           ldr r1, [0x08000c00]        ; [0x8000c00:4]=0x807ffff
|     :     0x08000ba4      8c42           cmp r4, r1
|     : ,=< 0x08000ba6      11d8           bhi 0x8000bcc
|     : |   0x08000ba8      3846           mov r0, r7
|     : |   0x08000baa      00f039f8       bl spi_xfer_byte_00
|     : |   0x08000bae      0428           cmp r0, 4                   ; 4
|     `===< 0x08000bb0      f6d1           bne 0x8000ba0
|       |   0x08000bb2      3069           ldr r0, [r6, 0x10]
|       |   0x08000bb4      40f00100       orr r0, r0, 1
|       |   0x08000bb8      3061           str r0, [r6, 0x10]
|       |   0x08000bba      2560           str r5, [r4]
|       |   0x08000bbc      3846           mov r0, r7                  ; int16_t arg1
|       |   0x08000bbe      00f05ff8       bl spi_xfer_byte_02
|       |   0x08000bc2      3169           ldr r1, [r6, 0x10]
|       |   0x08000bc4      21f00101       bic r1, r1, 1
|       |   0x08000bc8      3161           str r1, [r6, 0x10]
|       |   ; CODE XREFS from spi_transaction @ 0x8000bd6(x), 0x8000be0(x)
|     ..--> 0x08000bca      f8bd           pop {r3, r4, r5, r6, r7, pc}
|     ::|   ; CODE XREF from spi_transaction @ 0x8000ba6(x)
|     ::`-> 0x08000bcc      0c49           ldr r1, [0x08000c00]        ; [0x8000c00:4]=0x807ffff
|     ::    0x08000bce      c943           mvns r1, r1
|     ::    0x08000bd0      2144           add r1, r4
|     ::    0x08000bd2      b1f5002f       cmp.w r1, 0x80000
|     `===< 0x08000bd6      f8d2           bhs 0x8000bca
|      :    0x08000bd8      3846           mov r0, r7
|      :    0x08000bda      00f031f8       bl spi_xfer_byte_03
|      :    0x08000bde      0428           cmp r0, 4                   ; 4
|      `==< 0x08000be0      f3d1           bne 0x8000bca
|           0x08000be2      306d           ldr r0, [r6, 0x50]
|           0x08000be4      40f00100       orr r0, r0, 1
|           0x08000be8      3065           str r0, [r6, 0x50]
|           0x08000bea      2560           str r5, [r4]
|           0x08000bec      3846           mov r0, r7
|           0x08000bee      00f027f8       bl spi_xfer_byte_03
|           0x08000bf2      316d           ldr r1, [r6, 0x50]
|           0x08000bf4      21f00101       bic r1, r1, 1
|           0x08000bf8      3165           str r1, [r6, 0x50]
\           0x08000bfa      f8bd           pop {r3, r4, r5, r6, r7, pc}

; CALL XREF from clear_spi_update @ 0x80006fa(x)
            ; CALL XREF from flash_from_spi @ 0x80013c6(x)
            ; CALL XREF from aav.0x08002d00 @ +0x34(x)
/ 16: spi_cs_low ();
|           0x08000c04      0448           ldr r0, [0x08000c18]        ; [0x8000c18:4]=0x40022000
|           0x08000c06      0349           ldr r1, [0x08000c14]        ; [0x8000c14:4]=0x45670123
|           0x08000c08      4160           str r1, [r0, 4]
|           0x08000c0a      044a           ldr r2, [0x08000c1c]        ; [0x8000c1c:4]=0xcdef89ab
|           0x08000c0c      4260           str r2, [r0, 4]
|           0x08000c0e      4164           str r1, [r0, 0x44]
|           0x08000c10      4264           str r2, [r0, 0x44]
\           0x08000c12      7047           bx lr

; CALL XREFS from spi_flash_read @ 0x8000a66(x), 0x8000a82(x)
            ; CALL XREF from spi_transaction @ 0x8000baa(x)
/ 32: spi_xfer_byte_00 (int16_t arg1);
| `- args(r0)
|           0x08000c20      00b5           push {lr}
|           0x08000c22      0346           mov r3, r0                  ; arg1
|           0x08000c24      fff754ff       bl spi_flash_erase
|       ,=< 0x08000c28      02e0           b 0x8000c30
|       |   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c36(x)
|      .--> 0x08000c2a      fff751ff       bl spi_flash_erase
|      :|   0x08000c2e      5b1e           subs r3, r3, 1
|      :|   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c28(x)
|      :`-> 0x08000c30      0128           cmp r0, 1                   ; 1
|      :,=< 0x08000c32      01d1           bne 0x8000c38
|      :|   0x08000c34      002b           cmp r3, 0
|      `==< 0x08000c36      f8d1           bne 0x8000c2a
|       |   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c32(x)
|       `-> 0x08000c38      002b           cmp r3, 0
|       ,=< 0x08000c3a      00d1           bne 0x8000c3e
|       |   0x08000c3c      0520           movs r0, 5
|       |   ; CODE XREF from spi_xfer_byte_00 @ 0x8000c3a(x)
\       `-> 0x08000c3e      00bd           pop {pc}

; CALL XREFS from spi_flash_read @ 0x8000a9e(x), 0x8000aba(x)
            ; CALL XREFS from spi_transaction @ 0x8000bda(x), 0x8000bee(x)
/ 32: spi_xfer_byte_03 (int16_t arg1);
| `- args(r0)
|           0x08000c40      00b5           push {lr}
|           0x08000c42      0346           mov r3, r0                  ; arg1
|           0x08000c44      fff758ff       bl spi_flash_write_enable
|       ,=< 0x08000c48      02e0           b 0x8000c50
|       |   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c56(x)
|      .--> 0x08000c4a      fff755ff       bl spi_flash_write_enable
|      :|   0x08000c4e      5b1e           subs r3, r3, 1
|      :|   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c48(x)
|      :`-> 0x08000c50      0128           cmp r0, 1                   ; 1
|      :,=< 0x08000c52      01d1           bne 0x8000c58
|      :|   0x08000c54      002b           cmp r3, 0
|      `==< 0x08000c56      f8d1           bne 0x8000c4a
|       |   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c52(x)
|       `-> 0x08000c58      002b           cmp r3, 0
|       ,=< 0x08000c5a      00d1           bne 0x8000c5e
|       |   0x08000c5c      0520           movs r0, 5
|       |   ; CODE XREF from spi_xfer_byte_03 @ 0x8000c5a(x)
\       `-> 0x08000c5e      00bd           pop {pc}

; DATA XREF from lcd_show_status @ +0x160(r)
            ; CALL XREFS from spi_flash_read @ 0x8000a2e(r), 0x8000a4c(x)
            ; CALL XREFS from spi_transaction @ 0x8000b7e(r), 0x8000b94(x)
/ 32: spi_xfer_byte_0b (int16_t arg1);
| `- args(r0)
|           0x08000c60      00b5           push {lr}
|           0x08000c62      0346           mov r3, r0                  ; arg1
|           0x08000c64      fff75cff       bl spi_flash_write
|       ,=< 0x08000c68      02e0           b 0x8000c70
|       |   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c76(x)
|      .--> 0x08000c6a      fff759ff       bl spi_flash_write
|      :|   0x08000c6e      5b1e           subs r3, r3, 1
|      :|   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c68(x)
|      :`-> 0x08000c70      0128           cmp r0, 1                   ; 1
|      :,=< 0x08000c72      01d1           bne 0x8000c78
|      :|   0x08000c74      002b           cmp r3, 0
|      `==< 0x08000c76      f8d1           bne 0x8000c6a
|       |   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c72(x)
|       `-> 0x08000c78      002b           cmp r3, 0
|       ,=< 0x08000c7a      00d1           bne 0x8000c7e
|       |   0x08000c7c      0520           movs r0, 5
|       |   ; CODE XREF from spi_xfer_byte_0b @ 0x8000c7a(x)
\       `-> 0x08000c7e      00bd           pop {pc}

; DATA XREF from lcd_show_status @ +0x13e(r)
            ; CALL XREF from spi_transaction @ 0x8000bbe(r)
/ 32: spi_xfer_byte_02 (int16_t arg1);
| `- args(r0)
|           0x08000c80      00b5           push {lr}
|           0x08000c82      0346           mov r3, r0                  ; arg1
|           0x08000c84      fff724ff       bl spi_flash_erase
|       ,=< 0x08000c88      02e0           b 0x8000c90
|       |   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c96(x)
|      .--> 0x08000c8a      fff721ff       bl spi_flash_erase
|      :|   0x08000c8e      5b1e           subs r3, r3, 1
|      :|   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c88(x)
|      :`-> 0x08000c90      0128           cmp r0, 1                   ; 1
|      :,=< 0x08000c92      01d1           bne 0x8000c98
|      :|   0x08000c94      002b           cmp r3, 0
|      `==< 0x08000c96      f8d1           bne 0x8000c8a
|       |   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c92(x)
|       `-> 0x08000c98      002b           cmp r3, 0
|       ,=< 0x08000c9a      00d1           bne 0x8000c9e
|       |   0x08000c9c      0520           movs r0, 5
|       |   ; CODE XREF from spi_xfer_byte_02 @ 0x8000c9a(x)
\       `-> 0x08000c9e      00bd           pop {pc}

; XREFS: CALL 0x08001214  CALL 0x08001236  CALL 0x0800125a  CALL 0x0800127a  CALL 0x08001294  
            ; XREFS: CALL 0x080012a8  CALL 0x080012ca  CALL 0x080012f6  CALL 0x08001316  CALL 0x08001330  
            ; XREFS: CALL 0x08001350  CALL 0x0800136c  CALL 0x08002178  CALL 0x0800218c  
/ 166: spi_flash_read_id (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x08000ca0      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|           0x08000ca4      0022           movs r2, 0
|           0x08000ca6      cc78           ldrb r4, [r1, 3]            ; arg2
|           0x08000ca8      04f00f03       and r3, r4, 0xf
|           0x08000cac      e406           lsls r4, r4, 0x1b
|       ,=< 0x08000cae      01d5           bpl 0x8000cb4
|       |   0x08000cb0      8c78           ldrb r4, [r1, 2]            ; arg2
|       |   0x08000cb2      2343           orrs r3, r4
|       |   ; CODE XREF from spi_flash_read_id @ 0x8000cae(x)
|       `-> 0x08000cb4      0c78           ldrb r4, [r1]               ; arg2
|           0x08000cb6      4ff00f07       mov.w r7, 0xf
|           0x08000cba      14f0ff0f       tst.w r4, 0xff              ; 255
|           0x08000cbe      4ff0010c       mov.w ip, 1
|       ,=< 0x08000cc2      1cd0           beq 0x8000cfe
|       |   0x08000cc4      0568           ldr r5, [r0]                ; arg1
|       |   ; CODE XREF from spi_flash_read_id @ 0x8000cfa(x)
|      .--> 0x08000cc6      0cfa02f4       lsl.w r4, ip, r2
|      :|   0x08000cca      0e88           ldrh r6, [r1]               ; arg2
|      :|   0x08000ccc      2640           ands r6, r4
|      :|   0x08000cce      a642           cmp r6, r4
|     ,===< 0x08000cd0      11d1           bne 0x8000cf6
|     |:|   0x08000cd2      9600           lsls r6, r2, 2
|     |:|   0x08000cd4      07fa06f8       lsl.w r8, r7, r6
|     |:|   0x08000cd8      25ea0805       bic.w r5, r5, r8
|     |:|   0x08000cdc      03fa06f8       lsl.w r8, r3, r6
|     |:|   0x08000ce0      48ea0505       orr.w r5, r8, r5
|     |:|   0x08000ce4      ce78           ldrb r6, [r1, 3]            ; arg2
|     |:|   0x08000ce6      282e           cmp r6, 0x28                ; 40
|    ,====< 0x08000ce8      02d0           beq 0x8000cf0
|    ||:|   0x08000cea      482e           cmp r6, 0x48                ; 72
|   ,=====< 0x08000cec      02d0           beq 0x8000cf4
|  ,======< 0x08000cee      02e0           b 0x8000cf6
|  ||||:|   ; CODE XREF from spi_flash_read_id @ 0x8000ce8(x)
|  ||`----> 0x08000cf0      4461           str r4, [r0, 0x14]          ; arg1
|  ||,====< 0x08000cf2      00e0           b 0x8000cf6
|  ||||:|   ; CODE XREF from spi_flash_read_id @ 0x8000cec(x)
|  |`-----> 0x08000cf4      0461           str r4, [r0, 0x10]          ; arg1
|  | ||:|   ; CODE XREFS from spi_flash_read_id @ 0x8000cd0(x), 0x8000cee(x), 0x8000cf2(x)
|  `-``---> 0x08000cf6      521c           adds r2, r2, 1
|      :|   0x08000cf8      082a           cmp r2, 8                   ; 8
|      `==< 0x08000cfa      e4d3           blo 0x8000cc6
|       |   0x08000cfc      0560           str r5, [r0]                ; arg1
|       |   ; CODE XREF from spi_flash_read_id @ 0x8000cc2(x)
|       `-> 0x08000cfe      0a88           ldrh r2, [r1]               ; arg2
|           0x08000d00      ff2a           cmp r2, 0xff                ; 255
|       ,=< 0x08000d02      1ed9           bls 0x8000d42
|       |   0x08000d04      4568           ldr r5, [r0, 4]             ; arg1
|       |   0x08000d06      0022           movs r2, 0
|       |   ; CODE XREF from spi_flash_read_id @ 0x8000d3e(x)
|      .--> 0x08000d08      02f10806       add.w r6, r2, 8
|      :|   0x08000d0c      0cfa06f4       lsl.w r4, ip, r6
|      :|   0x08000d10      0e88           ldrh r6, [r1]               ; arg2
|      :|   0x08000d12      2640           ands r6, r4
|      :|   0x08000d14      a642           cmp r6, r4
|     ,===< 0x08000d16      10d1           bne 0x8000d3a
|     |:|   0x08000d18      9600           lsls r6, r2, 2
|     |:|   0x08000d1a      07fa06f8       lsl.w r8, r7, r6
|     |:|   0x08000d1e      25ea0805       bic.w r5, r5, r8
|     |:|   0x08000d22      03fa06f8       lsl.w r8, r3, r6
|     |:|   0x08000d26      48ea0505       orr.w r5, r8, r5
|     |:|   0x08000d2a      ce78           ldrb r6, [r1, 3]            ; arg2
|     |:|   0x08000d2c      282e           cmp r6, 0x28                ; 40
|    ,====< 0x08000d2e      00d1           bne 0x8000d32
|    ||:|   0x08000d30      4461           str r4, [r0, 0x14]          ; arg1
|    ||:|   ; CODE XREF from spi_flash_read_id @ 0x8000d2e(x)
|    `----> 0x08000d32      ce78           ldrb r6, [r1, 3]            ; arg2
|     |:|   0x08000d34      482e           cmp r6, 0x48                ; 72
|    ,====< 0x08000d36      00d1           bne 0x8000d3a
|    ||:|   0x08000d38      0461           str r4, [r0, 0x10]          ; arg1
|    ||:|   ; CODE XREFS from spi_flash_read_id @ 0x8000d16(x), 0x8000d36(x)
|    ``---> 0x08000d3a      521c           adds r2, r2, 1
|      :|   0x08000d3c      082a           cmp r2, 8                   ; 8
|      `==< 0x08000d3e      e3d3           blo 0x8000d08
|       |   0x08000d40      4560           str r5, [r0, 4]             ; arg1
|       |   ; CODE XREF from spi_flash_read_id @ 0x8000d02(x)
\       `-> 0x08000d42      bde8f081       pop.w {r4, r5, r6, r7, r8, pc}

; CALL XREF from lcd_gpio_init @ 0x80011f0(x)
/ 1078: uart_irq_handler (uint32_t arg1, uint32_t arg2, int16_t arg3);
| `- args(r0, r1, r2)
|           0x08000d48      70b5           push {r4, r5, r6, lr}
|           0x08000d4a      b0f1004f       cmp.w r0, -0x80000000       ; arg1
|       ,=< 0x08000d4e      6cd9           bls 0x8000e2a
|       |   0x08000d50      20f00040       bic r0, r0, 0x80000000      ; arg1
|       |   0x08000d54      c409           lsrs r4, r0, 7              ; arg1
|       |   0x08000d56      00f07f03       and r3, r0, 0x7f            ; arg1
|       |   0x08000d5a      1809           lsrs r0, r3, 4
|       |   0x08000d5c      03f00f03       and r3, r3, 0xf
|       |   0x08000d60      8500           lsls r5, r0, 2
|       |   0x08000d62      03fa05f5       lsl.w r5, r3, r5
|       |   0x08000d66      062c           cmp r4, 6                   ; 6
|      ,==< 0x08000d68      14d2           bhs 0x8000d94
|      ||   ;-- switch:
|      ||   0x08000d6a      dfe804f0       tbb [0x08000d70]            ; switch table (6 cases) at 0x8000d6e
       ||   ; DATA XREF from uart_irq_handler @ 0x8000d6a(r)
..
|      ||   ;-- case 0:                                                ; from 0x08000d6a
|      ||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|      ||   0x08000d74      f84a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|      ||   0x08000d76      0de0           b 0x8000d94
|      ||   ;-- case 1:                                                ; from 0x08000d6a
|      ||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|      ||   0x08000d78      f74a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|      ||   0x08000d7a      121d           adds r2, r2, 4
|     ,===< 0x08000d7c      0ae0           b 0x8000d94
|     |||   ;-- case 2:                                                ; from 0x08000d6a
|     |||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|     |||   0x08000d7e      f64a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|     |||   0x08000d80      0832           adds r2, 8
|    ,====< 0x08000d82      07e0           b 0x8000d94
|    ||||   ;-- case 3:                                                ; from 0x08000d6a
|    ||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|    ||||   0x08000d84      f44a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|    ||||   0x08000d86      0c32           adds r2, 0xc
|   ,=====< 0x08000d88      04e0           b 0x8000d94
|   |||||   ;-- case 4:                                                ; from 0x08000d6a
|   |||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|   |||||   0x08000d8a      f34a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|   |||||   0x08000d8c      1032           adds r2, 0x10
|  ,======< 0x08000d8e      01e0           b 0x8000d94
|  ||||||   ;-- case 5:                                                ; from 0x08000d6a
|  ||||||   ; CODE XREF from uart_irq_handler @ 0x8000d6a(x)
|  ||||||   0x08000d90      f14a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|  ||||||   0x08000d92      1432           adds r2, 0x14
|  ||||||   ; XREFS: CODE 0x08000d68  CODE 0x08000d76  CODE 0x08000d7c  CODE 0x08000d82  CODE 0x08000d88  
|  ||||||   ; XREFS: CODE 0x08000d8e  
| ,`````--> 0x08000d94      70b1           cbz r0, 0x8000db4
| |     |   0x08000d96      0128           cmp r0, 1                   ; 1
| |    ,==< 0x08000d98      11d0           beq 0x8000dbe
| |    ||   0x08000d9a      0228           cmp r0, 2                   ; 2
| |   ,===< 0x08000d9c      22d0           beq 0x8000de4
| |   |||   0x08000d9e      0328           cmp r0, 3                   ; 3
| |  ,====< 0x08000da0      25d0           beq 0x8000dee
| |  ||||   0x08000da2      0428           cmp r0, 4                   ; 4
| | ,=====< 0x08000da4      28d0           beq 0x8000df8
| | |||||   0x08000da6      0528           cmp r0, 5                   ; 5
| |,======< 0x08000da8      2bd0           beq 0x8000e02
| |||||||   0x08000daa      0628           cmp r0, 6                   ; 6
| ========< 0x08000dac      2ed0           beq 0x8000e0c
| |||||||   0x08000dae      0728           cmp r0, 7                   ; 7
| ========< 0x08000db0      31d0           beq 0x8000e16
| ========< 0x08000db2      34e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d94(x)
| `-------> 0x08000db4      1068           ldr r0, [r2]                ; arg3
|  ||||||   0x08000db6      20f00f00       bic r0, r0, 0xf
|  ||||||   0x08000dba      1060           str r0, [r2]                ; arg3
| ,=======< 0x08000dbc      2fe0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d98(x)
| |||||`--> 0x08000dbe      012c           cmp r4, 1                   ; 1
| |||||,==< 0x08000dc0      04d0           beq 0x8000dcc
| |||||||   0x08000dc2      1068           ldr r0, [r2]                ; arg3
| |||||||   0x08000dc4      20f0f000       bic r0, r0, 0xf0
| |||||||   0x08000dc8      1060           str r0, [r2]                ; arg3
| ========< 0x08000dca      28e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000dc0(x)
| |||||`--> 0x08000dcc      042b           cmp r3, 4                   ; 4
| |||||,==< 0x08000dce      04d9           bls 0x8000dda
| |||||||   0x08000dd0      1068           ldr r0, [r2]                ; arg3
| |||||||   0x08000dd2      20f0c000       bic r0, r0, 0xc0
| |||||||   0x08000dd6      1060           str r0, [r2]                ; arg3
| ========< 0x08000dd8      21e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000dce(x)
| |||||`--> 0x08000dda      1068           ldr r0, [r2]                ; arg3
| ||||| |   0x08000ddc      20f03000       bic r0, r0, 0x30
| ||||| |   0x08000de0      1060           str r0, [r2]                ; arg3
| |||||,==< 0x08000de2      1ce0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000d9c(x)
| ||||`---> 0x08000de4      1068           ldr r0, [r2]                ; arg3
| |||| ||   0x08000de6      20f47060       bic r0, r0, 0xf00
| |||| ||   0x08000dea      1060           str r0, [r2]                ; arg3
| ||||,===< 0x08000dec      17e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000da0(x)
| |||`----> 0x08000dee      1068           ldr r0, [r2]                ; arg3
| ||| |||   0x08000df0      20f47040       bic r0, r0, 0xf000
| ||| |||   0x08000df4      1060           str r0, [r2]                ; arg3
| |||,====< 0x08000df6      12e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000da4(x)
| ||`-----> 0x08000df8      1068           ldr r0, [r2]                ; arg3
| || ||||   0x08000dfa      20f47020       bic r0, r0, 0xf0000
| || ||||   0x08000dfe      1060           str r0, [r2]                ; arg3
| ||,=====< 0x08000e00      0de0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000da8(x)
| |`------> 0x08000e02      1068           ldr r0, [r2]                ; arg3
| | |||||   0x08000e04      20f47000       bic r0, r0, 0xf00000
| | |||||   0x08000e08      1060           str r0, [r2]                ; arg3
| |,======< 0x08000e0a      08e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000dac(x)
| --------> 0x08000e0c      1068           ldr r0, [r2]                ; arg3
| |||||||   0x08000e0e      20f07060       bic r0, r0, 0xf000000
| |||||||   0x08000e12      1060           str r0, [r2]                ; arg3
| ========< 0x08000e14      03e0           b 0x8000e1e
| |||||||   ; CODE XREF from uart_irq_handler @ 0x8000db0(x)
| --------> 0x08000e16      1068           ldr r0, [r2]                ; arg3
| |||||||   0x08000e18      20f07040       bic r0, r0, 0xf0000000
| |||||||   0x08000e1c      1060           str r0, [r2]                ; arg3
| |||||||   ; XREFS: CODE 0x08000db2  CODE 0x08000dbc  CODE 0x08000dca  CODE 0x08000dd8  CODE 0x08000de2  
| |||||||   ; XREFS: CODE 0x08000dec  CODE 0x08000df6  CODE 0x08000e00  CODE 0x08000e0a  CODE 0x08000e14  
| ``````--> 0x08000e1e      0129           cmp r1, 1                   ; 1 ; arg2
|      ,==< 0x08000e20      02d1           bne 0x8000e28
|      ||   0x08000e22      1068           ldr r0, [r2]                ; arg3
|      ||   0x08000e24      2843           orrs r0, r5
|      ||   0x08000e26      1060           str r0, [r2]                ; arg3
|      ||   ; CODE XREFS from uart_irq_handler @ 0x8000e20(x), 0x8000e54(x), 0x8000e68(x), 0x8000e74(x)
|   ...`--> 0x08000e28      70bd           pop {r4, r5, r6, pc}
|   ::: |   ; CODE XREF from uart_irq_handler @ 0x8000d4e(x)
|   ::: `-> 0x08000e2a      cb4a           ldr r2, [0x08001158]        ; [0x8001158:4]=0x40010020
|   :::     0x08000e2c      4ff48025       mov.w r5, 0x40000
|   :::     0x08000e30      203a           subs r2, 0x20
|   :::     0x08000e32      a842           cmp r0, r5                  ; arg1
|   ::: ,=< 0x08000e34      0fd0           beq 0x8000e56
|   :::,==< 0x08000e36      41dc           bgt 0x8000ebc
|   :::||   0x08000e38      b0f5007f       cmp.w r0, 0x200             ; 512 ; arg1
|  ,======< 0x08000e3c      74d0           beq 0x8000f28
| ,=======< 0x08000e3e      1fdc           bgt 0x8000e80
| ||:::||   0x08000e40      1028           cmp r0, 0x10                ; 16 ; arg1
| ========< 0x08000e42      72d0           beq 0x8000f2a
| ========< 0x08000e44      08dc           bgt 0x8000e58
| ||:::||   0x08000e46      0128           cmp r0, 1                   ; 1 ; arg1
| ========< 0x08000e48      70d0           beq 0x8000f2c
| ||:::||   0x08000e4a      0228           cmp r0, 2                   ; 2 ; arg1
| ========< 0x08000e4c      03d0           beq 0x8000e56
| ||:::||   0x08000e4e      0428           cmp r0, 4                   ; 4 ; arg1
| ========< 0x08000e50      01d0           beq 0x8000e56
| ||:::||   0x08000e52      0828           cmp r0, 8                   ; 8 ; arg1
| ||`=====< 0x08000e54      e8d1           bne 0x8000e28
| || ::||   ; CODE XREFS from uart_irq_handler @ 0x8000e34(x), 0x8000e4c(x), 0x8000e50(x)
| --,===`-> 0x08000e56      9be0           b 0x8000f90
| |||::|    ; CODE XREF from uart_irq_handler @ 0x8000e44(x)
| --------> 0x08000e58      3028           cmp r0, 0x30                ; 48 ; arg1
| |||::|,=< 0x08000e5a      68d0           beq 0x8000f2e
| |||::||   0x08000e5c      4028           cmp r0, 0x40                ; 64 ; arg1
| ========< 0x08000e5e      67d0           beq 0x8000f30
| |||::||   0x08000e60      c028           cmp r0, 0xc0                ; 192 ; arg1
| ========< 0x08000e62      79d0           beq 0x8000f58
| |||::||   0x08000e64      b0f5807f       cmp.w r0, 0x100             ; 256 ; arg1
| |||`====< 0x08000e68      ded1           bne 0x8000e28
| ||| :||   0x08000e6a      5068           ldr r0, [r2, 4]
| ||| :||   0x08000e6c      20f44070       bic r0, r0, 0x300
| ||| :||   0x08000e70      5060           str r0, [r2, 4]
| ||| :||   0x08000e72      0129           cmp r1, 1                   ; 1 ; arg2
| ||| `===< 0x08000e74      d8d1           bne 0x8000e28
| |||  ||   0x08000e76      5068           ldr r0, [r2, 4]
| |||  ||   0x08000e78      40f48070       orr r0, r0, 0x100
| |||  ||   0x08000e7c      5060           str r0, [r2, 4]
| |||  ||   ; CODE XREFS from uart_irq_handler @ 0x8000e9e(x), 0x8000eb8(x), 0x8000ee2(x), 0x8000eee(x)
| ---..---> 0x08000e7e      70bd           pop {r4, r5, r6, pc}
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e3e(x)
| `-------> 0x08000e80      b0f5804f       cmp.w r0, 0x4000            ; arg1
| ,=======< 0x08000e84      69d0           beq 0x8000f5a
| ========< 0x08000e86      0cdc           bgt 0x8000ea2
| |||::||   0x08000e88      b0f5407f       cmp.w r0, 0x300             ; 768 ; arg1
| ========< 0x08000e8c      7cd0           beq 0x8000f88
| |||::||   0x08000e8e      b0f5006f       cmp.w r0, 0x800             ; 2048 ; arg1
| ========< 0x08000e92      7ad0           beq 0x8000f8a
| |||::||   0x08000e94      b0f5406f       cmp.w r0, 0xc00             ; 3072 ; arg1
| ========< 0x08000e98      78d0           beq 0x8000f8c
| |||::||   0x08000e9a      b0f5805f       cmp.w r0, 0x1000            ; arg1
| ========< 0x08000e9e      eed1           bne 0x8000e7e
| ========< 0x08000ea0      76e0           b 0x8000f90
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e86(x)
| --------> 0x08000ea2      b0f5c04f       cmp.w r0, 0x6000            ; arg1
| ========< 0x08000ea6      72d0           beq 0x8000f8e
| |||::||   0x08000ea8      b0f5004f       cmp.w r0, 0x8000            ; arg1
| ========< 0x08000eac      70d0           beq 0x8000f90
| |||::||   0x08000eae      b0f5803f       cmp.w r0, 0x10000           ; arg1
| ========< 0x08000eb2      6dd0           beq 0x8000f90
| |||::||   0x08000eb4      b0f5003f       cmp.w r0, 0x20000           ; arg1
| ========< 0x08000eb8      e1d1           bne 0x8000e7e
| ========< 0x08000eba      69e0           b 0x8000f90
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e36(x)
| |||::`--> 0x08000ebc      b0f1005f       cmp.w r0, 0x20000000        ; arg1
| |||::,==< 0x08000ec0      6fd0           beq 0x8000fa2
| ========< 0x08000ec2      36dc           bgt 0x8000f32
| |||::||   0x08000ec4      b0f5000f       cmp.w r0, 0x800000          ; arg1
| ========< 0x08000ec8      6cd0           beq 0x8000fa4
| ========< 0x08000eca      16dc           bgt 0x8000efa
| |||::||   0x08000ecc      b0f5002f       cmp.w r0, 0x80000           ; arg1
| ========< 0x08000ed0      5ed0           beq 0x8000f90
| |||::||   0x08000ed2      b0f5801f       cmp.w r0, 0x100000          ; arg1
| ========< 0x08000ed6      5bd0           beq 0x8000f90
| |||::||   0x08000ed8      b0f5001f       cmp.w r0, 0x200000          ; arg1
| ========< 0x08000edc      79d0           beq 0x8000fd2
| |||::||   0x08000ede      b0f5800f       cmp.w r0, 0x400000          ; arg1
| |||`====< 0x08000ee2      ccd1           bne 0x8000e7e
| ||| :||   0x08000ee4      5068           ldr r0, [r2, 4]
| ||| :||   0x08000ee6      20f48000       bic r0, r0, 0x400000
| ||| :||   0x08000eea      5060           str r0, [r2, 4]
| ||| :||   0x08000eec      0129           cmp r1, 1                   ; 1 ; arg2
| ||| `===< 0x08000eee      c6d1           bne 0x8000e7e
| |||  ||   0x08000ef0      5068           ldr r0, [r2, 4]
| |||  ||   0x08000ef2      40f48000       orr r0, r0, 0x400000
| |||  ||   0x08000ef6      5060           str r0, [r2, 4]
| |||  ||   ; CODE XREFS from uart_irq_handler @ 0x8000f10(x), 0x8000f1c(x)
| |||..---> 0x08000ef8      70bd           pop {r4, r5, r6, pc}
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000eca(x)
| --------> 0x08000efa      b0f1807f       cmp.w r0, 0x1000000         ; arg1
| ========< 0x08000efe      69d0           beq 0x8000fd4
| |||::||   0x08000f00      b0f1007f       cmp.w r0, 0x2000000         ; arg1
| ========< 0x08000f04      67d0           beq 0x8000fd6
| |||::||   0x08000f06      b0f1806f       cmp.w r0, 0x4000000         ; arg1
| ========< 0x08000f0a      70d0           beq 0x8000fee
| |||::||   0x08000f0c      b0f1805f       cmp.w r0, 0x10000000        ; arg1
| |||`====< 0x08000f10      f2d1           bne 0x8000ef8
| ||| :||   0x08000f12      5068           ldr r0, [r2, 4]
| ||| :||   0x08000f14      20f08050       bic r0, r0, 0x10000000
| ||| :||   0x08000f18      5060           str r0, [r2, 4]
| ||| :||   0x08000f1a      0129           cmp r1, 1                   ; 1 ; arg2
| ||| `===< 0x08000f1c      ecd1           bne 0x8000ef8
| |||  ||   0x08000f1e      5068           ldr r0, [r2, 4]
| |||  ||   0x08000f20      40f08050       orr r0, r0, 0x10000000
| |||  ||   0x08000f24      5060           str r0, [r2, 4]
| |||  ||   ; XREFS(32)
| ---..---> 0x08000f26      70bd           pop {r4, r5, r6, pc}
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e3c(x)
| =`------> 0x08000f28      78e0           b 0x800101c
| | |::||   ; CODE XREF from uart_irq_handler @ 0x8000e42(x)
| -,======< 0x08000f2a      47e0           b 0x8000fbc
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e48(x)
| ========< 0x08000f2c      3be0           b 0x8000fa6
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e5a(x)
| ======`-> 0x08000f2e      53e0           b 0x8000fd8
| |||::|    ; CODE XREF from uart_irq_handler @ 0x8000e5e(x)
| ------,=< 0x08000f30      5ee0           b 0x8000ff0
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000ec2(x)
| --------> 0x08000f32      8a4c           ldr r4, [0x0800115c]        ; [0x800115c:4]=0x40040000
| |||::||   0x08000f34      031b           subs r3, r0, r4             ; arg1
| |||::||   0x08000f36      a042           cmp r0, r4                  ; arg1
| ========< 0x08000f38      1bd0           beq 0x8000f72
| ========< 0x08000f3a      0fdc           bgt 0x8000f5c
| |||::||   0x08000f3c      b0f1804f       cmp.w r0, 0x40000000        ; arg1
| ========< 0x08000f40      77d0           beq 0x8001032
| |||::||   0x08000f42      a0f18043       sub.w r3, r0, 0x40000000    ; arg1
| |||::||   0x08000f46      203b           subs r3, 0x20
| ========< 0x08000f48      13d0           beq 0x8000f72
| |||::||   0x08000f4a      b3f5787f       cmp.w r3, 0x3e0             ; 992
| ========< 0x08000f4e      10d0           beq 0x8000f72
| |||::||   0x08000f50      834c           ldr r4, [0x08001160]        ; [0x8001160:4]=0xfffe0020
| |||::||   0x08000f52      e342           cmn r3, r4
| ========< 0x08000f54      e7d1           bne 0x8000f26
| ========< 0x08000f56      0ce0           b 0x8000f72
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e62(x)
| --------> 0x08000f58      55e0           b 0x8001006
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e84(x)
| `-------> 0x08000f5a      8fe0           b 0x800107c
|  ||::||   ; CODE XREF from uart_irq_handler @ 0x8000f3a(x)
| --------> 0x08000f5c      ab42           cmp r3, r5
| ,=======< 0x08000f5e      69d0           beq 0x8001034
| |||::||   0x08000f60      b3f5402f       cmp.w r3, 0xc0000
| ========< 0x08000f64      67d0           beq 0x8001036
| |||::||   0x08000f66      b3f5a01f       cmp.w r3, 0x140000
| ========< 0x08000f6a      70d0           beq 0x800104e
| |||::||   0x08000f6c      b3f5e01f       cmp.w r3, 0x1c0000
| ========< 0x08000f70      d9d1           bne 0x8000f26
| |||::||   ; CODE XREFS from uart_irq_handler @ 0x8000f38(x), 0x8000f48(x), 0x8000f4e(x), 0x8000f56(x)
| --------> 0x08000f72      d369           ldr r3, [r2, 0x1c]
| |||::||   0x08000f74      c0f31500       ubfx r0, r0, 0, 0x16        ; arg1
| |||::||   0x08000f78      8343           bics r3, r0                 ; arg1
| |||::||   0x08000f7a      d361           str r3, [r2, 0x1c]
| |||::||   0x08000f7c      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08000f7e      d2d1           bne 0x8000f26
| |||::||   0x08000f80      d169           ldr r1, [r2, 0x1c]
| |||::||   0x08000f82      0143           orrs r1, r0                 ; arg1
| |||::||   0x08000f84      d161           str r1, [r2, 0x1c]
| |||::||   ; CODE XREFS from uart_irq_handler @ 0x8001026(x), 0x8001042(x), 0x800105a(x), 0x8001070(x)
| --------> 0x08000f86      cee7           b 0x8000f26
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e8c(x)
| --------> 0x08000f88      56e0           b 0x8001038
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e92(x)
| ========< 0x08000f8a      61e0           b 0x8001050
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000e98(x)
| ========< 0x08000f8c      6be0           b 0x8001066
| |||::||   ; CODE XREF from uart_irq_handler @ 0x8000ea6(x)
| --------> 0x08000f8e      80e0           b 0x8001092
| |||::||   ; XREFS: CODE 0x08000e56  CODE 0x08000ea0  CODE 0x08000eac  CODE 0x08000eb2  CODE 0x08000eba  
| |||::||   ; XREFS: CODE 0x08000ed0  CODE 0x08000ed6  
| --`-----> 0x08000f90      5368           ldr r3, [r2, 4]
| || ::||   0x08000f92      8343           bics r3, r0                 ; arg1
| || ::||   0x08000f94      5360           str r3, [r2, 4]
| || ::||   0x08000f96      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08000f98      c5d1           bne 0x8000f26
| || ::||   0x08000f9a      5168           ldr r1, [r2, 4]
| || ::||   0x08000f9c      0143           orrs r1, r0                 ; arg1
| || ::||   0x08000f9e      5160           str r1, [r2, 4]
| || ::||   ; CODE XREF from uart_irq_handler @ 0x8001086(x)
| ==.-----> 0x08000fa0      c1e7           b 0x8000f26
| ||:::||   ; CODE XREF from uart_irq_handler @ 0x8000ec0(x)
| =====`--> 0x08000fa2      ade0           b 0x8001100
| ||::: |   ; CODE XREF from uart_irq_handler @ 0x8000ec8(x)
| -----,==< 0x08000fa4      b7e0           b 0x8001116
| ||:::||   ; CODE XREF from uart_irq_handler @ 0x8000f2c(x)
| --------> 0x08000fa6      5068           ldr r0, [r2, 4]
| ||:::||   0x08000fa8      6e4b           ldr r3, [0x08001164]        ; [0x8001164:4]=0x7ffffffe
| ||:::||   0x08000faa      1840           ands r0, r3
| ||:::||   0x08000fac      5060           str r0, [r2, 4]
| ||:::||   0x08000fae      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08000fb0      b9d1           bne 0x8000f26
| ||:::||   0x08000fb2      5068           ldr r0, [r2, 4]
| ||:::||   0x08000fb4      40f00100       orr r0, r0, 1
| ||:::||   0x08000fb8      5060           str r0, [r2, 4]
| ||:::||   ; CODE XREF from uart_irq_handler @ 0x800109c(x)
| --------> 0x08000fba      b4e7           b 0x8000f26
| ||:::||   ; CODE XREF from uart_irq_handler @ 0x8000f2a(x)
| |`------> 0x08000fbc      5068           ldr r0, [r2, 4]
| | :::||   0x08000fbe      20f03000       bic r0, r0, 0x30
| | :::||   0x08000fc2      5060           str r0, [r2, 4]
| | :::||   0x08000fc4      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08000fc6      aed1           bne 0x8000f26
| | :::||   0x08000fc8      5068           ldr r0, [r2, 4]
| | :::||   0x08000fca      40f01000       orr r0, r0, 0x10
| | :::||   0x08000fce      5060           str r0, [r2, 4]
| | :::||   ; CODE XREF from uart_irq_handler @ 0x80010b2(x)
| =.------> 0x08000fd0      a9e7           b 0x8000f26
| |::::||   ; CODE XREF from uart_irq_handler @ 0x8000edc(x)
| ========< 0x08000fd2      abe0           b 0x800112c
| |::::||   ; CODE XREF from uart_irq_handler @ 0x8000efe(x)
| ========< 0x08000fd4      68e0           b 0x80010a8
| |::::||   ; CODE XREF from uart_irq_handler @ 0x8000f04(x)
| ========< 0x08000fd6      72e0           b 0x80010be
| |::::||   ; CODE XREF from uart_irq_handler @ 0x8000f2e(x)
| --------> 0x08000fd8      5068           ldr r0, [r2, 4]
| |::::||   0x08000fda      20f03000       bic r0, r0, 0x30
| |::::||   0x08000fde      5060           str r0, [r2, 4]
| |::::||   0x08000fe0      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08000fe2      a0d1           bne 0x8000f26
| |::::||   0x08000fe4      5068           ldr r0, [r2, 4]
| |::::||   0x08000fe6      40f03000       orr r0, r0, 0x30
| |::::||   0x08000fea      5060           str r0, [r2, 4]
| |::::||   ; CODE XREFS from uart_irq_handler @ 0x80010c8(x), 0x80010de(x)
| --------> 0x08000fec      9be7           b 0x8000f26
| |::::||   ; CODE XREF from uart_irq_handler @ 0x8000f0a(x)
| ========< 0x08000fee      71e0           b 0x80010d4
| |::::||   ; CODE XREF from uart_irq_handler @ 0x8000f30(x)
| |::::|`-> 0x08000ff0      5068           ldr r0, [r2, 4]
| |::::|    0x08000ff2      20f0c000       bic r0, r0, 0xc0
| |::::|    0x08000ff6      5060           str r0, [r2, 4]
| |::::|    0x08000ff8      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08000ffa      94d1           bne 0x8000f26
| |::::|    0x08000ffc      5068           ldr r0, [r2, 4]
| |::::|    0x08000ffe      40f04000       orr r0, r0, 0x40
| |::::|    0x08001002      5060           str r0, [r2, 4]
| |::::|    ; CODE XREF from uart_irq_handler @ 0x80010f4(x)
| ======.-> 0x08001004      8fe7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f58(x)
| --------> 0x08001006      5068           ldr r0, [r2, 4]
| |::::|:   0x08001008      20f0c000       bic r0, r0, 0xc0
| |::::|:   0x0800100c      5060           str r0, [r2, 4]
| |::::|:   0x0800100e      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001010      89d1           bne 0x8000f26
| |::::|:   0x08001012      5068           ldr r0, [r2, 4]
| |::::|:   0x08001014      40f0c000       orr r0, r0, 0xc0
| |::::|:   0x08001018      5060           str r0, [r2, 4]
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x800110a(x)
| --------> 0x0800101a      84e7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f28(x)
| --------> 0x0800101c      5068           ldr r0, [r2, 4]
| |::::|:   0x0800101e      20f44070       bic r0, r0, 0x300
| |::::|:   0x08001022      5060           str r0, [r2, 4]
| |::::|:   0x08001024      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001026      aed1           bne 0x8000f86
| |::::|:   0x08001028      5068           ldr r0, [r2, 4]
| |::::|:   0x0800102a      40f40070       orr r0, r0, 0x200
| |::::|:   0x0800102e      5060           str r0, [r2, 4]
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8001120(x)
| --------> 0x08001030      79e7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f40(x)
| ========< 0x08001032      5ae0           b 0x80010ea
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f5e(x)
| `-------> 0x08001034      85e0           b 0x8001142
|  ::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f64(x)
| ,=======< 0x08001036      97e0           b 0x8001168
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f88(x)
| --------> 0x08001038      5068           ldr r0, [r2, 4]
| |::::|:   0x0800103a      20f44070       bic r0, r0, 0x300
| |::::|:   0x0800103e      5060           str r0, [r2, 4]
| |::::|:   0x08001040      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001042      a0d1           bne 0x8000f86
| |::::|:   0x08001044      5068           ldr r0, [r2, 4]
| |::::|:   0x08001046      40f44070       orr r0, r0, 0x300
| |::::|:   0x0800104a      5060           str r0, [r2, 4]
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8001136(x)
| --------> 0x0800104c      6be7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f6a(x)
| ========< 0x0800104e      96e0           b 0x800117e
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f8a(x)
| --------> 0x08001050      5068           ldr r0, [r2, 4]
| |::::|:   0x08001052      20f44060       bic r0, r0, 0xc00
| |::::|:   0x08001056      5060           str r0, [r2, 4]
| |::::|:   0x08001058      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x0800105a      94d1           bne 0x8000f86
| |::::|:   0x0800105c      5068           ldr r0, [r2, 4]
| |::::|:   0x0800105e      40f40060       orr r0, r0, 0x800
| |::::|:   0x08001062      5060           str r0, [r2, 4]
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x800114c(x)
| --------> 0x08001064      5fe7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f8c(x)
| --------> 0x08001066      5068           ldr r0, [r2, 4]
| |::::|:   0x08001068      20f44060       bic r0, r0, 0xc00
| |::::|:   0x0800106c      5060           str r0, [r2, 4]
| |::::|:   0x0800106e      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001070      89d1           bne 0x8000f86
| |::::|:   0x08001072      5068           ldr r0, [r2, 4]
| |::::|:   0x08001074      40f44060       orr r0, r0, 0xc00
| |::::|:   0x08001078      5060           str r0, [r2, 4]
| ========< 0x0800107a      54e7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f5a(x)
| --------> 0x0800107c      5068           ldr r0, [r2, 4]
| |::::|:   0x0800107e      20f4c040       bic r0, r0, 0x6000
| |::::|:   0x08001082      5060           str r0, [r2, 4]
| |::::|:   0x08001084      0129           cmp r1, 1                   ; 1 ; arg2
| |:`=====< 0x08001086      8bd1           bne 0x8000fa0
| |: ::|:   0x08001088      5068           ldr r0, [r2, 4]
| |: ::|:   0x0800108a      40f48040       orr r0, r0, 0x4000
| |: ::|:   0x0800108e      5060           str r0, [r2, 4]
| |: ::|:   ; CODE XREF from uart_irq_handler @ 0x8001172(x)
| ==.-----> 0x08001090      49e7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000f8e(x)
| --------> 0x08001092      5068           ldr r0, [r2, 4]
| |::::|:   0x08001094      20f4c040       bic r0, r0, 0x6000
| |::::|:   0x08001098      5060           str r0, [r2, 4]
| |::::|:   0x0800109a      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x0800109c      8dd1           bne 0x8000fba
| |::::|:   0x0800109e      5068           ldr r0, [r2, 4]
| |::::|:   0x080010a0      40f4c040       orr r0, r0, 0x6000
| |::::|:   0x080010a4      5060           str r0, [r2, 4]
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8001188(x)
| --------> 0x080010a6      3ee7           b 0x8000f26
| |::::|:   ; CODE XREF from uart_irq_handler @ 0x8000fd4(x)
| --------> 0x080010a8      5068           ldr r0, [r2, 4]
| |::::|:   0x080010aa      20f0e060       bic r0, r0, 0x7000000
| |::::|:   0x080010ae      5060           str r0, [r2, 4]
| |::::|:   0x080010b0      0129           cmp r1, 1                   ; 1 ; arg2
| |`======< 0x080010b2      8dd1           bne 0x8000fd0
| | :::|:   0x080010b4      5068           ldr r0, [r2, 4]
| | :::|:   0x080010b6      40f08070       orr r0, r0, 0x1000000
| | :::|:   0x080010ba      5060           str r0, [r2, 4]
| ========< 0x080010bc      33e7           b 0x8000f26
| | :::|:   ; CODE XREF from uart_irq_handler @ 0x8000fd6(x)
| --------> 0x080010be      5068           ldr r0, [r2, 4]
| | :::|:   0x080010c0      20f0e060       bic r0, r0, 0x7000000
| | :::|:   0x080010c4      5060           str r0, [r2, 4]
| | :::|:   0x080010c6      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x080010c8      90d1           bne 0x8000fec
| | :::|:   0x080010ca      5068           ldr r0, [r2, 4]
| | :::|:   0x080010cc      40f00070       orr r0, r0, 0x2000000
| | :::|:   0x080010d0      5060           str r0, [r2, 4]
| ========< 0x080010d2      28e7           b 0x8000f26
| | :::|:   ; CODE XREF from uart_irq_handler @ 0x8000fee(x)
| --------> 0x080010d4      5068           ldr r0, [r2, 4]
| | :::|:   0x080010d6      20f0e060       bic r0, r0, 0x7000000
| | :::|:   0x080010da      5060           str r0, [r2, 4]
| | :::|:   0x080010dc      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x080010de      85d1           bne 0x8000fec
| | :::|:   0x080010e0      5068           ldr r0, [r2, 4]
| | :::|:   0x080010e2      40f08060       orr r0, r0, 0x4000000
| | :::|:   0x080010e6      5060           str r0, [r2, 4]
| ========< 0x080010e8      1de7           b 0x8000f26
| | :::|:   ; CODE XREF from uart_irq_handler @ 0x8001032(x)
| --------> 0x080010ea      5068           ldr r0, [r2, 4]
| | :::|:   0x080010ec      20f08040       bic r0, r0, 0x40000000
| | :::|:   0x080010f0      5060           str r0, [r2, 4]
| | :::|:   0x080010f2      0129           cmp r1, 1                   ; 1 ; arg2
| | :::|`=< 0x080010f4      86d1           bne 0x8001004
| | :::|    0x080010f6      5068           ldr r0, [r2, 4]
| | :::|    0x080010f8      40f08040       orr r0, r0, 0x40000000
| | :::|    0x080010fc      5060           str r0, [r2, 4]
| ========< 0x080010fe      12e7           b 0x8000f26
| | :::|    ; CODE XREF from uart_irq_handler @ 0x8000fa2(x)
| --------> 0x08001100      5068           ldr r0, [r2, 4]
| | :::|    0x08001102      20f00050       bic r0, r0, 0x20000000
| | :::|    0x08001106      5060           str r0, [r2, 4]
| | :::|    0x08001108      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x0800110a      86d1           bne 0x800101a
| | :::|    0x0800110c      5068           ldr r0, [r2, 4]
| | :::|    0x0800110e      40f00050       orr r0, r0, 0x20000000
| | :::|    0x08001112      5060           str r0, [r2, 4]
| ========< 0x08001114      07e7           b 0x8000f26
| | :::|    ; CODE XREF from uart_irq_handler @ 0x8000fa4(x)
| | :::`--> 0x08001116      5068           ldr r0, [r2, 4]
| | :::     0x08001118      20f40000       bic r0, r0, 0x800000
| | :::     0x0800111c      5060           str r0, [r2, 4]
| | :::     0x0800111e      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001120      86d1           bne 0x8001030
| | :::     0x08001122      5068           ldr r0, [r2, 4]
| | :::     0x08001124      40f40000       orr r0, r0, 0x800000
| | :::     0x08001128      5060           str r0, [r2, 4]
| ========< 0x0800112a      fce6           b 0x8000f26
| | :::     ; CODE XREF from uart_irq_handler @ 0x8000fd2(x)
| --------> 0x0800112c      5068           ldr r0, [r2, 4]
| | :::     0x0800112e      20f40010       bic r0, r0, 0x200000
| | :::     0x08001132      5060           str r0, [r2, 4]
| | :::     0x08001134      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001136      89d1           bne 0x800104c
| | :::     0x08001138      5068           ldr r0, [r2, 4]
| | :::     0x0800113a      40f40010       orr r0, r0, 0x200000
| | :::     0x0800113e      5060           str r0, [r2, 4]
| ========< 0x08001140      f1e6           b 0x8000f26
| | :::     ; CODE XREF from uart_irq_handler @ 0x8001034(x)
| --------> 0x08001142      d069           ldr r0, [r2, 0x1c]
| | :::     0x08001144      20f4c010       bic r0, r0, 0x180000
| | :::     0x08001148      d061           str r0, [r2, 0x1c]
| | :::     0x0800114a      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x0800114c      8ad1           bne 0x8001064
| | :::     0x0800114e      d069           ldr r0, [r2, 0x1c]
| | :::     0x08001150      40f40020       orr r0, r0, 0x80000
| | :::     0x08001154      d061           str r0, [r2, 0x1c]
| ========< 0x08001156      e6e6           b 0x8000f26
  | :::     ; XREFS: DATA 0x08000d74  DATA 0x08000d78  DATA 0x08000d7e  DATA 0x08000d84  DATA 0x08000d8a  
  | :::     ; XREFS: DATA 0x08000d90  DATA 0x08000e2a  
..
  | :::     ; DATA XREF from uart_irq_handler @ 0x8000f32(r)
  | :::     ; DATA XREF from uart_irq_handler @ 0x8000f50(r)
| | :::     ; CODE XREF from uart_irq_handler @ 0x8001036(x)
| `-------> 0x08001168      d069           ldr r0, [r2, 0x1c]
|   :::     0x0800116a      20f4c010       bic r0, r0, 0x180000
|   :::     0x0800116e      d061           str r0, [r2, 0x1c]
|   :::     0x08001170      0129           cmp r1, 1                   ; 1 ; arg2
|   `=====< 0x08001172      8dd1           bne 0x8001090
|    ::     0x08001174      d069           ldr r0, [r2, 0x1c]
|    ::     0x08001176      40f48010       orr r0, r0, 0x100000
|    ::     0x0800117a      d061           str r0, [r2, 0x1c]
|    `====< 0x0800117c      d3e6           b 0x8000f26
|     :     ; CODE XREF from uart_irq_handler @ 0x800104e(x)
| --------> 0x0800117e      d069           ldr r0, [r2, 0x1c]
|     :     0x08001180      20f4c010       bic r0, r0, 0x180000
|     :     0x08001184      d061           str r0, [r2, 0x1c]
|     :     0x08001186      0129           cmp r1, 1                   ; 1 ; arg2
| ========< 0x08001188      8dd1           bne 0x80010a6
|     :     0x0800118a      d069           ldr r0, [r2, 0x1c]
|     :     0x0800118c      40f4c010       orr r0, r0, 0x180000
|     :     0x08001190      d061           str r0, [r2, 0x1c]
\     `===< 0x08001192      c8e6           b 0x8000f26

; CALL XREFS from check_update_button @ 0x80004a8(x), 0x80004b4(x), 0x80004c2(x), 0x80004cc(x)
            ; CALL XREF from uart_rx_handler @ 0x80006be(x)
/ 14: flash_unlock (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x08001194      0246           mov r2, r0                  ; arg1
|           0x08001196      0020           movs r0, 0
|           0x08001198      9268           ldr r2, [r2, 8]
|           0x0800119a      0a42           tst r2, r1                  ; arg2
|       ,=< 0x0800119c      00d0           beq 0x80011a0
|       |   0x0800119e      0120           movs r0, 1
|       |   ; CODE XREF from flash_unlock @ 0x800119c(x)
\       `-> 0x080011a0      7047           bx lr

; XREFS: CALL 0x0800041e  CALL 0x080006d4  CALL 0x080012d2  CALL 0x080012da  CALL 0x080012e2  
            ; XREFS: CALL 0x0800159e  CALL 0x08001c32  CALL 0x08001c9a  CALL 0x08001d4a  CALL 0x08001e24  
            ; XREFS: CALL 0x08002372  CALL 0x0800237a  CALL 0x08002382  CALL 0x080023d2  CALL 0x080023e2  
/ 4: fcn.080011a2 (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x080011a2      4161           str r1, [r0, 0x14]          ; arg2
\           0x080011a4      7047           bx lr

; XREFS: CALL 0x0800023c  CALL 0x08000482  CODE 0x08000880  CALL 0x080008ac  CALL 0x080008de  
; --------------------------------------------------------------------------
; uart_set_baud (fcn.080011a6): Set UART baud rate divisor.
; C pseudocode: void uart_set_baud(UART_TypeDef *uart, uint16_t divisor) {
;                   uart->BRR = divisor;  // offset 0x10 in UART registers
;               }
; Called with 0x40011800 (UART4) and divisor 0x400.
; --------------------------------------------------------------------------
            ; XREFS: CODE 0x08000906  CODE 0x08000924  CALL 0x0800121c  CALL 0x08001590  CALL 0x080015ac  
            ; XREFS: CALL 0x08001c70  CALL 0x08001cb4  CALL 0x08001d88  CALL 0x08001e38  CALL 0x080023b4  
            ; XREFS: CODE 0x080023c0  CALL 0x080023da  CALL 0x08002414  CODE 0x08002420  
/ 4: fcn.080011a6 (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x080011a6      0161           str r1, [r0, 0x10]          ; arg2
\           0x080011a8      7047           bx lr

; XREFS: CALL 0x080011f6  CALL 0x0800123c  CALL 0x080012ae  CALL 0x080012fc  CALL 0x08001336  
            ; XREFS: CALL 0x0800215a  
/ 16: fcn.080011aa (int16_t arg1);
| `- args(r0)
|           0x080011aa      4ff6ff71       movw r1, 0xffff
|           0x080011ae      0180           strh r1, [r0]               ; arg1
|           0x080011b0      0221           movs r1, 2
|           0x080011b2      8170           strb r1, [r0, 2]            ; arg1
|           0x080011b4      0421           movs r1, 4
|           0x080011b6      c170           strb r1, [r0, 3]            ; arg1
\           0x080011b8      7047           bx lr

; CALL XREFS from lcd_gpio_init @ 0x8001264(x), 0x800131e(x), 0x800135a(x)
            ; CALL XREF from model_xor_decode @ 0x8002394(x)
            ; CALL XREF from model_xor_encode @ 0x80023f4(x)
/ 4: fcn.080011ba (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x080011ba      c160           str r1, [r0, 0xc]           ; arg2
\           0x080011bc      7047           bx lr

; CALL XREF from uart_update_mode @ 0x800035a(x)
; --------------------------------------------------------------------------
; set_decryption_key: Copy 16-byte XOR key from packet to RAM.
;
; C pseudocode:
;   void set_decryption_key(uint8_t *src) {
;       uint8_t *dst = (uint8_t *)0x20000034;  // key storage in RAM
;       for (int i = 0; i < 16; i++)
;           dst[i] = src[i];
;   }
;
; Called with block 1 data: key = packet_data[6..21] (16 bytes).
; No key expansion - the in-place ROL/ROR rotation during decryption
; IS the key schedule. After 128 bytes (8 rotations), key resets.
; --------------------------------------------------------------------------
/ 18: set_decryption_key (int16_t arg1);
| `- args(r0)
|           0x080011c0      0021           movs r1, 0                  ; i = 0
|           0x080011c2      044a           ldr r2, [0x080011d4]        ; [0x80011d4:4]=0x20000034
|           ; CODE XREF from set_decryption_key @ 0x80011ce(x)
|       .-> 0x080011c4      435c           ldrb r3, [r0, r1]           ; src[i]
|       :   0x080011c6      5354           strb r3, [r2, r1]           ; dst[i] = src[i]
|       :   0x080011c8      491c           adds r1, r1, 1              ; i++
|       :   0x080011ca      c9b2           uxtb r1, r1
|       :   0x080011cc      1029           cmp r1, 0x10                ; 16
|       `=< 0x080011ce      f9d3           blo 0x80011c4               ; loop while i < 16
\           0x080011d0      7047           bx lr

; --------------------------------------------------------------------------
; copy_bytes (hardfault_handler): Byte-by-byte memory copy.
; Despite the misleading radare2 name, this is a simple memcpy.
;
; C pseudocode:
;   int copy_bytes(uint8_t *src, uint8_t *dst, uint32_t len) {
;       for (int i = 0; i < len; i++)
;           dst[i] = *src++;
;       return 1;
;   }
; --------------------------------------------------------------------------
; CALL XREF from check_spi_model @ 0x800066e(x)
/ 22: hardfault_handler (int16_t arg1, int16_t arg2, uint32_t arg3);
| `- args(r0, r1, r2)
|           0x08001392      10b5           push {r4, lr}
|           0x08001394      0023           movs r3, 0
|       ,=< 0x08001396      03e0           b 0x80013a0
|       |   ; CODE XREF from hardfault_handler @ 0x80013a2(x)
|      .--> 0x08001398      10f8014b       ldrb r4, [r0], 1            ; arg1
|      :|   0x0800139c      cc54           strb r4, [r1, r3]           ; arg2
|      :|   0x0800139e      5b1c           adds r3, r3, 1
|      :|   ; CODE XREF from hardfault_handler @ 0x8001396(x)
|      :`-> 0x080013a0      9342           cmp r3, r2                  ; arg3
|      `==< 0x080013a2      f9d3           blo 0x8001398
|           0x080013a4      0120           movs r0, 1
\           0x080013a6      10bd           pop {r4, pc}

; CALL XREF from uart_update_mode @ 0x800036a(x)
; --------------------------------------------------------------------------
; flash_from_spi: Decrypt firmware block and write to internal flash.
;
; This is the CRITICAL decryption function. See file header for full
; C pseudocode and algorithm details.
;
; Args: r0 = flash offset from app base, r1 = pointer to 1024-byte data
; Returns: 1 = success, 0 = out of bounds, 2 = erase error
;
; Register allocation:
;   r4 = offset, r5 = data pointer, r6 = key_idx (0-15)
;   r7 = flash_addr (0x08003000 + offset), r8 = success flag
;   sb = 0x20000034 (16-byte key in RAM), sl = 0x400 (1024)
;   fp = &key[8] (used for ROR half during rotation)
; --------------------------------------------------------------------------
/ 222: flash_from_spi (int16_t arg1, int16_t arg2, int16_t arg_8h);
| `- args(r0, r1, sp[0x8..0x8])
|           0x080013a8      2de9f05f       push.w {r4, r5, r6, r7, r8, sb, sl, fp, ip, lr}
|           0x080013ac      0446           mov r4, r0                  ; r4 = offset
|           0x080013ae      0d46           mov r5, r1                  ; r5 = data pointer
|           0x080013b0      4ff00108       mov.w r8, 1                 ; r8 = success (assume ok)
|           ; === BOUNDS CHECK: offset must be < 0xFD000 ===
|           0x080013b4      b4f57d2f       cmp.w r4, 0xfd000
|       ,=< 0x080013b8      02d3           blo 0x80013c0               ; in bounds
|       |   0x080013ba      0020           movs r0, 0                  ; return 0 (out of bounds)
|       |   ; CODE XREFS from flash_from_spi @ 0x80013da(x), 0x8001484(x)
|     ..--> 0x080013bc      bde8f09f       pop.w {r4, r5, r6, r7, r8, sb, sl, fp, ip, pc}
|     ::|   ; CODE XREF from flash_from_spi @ 0x80013b8(x)
|     ::`-> 0x080013c0      3148           ldr r0, aav.0x08003000      ; [0x8001488:4]=0x8003000 aav.0x08003000
|     ::    ; r7 = flash_addr = 0x08003000 + offset
|     ::    0x080013c2      2718           adds r7, r4, r0
|     ::    ; === DISABLE INTERRUPTS for flash operations ===
|     ::    0x080013c4      72b6           cpsid i
|     ::    0x080013c6      fff71dfc       bl spi_cs_low
|     ::    ; === PAGE ERASE: if offset is 2KB-aligned (bit test via lsl 0x15) ===
|     ::    ; (offset << 0x15) == 0 iff offset is a multiple of 2048
|     ::    0x080013ca      6005           lsls r0, r4, 0x15
|     ::,=< 0x080013cc      06d1           bne 0x80013dc               ; not aligned → skip erase
|     ::|   0x080013ce      3846           mov r0, r7                  ; int16_t arg1
|     ::|   0x080013d0      fff724fb       bl spi_flash_read           ; erase flash page
|     ::|   0x080013d4      0428           cmp r0, 4                   ; 4 = erase success
|    ,====< 0x080013d6      01d0           beq 0x80013dc               ; success → continue
|    |::|   0x080013d8      0220           movs r0, 2                  ; return 2 (erase error)
|    |`===< 0x080013da      efe7           b 0x80013bc
|    | :|   ; CODE XREFS from flash_from_spi @ 0x80013cc(x), 0x80013d6(x)
|          ; === RETRY COUNTER: if *0x20000018 < 2, set to 5 ===
|    `--`-> 0x080013dc      2b48           ldr r0, [0x0800148c]        ; [0x800148c:4]=0x20000018
|      :    0x080013de      0168           ldr r1, [r0]                ; retry counter
|      :    0x080013e0      0229           cmp r1, 2                   ; 2
|      :,=< 0x080013e2      01d2           bhs 0x80013e8               ; >= 2 → keep
|      :|   0x080013e4      0521           movs r1, 5                  ; default to 5
|      :|   0x080013e6      0160           str r1, [r0]
|      :|   ; CODE XREF from flash_from_spi @ 0x80013e2(x)
|      :`-> 0x080013e8      0068           ldr r0, [r0]
|      :    ; max_offset = (retry - 1) * 0x400
|      :    0x080013ea      401e           subs r0, r0, 1
|      :    0x080013ec      8002           lsls r0, r0, 0xa            ; r0 = max decryptable offset
|      :    0x080013ee      4ff4806a       mov.w sl, 0x400             ; sl = 1024 (block size)
|      :    ; === DECRYPTION RANGE CHECK ===
|      :    ; Only decrypt if offset >= 0x400 (skip block 0 = plaintext vectors)
|      :    ; and offset < max_offset
|      :    0x080013f2      5445           cmp r4, sl
|      :,=< 0x080013f4      30d3           blo 0x8001458               ; offset < 0x400 → skip decrypt
|      :|   0x080013f6      8442           cmp r4, r0
|     ,===< 0x080013f8      2ed2           bhs 0x8001458               ; offset >= max → skip decrypt
|     |:|   ; === DECRYPTION LOOP: 1024 bytes with XOR + skip rules ===
|     |:|   0x080013fa      0026           movs r6, 0                  ; r6 = key_idx (0-15)
|     |:|   0x080013fc      0024           movs r4, 0                  ; r4 = byte index i (0-1023)
|     |:|   ; sb = key pointer at 0x20000034
|     |:|   0x080013fe      dff89090       ldr.w sb, [0x08001490]      ; [0x8001490:4]=0x20000034
|     |:|   ; CODE XREF from flash_from_spi @ 0x8001456(x)
|           ; --- Per-byte decryption: for (i = 0; i < 1024; i++) ---
|    .----> 0x08001402      2a5d           ldrb r2, [r5, r4]           ; r2 = data[i]
|           ; Skip condition 1: data[i] == 0x00 (plaintext zero)
|   ,=====< 0x08001404      5ab1           cbz r2, 0x800141e           ; → skip (no decrypt)
|   |:|:|   ; Skip condition 2: data[i] == 0xFF (plaintext erased byte)
|   |:|:|   0x08001406      ff2a           cmp r2, 0xff                ; 255
|  ,======< 0x08001408      09d0           beq 0x800141e               ; → skip
|  ||:|:|   ; Load key[key_idx]
|  ||:|:|   0x0800140a      19f80600       ldrb.w r0, [sb, r6]         ; r0 = key[key_idx]
|  ||:|:|   ; Skip condition 3: data[i] == key[key_idx] (XOR would produce 0x00)
|  ||:|:|   0x0800140e      8242           cmp r2, r0
| ,=======< 0x08001410      05d0           beq 0x800141e               ; → skip
| |||:|:|   ; Skip condition 4: data[i] == key[key_idx] ^ 0xFF (XOR would produce 0xFF)
| |||:|:|   0x08001412      80f0ff01       eor r1, r0, 0xff
| |||:|:|   0x08001416      8a42           cmp r2, r1
| ========< 0x08001418      01d0           beq 0x800141e               ; → skip
| |||:|:|   ; All skip conditions failed → DECRYPT: data[i] ^= key[key_idx]
| |||:|:|   0x0800141a      4240           eors r2, r0
| |||:|:|   0x0800141c      2a55           strb r2, [r5, r4]           ; data[i] = decrypted byte
| |||:|:|   ; CODE XREFS from flash_from_spi @ 0x8001404(x), 0x8001408(x), 0x8001410(x), 0x8001418(x)
|           ; --- Advance key index (regardless of skip/decrypt) ---
| ```-----> 0x0800141e      761c           adds r6, r6, 1              ; key_idx++
|    :|:|   0x08001420      06f00f06       and r6, r6, 0xf             ; key_idx &= 0x0F (wrap at 16)
|           ; If key_idx wrapped to 0 → rotate the 16-byte key
|   ,=====< 0x08001424      a6b9           cbnz r6, 0x8001450          ; not zero → skip rotation
|   |:|:|   ; === KEY ROTATION: every 16 bytes, rotate key in-place ===
|   |:|:|   ; Bytes 0-7: ROL8 by 1 (direction=1)
|   |:|:|   ; Bytes 8-15: ROR8 by 1 (direction=0)
|   |:|:|   0x08001426      0023           movs r3, 0                  ; j = 0
|   |:|:|   ; CODE XREF from flash_from_spi @ 0x800144e(x)
|  .------> 0x08001428      0121           movs r1, 1                  ; direction=1 (ROL)
|  :|:|:|   0x0800142a      19f80300       ldrb.w r0, [sb, r3]         ; key[j] (bytes 0-7)
|  :|:|:|   0x0800142e      fff7c6f9       bl fcn.080007be             ; rotate_byte(key[j], 1) → ROL8
|  :|:|:|   0x08001432      09f80300       strb.w r0, [sb, r3]         ; key[j] = ROL8(key[j])
|  :|:|:|   ; Now rotate key[j+8] with ROR (direction=0)
|  :|:|:|   0x08001436      09eb030b       add.w fp, sb, r3            ; fp = &key[j]
|  :|:|:|   0x0800143a      0021           movs r1, 0                  ; direction=0 (ROR)
|  :|:|:|   0x0800143c      9bf80800       ldrb.w r0, [arg_8h]         ; key[j+8] (bytes 8-15)
|  :|:|:|   0x08001440      fff7bdf9       bl fcn.080007be             ; rotate_byte(key[j+8], 0) → ROR8
|  :|:|:|   0x08001444      8bf80800       strb.w r0, [arg_8h]         ; key[j+8] = ROR8(key[j+8])
|  :|:|:|   0x08001448      5b1c           adds r3, r3, 1              ; j++
|  :|:|:|   0x0800144a      dbb2           uxtb r3, r3
|  :|:|:|   0x0800144c      082b           cmp r3, 8                   ; 8
|  `======< 0x0800144e      ebd3           blo 0x8001428               ; loop j = 0..7
|   |:|:|   ; CODE XREF from flash_from_spi @ 0x8001424(x)
|           ; --- Continue byte loop ---
|   `-----> 0x08001450      641c           adds r4, r4, 1              ; i++
|    :|:|   0x08001452      a4b2           uxth r4, r4
|    :|:|   0x08001454      5445           cmp r4, sl                  ; i < 1024?
|    `====< 0x08001456      d4d3           blo 0x8001402               ; next byte
|     |:|   ; CODE XREFS from flash_from_spi @ 0x80013f4(x), 0x80013f8(x)
|           ; === FLASH WRITE LOOP: write 256 words (1024 bytes) and verify ===
|     `-`-> 0x08001458      0024           movs r4, 0                  ; word index = 0
|      :    ; CODE XREF from flash_from_spi @ 0x800147a(x)
|      :.-> 0x0800145a      3846           mov r0, r7                  ; flash_addr
|      ::   0x0800145c      2968           ldr r1, [r5]                ; *(uint32_t*)data
|      ::   0x0800145e      fff781fb       bl spi_transaction          ; flash_program_word(addr, word)
|      ::   ; Verify: compare written word with source
|      ::   0x08001462      2868           ldr r0, [r5]                ; source word
|      ::   0x08001464      3968           ldr r1, [r7]                ; flash word (read back)
|      ::   0x08001466      8842           cmp r0, r1
|     ,===< 0x08001468      02d0           beq 0x8001470               ; match → next word
|     |::   ; Verify failed → mark r8 = 0 (will return failure)
|     |::   0x0800146a      4ff00008       mov.w r8, 0
|    ,====< 0x0800146e      05e0           b 0x800147c                 ; bail out of write loop
|    ||::   ; CODE XREF from flash_from_spi @ 0x8001468(x)
|    |`---> 0x08001470      3f1d           adds r7, r7, 4              ; flash_addr += 4
|    | ::   0x08001472      2d1d           adds r5, r5, 4              ; data += 4
|    | ::   0x08001474      641c           adds r4, r4, 1              ; word_idx++
|    | ::   0x08001476      a4b2           uxth r4, r4
|    | ::   0x08001478      ff2c           cmp r4, 0xff                ; 255 (256 words = 1024 bytes)
|    | :`=< 0x0800147a      eed9           bls 0x800145a               ; next word
|    | :    ; CODE XREF from flash_from_spi @ 0x800146e(x)
|           ; === CLEANUP: wait for flash, re-enable interrupts ===
|    `----> 0x0800147c      fff766fb       bl spi_wait_busy            ; wait for flash controller
|      :    0x08001480      62b6           cpsie i                     ; re-enable interrupts
|      :    0x08001482      4046           mov r0, r8                  ; return success flag
\      `==< 0x08001484      9ae7           b 0x80013bc                 ; → pop and return

; CALL XREF from lcd_gpio_init @ 0x8001370(x)
/ 82: fcn.08001494 ();
| afv: vars(9:sp[0xc..0x1c])
|           0x08001494      10b5           push {r4, lr}
|           0x08001496      86b0           sub sp, 0x18
|           0x08001498      01a8           add r0, var_4h              ; int16_t arg1
|           0x0800149a      00f0c1fa       bl fcn.08001a20
|           0x0800149e      0020           movs r0, 0
|           0x080014a0      adf80400       strh.w r0, [var_4h]
|           0x080014a4      4ff48271       mov.w r1, 0x104
|           0x080014a8      adf80610       strh.w r1, [var_6h]
|           0x080014ac      adf80800       strh.w r0, [var_8h]
|           0x080014b0      0221           movs r1, 2
|           0x080014b2      adf80a10       strh.w r1, [var_ah]
|           0x080014b6      0121           movs r1, 1
|           0x080014b8      adf80c10       strh.w r1, [var_ch]
|           0x080014bc      4902           lsls r1, r1, 9
|           0x080014be      adf80e10       strh.w r1, [var_eh]
|           0x080014c2      1821           movs r1, 0x18
|           0x080014c4      adf81010       strh.w r1, [var_10h]
|           0x080014c8      adf81200       strh.w r0, [var_12h]
|           0x080014cc      adf81400       strh.w r0, [var_14h]
|           0x080014d0      054c           ldr r4, [0x080014e8]        ; [0x80014e8:4]=0x40003800
|           0x080014d2      01a9           add r1, var_4h              ; int16_t arg2
|           0x080014d4      2046           mov r0, r4                  ; int16_t arg1
|           0x080014d6      00f0c6fa       bl fcn.08001a66
|           0x080014da      0121           movs r1, 1                  ; uint32_t arg2
|           0x080014dc      2046           mov r0, r4                  ; int16_t arg1
|           0x080014de      00f0abfa       bl fcn.08001a38
|           0x080014e2      06b0           add sp, 0x18
\           0x080014e4      10bd           pop {r4, pc}

; XREFS: CALL 0x08000862  CALL 0x08000872  CALL 0x08000894  CALL 0x080008a4  CALL 0x080008c6  
            ; XREFS: CALL 0x080008d6  CALL 0x080008f8  CALL 0x08000916  
/ 156: flash_program (int16_t arg1, int16_t arg2, int16_t arg3, int16_t arg4, int16_t arg_b0h);
| `- args(r0, r1, r2, r3) vars(4:sp[0xa4..0xb0])
|           0x080014ec      2de9f04f       push.w {r4, r5, r6, r7, r8, sb, sl, fp, lr}
|           0x080014f0      a3b0           sub sp, 0x8c
|           0x080014f2      8346           mov fp, r0                  ; arg1
|           0x080014f4      dde92c98       ldrd sb, r8, [arg_b0h]
|           0x080014f8      0d46           mov r5, r1                  ; arg2
|           0x080014fa      1646           mov r6, r2                  ; arg3
|           0x080014fc      1f46           mov r7, r3                  ; arg4
|           0x080014fe      8021           movs r1, 0x80
|           0x08001500      03a8           add r0, var_ch
|           0x08001502      fef75efe       bl fcn.080001c2
|           0x08001506      0024           movs r4, 0
|           0x08001508      0df10c0a       add.w sl, var_ch
|           ; CODE XREFS from flash_program @ 0x800153a(x), 0x8001580(x)
|      ..-> 0x0800150c      305d           ldrb r0, [r6, r4]
|      ::   0x0800150e      0028           cmp r0, 0
|     ,===< 0x08001510      37d0           beq 0x8001582
|     |::   0x08001512      a128           cmp r0, 0xa1                ; 161
|    ,====< 0x08001514      12d3           blo 0x800153c
|    ||::   0x08001516      3019           adds r0, r6, r4             ; int16_t arg1
|    ||::   0x08001518      03a9           add r1, var_ch
|    ||::   0x0800151a      00f08afe       bl flash_erase_page
|    ||::   0x0800151e      cde900a7       strd sl, r7, [sp]
|    ||::   0x08001522      1823           movs r3, 0x18
|    ||::   0x08001524      1922           movs r2, 0x19
|    ||::   0x08001526      2946           mov r1, r5
|    ||::   0x08001528      5846           mov r0, fp
|    ||::   0x0800152a      cdf80890       str.w sb, [var_8h]
|    ||::   0x0800152e      00f013f9       bl lcd_draw_text
|    ||::   0x08001532      1935           adds r5, 0x19
|    ||::   0x08001534      adb2           uxth r5, r5
|    ||::   0x08001536      a41c           adds r4, r4, 2
|    ||::   0x08001538      e4b2           uxtb r4, r4
|    ||`==< 0x0800153a      e7e7           b 0x800150c
|    || :   ; CODE XREF from flash_program @ 0x8001514(x)
|    `----> 0x0800153c      2038           subs r0, 0x20
|     | :   0x0800153e      5e28           cmp r0, 0x5e                ; 94
|     |,==< 0x08001540      1cd8           bhi 0x800157c
|     ||:   0x08001542      3019           adds r0, r6, r4             ; int16_t arg1
|     ||:   0x08001544      03a9           add r1, var_ch
|     ||:   0x08001546      00f064fe       bl flash_clear_flags
|     ||:   0x0800154a      cde900a7       strd sl, r7, [sp]
|     ||:   0x0800154e      1823           movs r3, 0x18
|     ||:   0x08001550      0d22           movs r2, 0xd
|     ||:   0x08001552      2946           mov r1, r5
|     ||:   0x08001554      5846           mov r0, fp
|     ||:   0x08001556      cdf80890       str.w sb, [var_8h]
|     ||:   0x0800155a      00f0fdf8       bl lcd_draw_text
|     ||:   0x0800155e      0d35           adds r5, 0xd
|     ||:   0x08001560      adb2           uxth r5, r5
|     ||:   0x08001562      b8f1000f       cmp.w r8, 0
|    ,====< 0x08001566      09d0           beq 0x800157c
|    |||:   0x08001568      1823           movs r3, 0x18               ; int16_t arg4
|    |||:   0x0800156a      4246           mov r2, r8                  ; int16_t arg3
|    |||:   0x0800156c      2946           mov r1, r5                  ; int16_t arg2
|    |||:   0x0800156e      5846           mov r0, fp                  ; int16_t arg1
|    |||:   0x08001570      0097           str r7, [sp]
|    |||:   0x08001572      00f0d5fe       bl app_validate_jump
|    |||:   0x08001576      05eb0800       add.w r0, r5, r8
|    |||:   0x0800157a      85b2           uxth r5, r0
|    |||:   ; CODE XREFS from flash_program @ 0x8001540(x), 0x8001566(x)
|    `-`--> 0x0800157c      641c           adds r4, r4, 1
|     | :   0x0800157e      e4b2           uxtb r4, r4
|     | `=< 0x08001580      c4e7           b 0x800150c
|     |     ; CODE XREF from flash_program @ 0x8001510(x)
|     `---> 0x08001582      23b0           add sp, 0x8c
\           0x08001584      bde8f08f       pop.w {r4, r5, r6, r7, r8, sb, sl, fp, pc}

::   ; CALL XREFS from check_update_button @ 0x80004ba(x), 0x800053a(x), 0x80005c6(x)
       ::   ; CALL XREF from check_spi_model @ 0x8000684(x)
/ 478: flash_erase_sector ();
| afv: vars(1:sp[0x8..0x8])
|      ::   0x08001588      10b5           push {r4, lr}
|      ::   0x0800158a      724c           ldr r4, [0x08001754]        ; [0x8001754:4]=0x40011400
|      ::   0x0800158c      0421           movs r1, 4
|      ::   0x0800158e      2046           mov r0, r4
|      ::   0x08001590      fff709fe       bl fcn.080011a6
|      ::   0x08001594      0120           movs r0, 1
|      ::   0x08001596      fff72af9       bl gpio_read_pin
|      ::   0x0800159a      0421           movs r1, 4
|      ::   0x0800159c      2046           mov r0, r4
|      ::   0x0800159e      fff700fe       bl fcn.080011a2
|      ::   0x080015a2      0120           movs r0, 1
|      ::   0x080015a4      fff723f9       bl gpio_read_pin
|      ::   0x080015a8      0421           movs r1, 4
|      ::   0x080015aa      2046           mov r0, r4
|      ::   0x080015ac      fff7fbfd       bl fcn.080011a6
|      ::   0x080015b0      7820           movs r0, 0x78               ; 'x'
|      ::   0x080015b2      fff71cf9       bl gpio_read_pin
|      ::   0x080015b6      1120           movs r0, 0x11               ; int16_t arg1
|      ::   0x080015b8      00f0d6fe       bl model_xor_decode
|      ::   0x080015bc      7820           movs r0, 0x78               ; 'x'
|      ::   0x080015be      fff716f9       bl gpio_read_pin
|      ::   0x080015c2      b220           movs r0, 0xb2               ; int16_t arg1
|      ::   0x080015c4      00f0d0fe       bl model_xor_decode
|      ::   0x080015c8      0520           movs r0, 5                  ; int16_t arg1
|      ::   0x080015ca      00f0fdfe       bl model_xor_encode
|      ::   0x080015ce      0520           movs r0, 5                  ; int16_t arg1
|      ::   0x080015d0      00f0fafe       bl model_xor_encode
|      ::   0x080015d4      0020           movs r0, 0                  ; int16_t arg1
|      ::   0x080015d6      00f0f7fe       bl model_xor_encode
|      ::   0x080015da      3320           movs r0, 0x33               ; '3' ; int16_t arg1
|      ::   0x080015dc      00f0f4fe       bl model_xor_encode
|      ::   0x080015e0      3320           movs r0, 0x33               ; '3' ; int16_t arg1
|      ::   0x080015e2      00f0f1fe       bl model_xor_encode
|      ::   0x080015e6      b720           movs r0, 0xb7               ; int16_t arg1
|      ::   0x080015e8      00f0befe       bl model_xor_decode
|      ::   0x080015ec      3520           movs r0, 0x35               ; '5' ; int16_t arg1
|      ::   0x080015ee      00f0ebfe       bl model_xor_encode
|      ::   0x080015f2      c020           movs r0, 0xc0               ; int16_t arg1
|      ::   0x080015f4      00f0b8fe       bl model_xor_decode
|      ::   0x080015f8      2c20           movs r0, 0x2c               ; ',' ; int16_t arg1
|      ::   0x080015fa      00f0e5fe       bl model_xor_encode
|      ::   0x080015fe      c220           movs r0, 0xc2               ; int16_t arg1
|      ::   0x08001600      00f0b2fe       bl model_xor_decode
|      ::   0x08001604      0120           movs r0, 1                  ; int16_t arg1
|      ::   0x08001606      00f0dffe       bl model_xor_encode
|      ::   0x0800160a      c320           movs r0, 0xc3               ; int16_t arg1
|      ::   0x0800160c      00f0acfe       bl model_xor_decode
|      ::   0x08001610      0f20           movs r0, 0xf                ; int16_t arg1
|      ::   0x08001612      00f0d9fe       bl model_xor_encode
|      ::   0x08001616      c420           movs r0, 0xc4               ; int16_t arg1
|      ::   0x08001618      00f0a6fe       bl model_xor_decode
|      ::   0x0800161c      2020           movs r0, 0x20               ; int16_t arg1
|      ::   0x0800161e      00f0d3fe       bl model_xor_encode
|      ::   0x08001622      c620           movs r0, 0xc6               ; int16_t arg1
|      ::   0x08001624      00f0a0fe       bl model_xor_decode
|      ::   0x08001628      1120           movs r0, 0x11               ; int16_t arg1
|      ::   0x0800162a      00f0cdfe       bl model_xor_encode
|      ::   0x0800162e      d020           movs r0, 0xd0               ; int16_t arg1
|      ::   0x08001630      00f09afe       bl model_xor_decode
|      ::   0x08001634      a420           movs r0, 0xa4               ; int16_t arg1
|      ::   0x08001636      00f0c7fe       bl model_xor_encode
|      ::   0x0800163a      a120           movs r0, 0xa1               ; int16_t arg1
|      ::   0x0800163c      00f0c4fe       bl model_xor_encode
|      ::   0x08001640      e820           movs r0, 0xe8               ; int16_t arg1
|      ::   0x08001642      00f091fe       bl model_xor_decode
|      ::   0x08001646      0320           movs r0, 3                  ; int16_t arg1
|      ::   0x08001648      00f0befe       bl model_xor_encode
|      ::   0x0800164c      e920           movs r0, 0xe9               ; int16_t arg1
|      ::   0x0800164e      00f08bfe       bl model_xor_decode
|      ::   0x08001652      0920           movs r0, 9                  ; int16_t arg1
|      ::   0x08001654      00f0b8fe       bl model_xor_encode
|      ::   0x08001658      0920           movs r0, 9                  ; int16_t arg1
|      ::   0x0800165a      00f0b5fe       bl model_xor_encode
|      ::   0x0800165e      0820           movs r0, 8                  ; int16_t arg1
|      ::   0x08001660      00f0b2fe       bl model_xor_encode
|      ::   0x08001664      bb20           movs r0, 0xbb               ; int16_t arg1
|      ::   0x08001666      00f07ffe       bl model_xor_decode
|      ::   0x0800166a      3f20           movs r0, 0x3f               ; '?' ; int16_t arg1
|      ::   0x0800166c      00f0acfe       bl model_xor_encode
|      ::   0x08001670      e020           movs r0, 0xe0               ; int16_t arg1
|      ::   0x08001672      00f079fe       bl model_xor_decode
|      ::   0x08001676      d020           movs r0, 0xd0               ; int16_t arg1
|      ::   0x08001678      00f0a6fe       bl model_xor_encode
|      ::   0x0800167c      0520           movs r0, 5                  ; int16_t arg1
|      ::   0x0800167e      00f0a3fe       bl model_xor_encode
|      ::   0x08001682      0920           movs r0, 9                  ; int16_t arg1
|      ::   0x08001684      00f0a0fe       bl model_xor_encode
|      ::   0x08001688      0920           movs r0, 9                  ; int16_t arg1
|      ::   0x0800168a      00f09dfe       bl model_xor_encode
|      ::   0x0800168e      0820           movs r0, 8                  ; int16_t arg1
|      ::   0x08001690      00f09afe       bl model_xor_encode
|      ::   0x08001694      1420           movs r0, 0x14               ; int16_t arg1
|      ::   0x08001696      00f097fe       bl model_xor_encode
|      ::   0x0800169a      2820           movs r0, 0x28               ; '(' ; int16_t arg1
|      ::   0x0800169c      00f094fe       bl model_xor_encode
|      ::   0x080016a0      3320           movs r0, 0x33               ; '3' ; int16_t arg1
|      ::   0x080016a2      00f091fe       bl model_xor_encode
|      ::   0x080016a6      3f20           movs r0, 0x3f               ; '?' ; int16_t arg1
|      ::   0x080016a8      00f08efe       bl model_xor_encode
|      ::   0x080016ac      0720           movs r0, 7                  ; int16_t arg1
|      ::   0x080016ae      00f08bfe       bl model_xor_encode
|      ::   0x080016b2      1320           movs r0, 0x13               ; int16_t arg1
|      ::   0x080016b4      00f088fe       bl model_xor_encode
|      ::   0x080016b8      1420           movs r0, 0x14               ; int16_t arg1
|      ::   0x080016ba      00f085fe       bl model_xor_encode
|      ::   0x080016be      2820           movs r0, 0x28               ; '(' ; int16_t arg1
|      ::   0x080016c0      00f082fe       bl model_xor_encode
|      ::   0x080016c4      3020           movs r0, 0x30               ; '0' ; int16_t arg1
|      ::   0x080016c6      00f07ffe       bl model_xor_encode
|      ::   0x080016ca      e120           movs r0, 0xe1               ; int16_t arg1
|      ::   0x080016cc      00f04cfe       bl model_xor_decode
|      ::   0x080016d0      d020           movs r0, 0xd0               ; int16_t arg1
|      ::   0x080016d2      00f079fe       bl model_xor_encode
|      ::   0x080016d6      0520           movs r0, 5                  ; int16_t arg1
|      ::   0x080016d8      00f076fe       bl model_xor_encode
|      ::   0x080016dc      0920           movs r0, 9                  ; int16_t arg1
|      ::   0x080016de      00f073fe       bl model_xor_encode
|      ::   0x080016e2      0920           movs r0, 9                  ; int16_t arg1
|      ::   0x080016e4      00f070fe       bl model_xor_encode
|      ::   0x080016e8      0820           movs r0, 8                  ; int16_t arg1
|      ::   0x080016ea      00f06dfe       bl model_xor_encode
|      ::   0x080016ee      0320           movs r0, 3                  ; int16_t arg1
|      ::   0x080016f0      00f06afe       bl model_xor_encode
|      ::   0x080016f4      2420           movs r0, 0x24               ; '$' ; int16_t arg1
|      ::   0x080016f6      00f067fe       bl model_xor_encode
|      ::   0x080016fa      3220           movs r0, 0x32               ; '2' ; int16_t arg1
|      ::   0x080016fc      00f064fe       bl model_xor_encode
|      ::   0x08001700      3220           movs r0, 0x32               ; '2' ; int16_t arg1
|      ::   0x08001702      00f061fe       bl model_xor_encode
|      ::   0x08001706      3b20           movs r0, 0x3b               ; ';' ; int16_t arg1
|      ::   0x08001708      00f05efe       bl model_xor_encode
|      ::   0x0800170c      1420           movs r0, 0x14               ; int16_t arg1
|      ::   0x0800170e      00f05bfe       bl model_xor_encode
|      ::   0x08001712      1320           movs r0, 0x13               ; int16_t arg1
|      ::   0x08001714      00f058fe       bl model_xor_encode
|      ::   0x08001718      2820           movs r0, 0x28               ; '(' ; int16_t arg1
|      ::   0x0800171a      00f055fe       bl model_xor_encode
|      ::   0x0800171e      2f20           movs r0, 0x2f               ; '/' ; int16_t arg1
|      ::   0x08001720      00f052fe       bl model_xor_encode
|      ::   0x08001724      3620           movs r0, 0x36               ; '6' ; int16_t arg1
|      ::   0x08001726      00f01ffe       bl model_xor_decode
|      ::   0x0800172a      c020           movs r0, 0xc0               ; int16_t arg1
|      ::   0x0800172c      00f04cfe       bl model_xor_encode
|      ::   0x08001730      3a20           movs r0, 0x3a               ; ':' ; int16_t arg1
|      ::   0x08001732      00f019fe       bl model_xor_decode
|      ::   0x08001736      0520           movs r0, 5                  ; int16_t arg1
|      ::   0x08001738      00f046fe       bl model_xor_encode
|      ::   0x0800173c      2120           movs r0, 0x21               ; '!' ; int16_t arg1
|      ::   0x0800173e      00f013fe       bl model_xor_decode
|      ::   0x08001742      2920           movs r0, 0x29               ; ')' ; int16_t arg1
|      ::   0x08001744      00f010fe       bl model_xor_decode
|      ::   0x08001748      bde81040       pop.w {r4, lr}
|      ::   0x0800174c      0020           movs r0, 0
|     ,===< 0x0800174e      00f0ddbd       b.w 0x800230c
..
      |::   ; DATA XREF from flash_erase_sector @ 0x800158a(r)
      |::   ; CALL XREF from lcd_show_status @ 0x8000834(x)
      |::   ; CALL XREFS from flash_program @ 0x800152e(x), 0x800155a(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x800177a(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x80017d0(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x80017c2(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x80017ae(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x80017b6(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x80017a2(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x80017da(x)
| |||||::   ; CODE XREF from lcd_draw_text @ 0x800179e(x)
|  ||||::   ; CODE XREFS from lcd_draw_text @ 0x800176c(x), 0x8001770(x), 0x8001774(x)
|     |::   ; CODE XREF from lcd_draw_text @ 0x80017c8(x)
      |::   ; CALL XREF from lcd_gpio_init @ 0x80003fe(x)
      |::   ; CALL XREF from fcn.080017e0 @ 0x8001802(x)
|    ||::   ; CODE XREF from lcd_fill_rect @ 0x8001814(x)
      |::   ; DATA XREF from lcd_fill_rect @ 0x8001816(r)
      |::   ; CALL XREF from fcn.080017e0 @ 0x80017e6(x)
      |::   ; DATA XREF from lcd_draw_rect @ 0x800186c(r)
      |::   ; DATA XREF from lcd_draw_rect @ 0x8001870(r)
      |::   ; CALL XREF from main @ 0x80022a8(x)
      |::   ; DATA XREF from gpio_init @ 0x8001880(r)
      |::   ; DATA XREF from gpio_init @ 0x8001886(r)
      |::   ; CALL XREF from uart_update_mode @ 0x80003a0(x)
      |::   ; CALL XREF from check_update_button @ 0x800055e(x)
      |::   ; CALL XREF from uart_rx_handler @ 0x80006de(x)
|     |::   ; CODE XREF from systick_delay @ 0x80018ac(x)
      |::   ; DATA XREF from systick_delay @ 0x8001898(r)
      |::   ; DATA XREF from systick_delay @ 0x800189c(r)
      |::   ; CALL XREF from lcd_gpio_init @ 0x80011e8(x)
      |::   ; CALL XREF from uart_init @ 0x8002154(x)
|    ||::   ; CODE XREF from systick_init @ 0x80018c0(x)
      |::   ; DATA XREF from systick_init @ 0x80018bc(r)
      |::   ; CALL XREF from lcd_gpio_init @ 0x80011e0(x)
|    ||::   ; CODE XREF from systick_deinit @ 0x80018dc(x)
      |::   ; DATA XREF from systick_deinit @ 0x80018d8(r)
      |::   ; CALL XREF from flash_status_get @ 0x8001fe4(x)
|  ||||::   ; CODE XREFS from spi_init @ 0x8001922(x), 0x8001926(x), 0x8001984(x), 0x800198c(x), 0x8001990(x)
| :||||::   ; CODE XREF from spi_init @ 0x8001902(x)
| :|| |::   ; CODE XREF from spi_init @ 0x8001906(x)
| :|  |::   ; CODE XREF from spi_init @ 0x800190a(x)
| :||||::   ; CODE XREFS from spi_init @ 0x8001938(x), 0x8001940(x)
| :|  |::   ; CODE XREF from spi_init @ 0x8001944(x)
| :||||::   ; CODE XREF from spi_init @ 0x8001956(x)
| :||||::   ; CODE XREF from spi_init @ 0x800195c(x)
| :| ||::   ; CODE XREFS from spi_init @ 0x8001970(x), 0x8001978(x)
|     |::   ; CODE XREF from spi_init @ 0x800191a(x)
|     |::   ; CODE XREF from spi_init @ 0x800191e(x)
|   |||::   ; CODE XREF from spi_init @ 0x80019a4(x)
|   | |::   ; CODE XREF from spi_init @ 0x80019a8(x)
|   |||::   ; CODE XREF from spi_init @ 0x80019bc(x)
|   | |::   ; CODE XREF from spi_init @ 0x80019c0(x)
|    ||::   ; CODE XREF from spi_init @ 0x80019d2(x)
      |::   ; DATA XREF from spi_init @ 0x80018f6(r)
      |::   ; DATA XREFS from spi_init @ 0x80018fe(r), 0x800197a(r)
      |::   ; DATA XREFS from spi_init @ 0x8001916(r), 0x80019d8(r)
      |::   ; DATA XREF from spi_init @ 0x8001930(r)
      |::   ; DATA XREF from spi_init @ 0x8001972(r)
      |::   ; DATA XREF from spi_init @ 0x8001986(r)
      |::   ; DATA XREF from spi_init @ +0x108(r)
      |::   ; CODE XREFS from clock_setup @ 0x8001b7e(r), 0x8001ba2(x)
|   |:|::   ; CODE XREF from fcn.08001a00 @ 0x8001a04(x)
     :|::   ; DATA XREF from fcn.08001a00 @ 0x8001a00(r)
     :|::   ; CALL XREF from fcn.08001494 @ 0x800149a(x)
     :|::   ; CALL XREF from fcn.08001494 @ 0x80014de(x)
|   |:|::   ; CODE XREF from fcn.08001a38 @ 0x8001a3a(x)
     :|::   ; CALL XREFS from lcd_spi_write_cmd @ 0x8001be4(x), 0x8001c06(x)
     :|::   ; CALL XREFS from lcd_fill_color @ 0x8001cf8(x), 0x8001d1a(x)
|   |:|::   ; CODE XREF from fcn.08001a50 @ 0x8001a58(x)
     :|::   ; CALL XREF from lcd_spi_write_cmd @ 0x8001c10(x)
     :|::   ; CALL XREF from lcd_fill_color @ 0x8001d02(x)
     :|::   ; CALL XREF from lcd_spi_write_cmd @ 0x8001bf0(x)
     :|::   ; CALL XREF from lcd_fill_color @ 0x8001cdc(x)
     :|::   ; CALL XREF from fcn.08001494 @ 0x80014d6(x)
|  ||:|::   ; CODE XREF from fcn.08001a66 @ 0x8001a72(x)
|  | :|::   ; CODE XREF from fcn.08001a66 @ 0x8001a7c(x)
     :|::   ; CALL XREF from lcd_draw_text @ 0x800178e(x)
     :|::   ; CALL XREF from app_validate_jump @ 0x8002342(x)
    |:|::   ; CALL XREF from system_init @ 0x8001ed4(x)
|   |:|::   ; CODE XREF from clock_setup @ 0x8001b24(x)
| | |:|::   ; CODE XREF from clock_setup @ 0x8001b1e(x)
| |||:|::   ; CODE XREF from clock_setup @ 0x8001b2a(x)
| | |:|::   ; CODE XREF from clock_setup @ 0x8001b30(x)
|  ||:|::   ; CODE XREF from clock_setup @ 0x8001b7a(x)
|  ||:|::   ; CODE XREF from clock_setup @ 0x8001b9a(x)
|  || |::   ; CODE XREF from clock_setup @ 0x8001b38(x)
    | |::   ; DATA XREF from clock_setup @ 0x8001b00(r)
    | |::   ; DATA XREF from clock_setup @ 0x8001b60(r)
    | |::   ; DATA XREF from clock_setup @ 0x8001b68(r)
    | |::   ; CALL XREF from lcd_init_st7789 @ 0x8001d9a(x)
|   |||::   ; CODE XREF from fcn.08001bb4 @ 0x8001bbe(x)
    | |::   ; CALL XREF from crc_ccitt_spi @ 0x8000450(x)
    | |::   ; CALL XREF from lcd_spi_write_data @ 0x8001c5c(x)
    | |::   ; CALL XREF from lcd_set_window @ 0x8001caa(x)
|   |||::   ; CODE XREF from lcd_spi_write_cmd @ 0x8001bea(x)
| |:|||::   ; CODE XREFS from lcd_spi_write_cmd @ 0x8001bd2(x), 0x8001bda(x)
|   |||::   ; CODE XREF from lcd_spi_write_cmd @ 0x8001c0c(x)
| |:|||::   ; CODE XREFS from lcd_spi_write_cmd @ 0x8001bf4(x), 0x8001bfc(x)
    | |::   ; DATA XREF from lcd_spi_write_cmd @ 0x8001bd0(r)
    | |::   ; CALL XREFS from check_update_button @ 0x80004dc(x), 0x800050a(x)
    | |::   ; CALL XREF from spi_cmd_read_status @ 0x80009ca(x)
    | |::   ; CODE XREF from flash_clear_flags @ 0x800222e(x)
    | |::   ; CODE XREF from flash_erase_page @ 0x800229a(x)
| |:|:|::   ; CODE XREF from lcd_spi_write_data @ 0x8001c6a(x)
| |:|:|::   ; CODE XREF from lcd_spi_write_data @ 0x8001c5a(x)
   :|:| :   ; DATA XREF from lcd_spi_write_data @ 0x8001c26(r)
   :|:| :   ; DATA XREF from clock_setup @ +0xbe(r)
   :|:| :   ; CALL XREF from fcn.08001bb4 @ 0x8001bb8(r)
| |:|:||:   ; CODE XREFS from lcd_set_window @ 0x8001cc4(x), 0x8001cc8(x)
| |:|:||:   ; CODE XREF from lcd_set_window @ 0x8001c88(x)
| |:|:| :   ; CODE XREF from lcd_set_window @ 0x8001c8c(x)
   :|:| :   ; DATA XREF from lcd_set_window @ 0x8001c90(r)
   :|:| :   ; XREFS: CALL 0x0800042a  CALL 0x08000430  CALL 0x08000436  CALL 0x0800043c  CALL 0x08001c3e  
   :|:| :   ; XREFS: CALL 0x08001c46  CALL 0x08001c4e  CALL 0x08001c54  CALL 0x08001ca6  CALL 0x08001d56  
   :|:| :   ; XREFS: CALL 0x08001d5e  CALL 0x08001d66  CALL 0x08001d6c  CALL 0x08001d76  CALL 0x08001e30  
|  :|:||:   ; CODE XREF from lcd_fill_color @ 0x8001cfe(x)
| ::|:||:   ; CODE XREFS from lcd_fill_color @ 0x8001d14(x), 0x8001d24(x)
| ::|:||:   ; CODE XREFS from lcd_fill_color @ 0x8001ce4(x), 0x8001cec(x)
|  :|:||:   ; CODE XREF from lcd_fill_color @ 0x8001d20(x)
| ::|:||:   ; CODE XREFS from lcd_fill_color @ 0x8001d08(x), 0x8001d10(x)
   :|:| :   ; DATA XREF from lcd_fill_color @ 0x8001cd6(r)
|  :|:| :   ; CODE XREFS from lcd_init_st7789 @ 0x8001dca(x), 0x8001de4(x), 0x8001dfc(x), 0x8001e12(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001d82(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001d72(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001da6(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001d92(x)
|  :|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001da0(x)
   :|:|::   ; DATA XREF from lcd_init_st7789 @ 0x8001d3e(r)
   :|:|::   ; CALL XREF from check_update_button @ 0x8000554(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001e04(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001dde(x)
| |:|:|::   ; CODE XREFS from lcd_init_st7789 @ 0x8001dd8(x), 0x8001df4(x)
| |:|:|::   ; CODE XREF from lcd_init_st7789 @ 0x8001dc2(x)
   :|:| :   ; CALL XREF from lcd_init_st7789 @ 0x8001d3a(x)
   :|:|     ; DATA XREF from lcd_backlight_on @ 0x8001e1a(r)
   :|:| |   ; CALL XREF from lcd_gpio_init @ 0x80003fa(x)
| |:|:|||   ; CODE XREF from systick_handler @ 0x8001e62(x)
| |:|:| |   ; CODE XREF from systick_handler @ 0x8001e66(x)
|  :|:|||   ; CODE XREF from systick_handler @ 0x8001e80(x)
   :|:| |   ; DATA XREF from systick_handler @ 0x8001e50(r)
   :|:| |   ; DATA XREF from systick_handler @ 0x8001e6e(r)
   :|:| |   ; DATA XREFS from system_init @ 0x8001e92(r), 0x8001ed8(r)
   :|:| |   ; DATA XREF from system_init @ 0x8001e9c(r)
   :|:| |   ; DATA XREF from system_init @ 0x8001ea8(r)
   :|:| |   ; DATA XREF from system_init @ 0x8001ec8(r)
   :|:|||   ; CALL XREFS from uart_update_mode @ 0x8000240(x), 0x800027c(x)
   :|:|||   ; CALL XREFS from check_update_button @ 0x8000574(x), 0x80005b2(x)
   :|:|||   ; DATA XREF from timer_reset @ 0x8001f08(r)
   :|:|||   ; CALL XREF from uart_init @ 0x80021ce(x)
| |:|:|||   ; CODE XREF from timer_delay_us @ 0x8001f1a(x)
   :|:|||   ; CALL XREF from flash_lock @ 0x800206e(x)
| |:|:|||   ; CODE XREFS from timer_delay_ms @ 0x8001f68(x), 0x8001f6e(x)
| |:|:|||   ; CODE XREFS from timer_delay_ms @ 0x8001f5a(x), 0x8001f5c(x)
| |:|:|||   ; CODE XREF from timer_delay_ms @ 0x8001f48(x)
|  :|:|||   ; CODE XREF from timer_delay_ms @ 0x8001f4c(x)
   :|:|||   ; CALL XREF from uart_init @ 0x80021c6(x)
| |:|:|||   ; CODE XREFS from delay_loop @ 0x8001f98(x), 0x8001f9c(x)
| |:|:|||   ; CODE XREF from delay_loop @ 0x8001f84(x)
|  :|:|||   ; CODE XREF from delay_loop @ 0x8001f88(x)
|  :|:|||   ; CODE XREF from delay_loop @ 0x8001f8c(x)
   :|:|||   ; CALL XREF from uart_init @ 0x80021ba(x)
| |:|:|||   ; CODE XREF from flash_status_get @ 0x8001fec(x)
|  :|:|||   ; CODE XREF from flash_status_get @ 0x8001ff0(x)
| |:|:|||   ; CODE XREF from flash_status_get @ 0x8001ff8(x)
|  :|:|||   ; CODE XREF from flash_status_get @ 0x800200a(x)
| |:|:|||   ; CODE XREF from flash_status_get @ 0x8002038(x)
|  :|:|||   ; CODE XREF from flash_status_get @ 0x8002048(x)
   :|:|||   ; DATA XREF from flash_status_get @ 0x8001fe8(r)
   :|:|||   ; CODE XREF from system_init @ +0x6c(x)
| |:|:|||   ; CODE XREFS from flash_lock @ 0x8002074(x), 0x8002090(x)
   :|:| |   ; DATA XREF from flash_lock @ 0x8002066(r)
   :|:| |   ; DATA XREF from flash_lock @ 0x8002084(r)
   :|:| |   ; DATA XREF from flash_lock @ +0x50(r)
   :|:| |   ; DATA XREF from flash_lock @ 0x8002088(r)
   :|:| |   ; CODE XREF from lcd_backlight_on @ +0x34(x)
   :|:| |   ; CODE XREF from flash_lock @ +0x6a(x)
  |:|:|||   ; CODE XREFS from flash_lock @ +0x7a(x), +0x82(x), +0x94(x)
   :|:|     ; DATA XREF from flash_lock @ +0x5c(r)
   :|:|     ; DATA XREF from flash_lock @ +0x64(r)
   :|:|     ; DATA XREF from flash_lock @ +0x72(r)
   :|:|     ; CALL XREF from uart_init @ 0x8002192(x)
|  :|:|     ; CODE XREF from send_response @ 0x80003ee(x)
|  :|:| |   ; CODE XREF from send_response @ 0x8002140(x)
|  :|:|:|   ; CODE XREF from send_response @ 0x800213a(x)
|  :|:|:|   ; CODE XREF from send_response @ 0x8002130(x)
   :|:|     ; DATA XREF from send_response @ 0x800212c(r)
   :|:|     ; CALL XREF from main @ 0x80022b0(x)
|  :|:|     ; DATA XREF from flash_program_word @ +0x34(r)
   :|:|     ; DATA XREF from uart_init @ 0x8002172(r)
   :|:|     ; DATA XREF from uart_init @ 0x80021b4(r)
   :|:|     ; XREFS: CALL 0x080017b2  CALL 0x080017ba  CALL 0x08001ad2  CALL 0x08001ad8  CALL 0x08001ae4  
   :|:|     ; XREFS: CODE 0x08001aee  DATA 0x080021dc  CALL 0x08002354  CALL 0x08002458  CALL 0x08002482  
   :|:|     ; XREFS: CALL 0x08002488  CALL 0x08002494  CODE 0x0800249e  
   : :| |   ; CALL XREFS from libc_init_array @ 0x8000206(r), 0x8000210(x)
   : :| |   ; NULL XREF from aav.0x08002ca2 @ +0x1e(r)
|  : :|||   ; CODE XREF from memcpy_init @ 0x80021fe(x)
|  :::|||   ; CODE XREF from memcpy_init @ 0x80021f4(x)
   : :| |   ;-- aav.0x08002204:
   : :| |   ; NULL XREF from aav.0x08002ca2 @ +0x2e(r)
   : :|||   ; CODE XREF from aav.0x08002204 @ +0xa(x)
   :::|||   ; CODE XREF from aav.0x08002204 @ +0x2(x)
   : :| |   ; CALL XREF from flash_program @ 0x8001546(x)
|  : :|||   ; CODE XREF from flash_clear_flags @ 0x800221e(x)
     :| |   ; CALL XREF from flash_program @ 0x800151a(x)
|   |:|||   ; CODE XREF from flash_erase_page @ 0x8002244(x)
| |||:|||   ; CODE XREFS from flash_erase_page @ 0x8002250(x), 0x8002254(x)
| |||:|||   ; CODE XREFS from flash_erase_page @ 0x8002248(x), 0x8002270(x), 0x8002276(x), 0x800227a(x)
      | |   ; CODE XREF from stack_init @ 0x8000186(x)
|     |||   ; CODE XREF from main @ 0x80022bc(x)
|  @|||||   ; CODE XREF from main @ 0x80022c6(x)
|   ||| |   ; CODE XREF from main @ 0x80022ce(x)
|   | | |   ; CODE XREF from main @ 0x80022d0(x)
|     |||   ; CODE XREF from main @ 0x80022e8(x)
      | |   ; DATA XREF from main @ 0x80022dc(r)
      | |   ; DATA XREF from main @ 0x80022e0(r)
      | |   ; DATA XREF from main @ 0x80022ee(r)
|     | |   ; CODE XREF from flash_erase_sector @ 0x800174e(x)
|     `---> 0x0800230c      08b5           push {r3, lr}               ; arg1
|       |   0x0800230e      0021           movs r1, 0                  ; int16_t arg2
|       |   0x08002310      0090           str r0, [sp]
|       |   0x08002312      4ff4a073       mov.w r3, 0x140             ; int16_t arg4
|       |   0x08002316      f022           movs r2, 0xf0               ; int16_t arg3
|       |   0x08002318      0846           mov r0, r1                  ; int16_t arg1
|       |   0x0800231a      00f088f8       bl fcn.0800242e
\       |   0x0800231e      08bd           pop {r3, pc}

; CALL XREF from lcd_show_status @ 0x8000834(x)
            ; CALL XREFS from flash_program @ 0x800152e(x), 0x800155a(x)
/ 132: lcd_draw_text (int16_t arg1, int16_t arg2, int16_t arg3, int16_t arg4, int16_t arg_28h, int16_t arg_30h);
| `- args(r0, r1, r2, r3, sp[0x0..0x8])
|           0x08001758      2de9f05f       push.w {r4, r5, r6, r7, r8, sb, sl, fp, ip, lr}
|           0x0800175c      0446           mov r4, r0                  ; arg1
|           0x0800175e      dde90aab       ldrd sl, fp, [arg_28h]
|           0x08001762      0846           mov r0, r1                  ; arg2
|           0x08001764      1646           mov r6, r2                  ; arg3
|           0x08001766      9946           mov sb, r3                  ; arg4
|           0x08001768      b9f1000f       cmp.w sb, 0
|       ,=< 0x0800176c      31d0           beq 0x80017d2
|       |   0x0800176e      002e           cmp r6, 0
|      ,==< 0x08001770      2fd0           beq 0x80017d2
|      ||   0x08001772      f028           cmp r0, 0xf0                ; 240
|     ,===< 0x08001774      2dd8           bhi 0x80017d2
|     |||   0x08001776      8119           adds r1, r0, r6
|     |||   0x08001778      f029           cmp r1, 0xf0                ; 240
|    ,====< 0x0800177a      02d9           bls 0x8001782
|    ||||   0x0800177c      c0f1f001       rsb.w r1, r0, 0xf0
|    ||||   0x08001780      8eb2           uxth r6, r1
|    ||||   ; CODE XREF from lcd_draw_text @ 0x800177a(x)
|    `----> 0x08001782      04eb0901       add.w r1, r4, sb
|     |||   0x08001786      8bb2           uxth r3, r1
|     |||   0x08001788      8119           adds r1, r0, r6             ; int16_t arg2
|     |||   0x0800178a      89b2           uxth r1, r1
|     |||   0x0800178c      2246           mov r2, r4                  ; int16_t arg3
|     |||   0x0800178e      00f094f9       bl svccall_handler
|     |||   0x08001792      0125           movs r5, 1
|     |||   0x08001794      4ff00008       mov.w r8, 0
|     |||   0x08001798      00f046fe       bl fcn.08002428
|     |||   0x0800179c      0027           movs r7, 0
|    ,====< 0x0800179e      16e0           b 0x80017ce
|    ||||   ; CODE XREF from lcd_draw_text @ 0x80017d0(x)
|   .-----> 0x080017a0      0024           movs r4, 0
|  ,======< 0x080017a2      0de0           b 0x80017c0
|  |:||||   ; CODE XREF from lcd_draw_text @ 0x80017c2(x)
| .-------> 0x080017a4      04eb0800       add.w r0, r4, r8
| :|:||||   0x080017a8      1af80000       ldrb.w r0, [sl, r0]
| :|:||||   0x080017ac      2842           tst r0, r5
| ========< 0x080017ae      03d0           beq 0x80017b8
| :|:||||   0x080017b0      0c98           ldr r0, [arg_30h]           ; int16_t arg1
| :|:||||   0x080017b2      00f015fd       bl fcn.080021e0
| ========< 0x080017b6      02e0           b 0x80017be
| :|:||||   ; CODE XREF from lcd_draw_text @ 0x80017ae(x)
| --------> 0x080017b8      5846           mov r0, fp                  ; int16_t arg1
| :|:||||   0x080017ba      00f011fd       bl fcn.080021e0
| :|:||||   ; CODE XREF from lcd_draw_text @ 0x80017b6(x)
| --------> 0x080017be      641c           adds r4, r4, 1
| :|:||||   ; CODE XREF from lcd_draw_text @ 0x80017a2(x)
| :`------> 0x080017c0      b442           cmp r4, r6
| `=======< 0x080017c2      efd3           blo 0x80017a4
|   :||||   0x080017c4      781c           adds r0, r7, 1
|   :||||   0x080017c6      4007           lsls r0, r0, 0x1d
|  ,======< 0x080017c8      05d0           beq 0x80017d6
|  |:||||   0x080017ca      6d00           lsls r5, r5, 1
|  |:||||   ; CODE XREF from lcd_draw_text @ 0x80017da(x)
| .-------> 0x080017cc      7f1c           adds r7, r7, 1
| :|:||||   ; CODE XREF from lcd_draw_text @ 0x800179e(x)
| :|:`----> 0x080017ce      4f45           cmp r7, sb
| :|`=====< 0x080017d0      e6d3           blo 0x80017a0
| :|  |||   ; CODE XREFS from lcd_draw_text @ 0x800176c(x), 0x8001770(x), 0x8001774(x)
| :|  ```-> 0x080017d2      bde8f09f       pop.w {r4, r5, r6, r7, r8, sb, sl, fp, ip, pc}
| :|        ; CODE XREF from lcd_draw_text @ 0x80017c8(x)
| :`------> 0x080017d6      0125           movs r5, 1
| :         0x080017d8      b044           add r8, r6
\ `=======< 0x080017da      f7e7           b 0x80017cc

; CALL XREF from lcd_gpio_init @ 0x80003fe(x)
/ 40: fcn.080017e0 ();
| afv: vars(4:sp[0x5..0x8])
|           0x080017e0      08b5           push {r3, lr}
|           0x080017e2      4ff4e060       mov.w r0, 0x700
|           0x080017e6      00f041f8       bl lcd_draw_rect
|           0x080017ea      3420           movs r0, 0x34               ; '4'
|           0x080017ec      8df80000       strb.w r0, [sp]
|           0x080017f0      0020           movs r0, 0
|           0x080017f2      8df80100       strb.w r0, [var_1h]
|           0x080017f6      8df80200       strb.w r0, [var_2h]
|           0x080017fa      0120           movs r0, 1
|           0x080017fc      8df80300       strb.w r0, [var_3h]
|           0x08001800      6846           mov r0, sp                  ; int16_t arg1
|           0x08001802      00f001f8       bl lcd_fill_rect
\           0x08001806      08bd           pop {r3, pc}

; CALL XREF from fcn.080017e0 @ 0x8001802(x)
/ 96: lcd_fill_rect (int16_t arg1);
| `- args(r0)
|           0x08001808      70b5           push {r4, r5, r6, lr}
|           0x0800180a      0f23           movs r3, 0xf
|           0x0800180c      c278           ldrb r2, [r0, 3]            ; arg1
|           0x0800180e      0126           movs r6, 1
|           0x08001810      0178           ldrb r1, [r0]               ; arg1
|           0x08001812      002a           cmp r2, 0
|       ,=< 0x08001814      1ed0           beq 0x8001854
|       |   0x08001816      144a           ldr r2, [0x08001868]        ; [0x8001868:4]=0xe000ed0c
|       |   0x08001818      1268           ldr r2, [r2]
|       |   0x0800181a      02f4e062       and r2, r2, 0x700
|       |   0x0800181e      c2f5e062       rsb.w r2, r2, 0x700
|       |   0x08001822      120a           lsrs r2, r2, 8
|       |   0x08001824      c2f10404       rsb.w r4, r2, 4
|       |   0x08001828      d340           lsrs r3, r2
|       |   0x0800182a      4578           ldrb r5, [r0, 1]            ; arg1
|       |   0x0800182c      a540           lsls r5, r4
|       |   0x0800182e      8278           ldrb r2, [r0, 2]            ; arg1
|       |   0x08001830      1a40           ands r2, r3
|       |   0x08001832      2a43           orrs r2, r5
|       |   0x08001834      1201           lsls r2, r2, 4
|       |   0x08001836      01f1e021       add.w r1, r1, -0x1fff2000
|       |   0x0800183a      81f80024       strb.w r2, [r1, 0x400]
|       |   0x0800183e      0078           ldrb r0, [r0]               ; arg1
|       |   0x08001840      00f01f01       and r1, r0, 0x1f            ; arg1
|       |   0x08001844      8e40           lsls r6, r1
|       |   0x08001846      4009           lsrs r0, r0, 5              ; arg1
|       |   0x08001848      8000           lsls r0, r0, 2              ; arg1
|       |   0x0800184a      00f1e020       add.w r0, r0, -0x1fff2000   ; arg1
|       |   0x0800184e      c0f80061       str.w r6, [r0, 0x100]       ; arg1
|       |   0x08001852      70bd           pop {r4, r5, r6, pc}
|       |   ; CODE XREF from lcd_fill_rect @ 0x8001814(x)
|       `-> 0x08001854      01f01f00       and r0, r1, 0x1f
|           0x08001858      8640           lsls r6, r0
|           0x0800185a      4809           lsrs r0, r1, 5
|           0x0800185c      8000           lsls r0, r0, 2
|           0x0800185e      00f1e020       add.w r0, r0, -0x1fff2000
|           0x08001862      c0f88061       str.w r6, [r0, 0x180]
\           0x08001866      70bd           pop {r4, r5, r6, pc}

; CALL XREF from fcn.080017e0 @ 0x80017e6(x)
/ 10: lcd_draw_rect ();
|           0x0800186c      0249           ldr r1, [0x08001878]        ; [0x8001878:4]=0x5fa0000
|           0x0800186e      0843           orrs r0, r1
|           0x08001870      0249           ldr r1, [0x0800187c]        ; [0x800187c:4]=0xe000ed0c
|           0x08001872      0860           str r0, [r1]
\           0x08001874      7047           bx lr

; CALL XREF from main @ 0x80022a8(x)
/ 12: gpio_init (int16_t arg1);
| `- args(r0)
|           0x08001880      024a           ldr r2, [0x0800188c]        ; [0x800188c:4]=0x1fffff80
|           0x08001882      1140           ands r1, r2
|           0x08001884      0143           orrs r1, r0                 ; arg1
|           0x08001886      0248           ldr r0, [0x08001890]        ; [0x8001890:4]=0xe000ed08
|           0x08001888      0160           str r1, [r0]
\           0x0800188a      7047           bx lr

; CALL XREF from uart_update_mode @ 0x80003a0(x)
            ; CALL XREF from check_update_button @ 0x800055e(x)
            ; CALL XREF from uart_rx_handler @ 0x80006de(x)
; --------------------------------------------------------------------------
; systick_delay: Trigger system reset via AIRCR register.
; Despite the name, this is actually a SYSTEM RESET function.
;
; C pseudocode:
;   void systick_delay(void) {
;       __DSB();
;       SCB->AIRCR = (SCB->AIRCR & 0x700) | 0x05FA0004;  // SYSRESETREQ
;       __DSB();
;       while (1) __NOP();  // wait for reset
;   }
;
; 0xE000ED0C = SCB->AIRCR (Application Interrupt and Reset Control)
; 0x05FA0004 = VECTKEY (0x05FA) + SYSRESETREQ bit (bit 2)
; --------------------------------------------------------------------------
/ 26: systick_delay ();
|           0x08001894      bff34f8f       dsb sy
|           0x08001898      0548           ldr r0, [0x080018b0]        ; [0x80018b0:4]=0xe000ed0c
|           0x0800189a      0168           ldr r1, [r0]
|           0x0800189c      054a           ldr r2, [0x080018b4]        ; [0x80018b4:4]=0x5fa0004
|           0x0800189e      01f4e061       and r1, r1, 0x700
|           0x080018a2      1143           orrs r1, r2
|           0x080018a4      0160           str r1, [r0]
|           0x080018a6      bff34f8f       dsb sy
|           ; CODE XREF from systick_delay @ 0x80018ac(x)
|       .-> 0x080018aa      00bf           nop
\       `=< 0x080018ac      fde7           b 0x80018aa

; CALL XREF from lcd_gpio_init @ 0x80011e8(x)
            ; CALL XREF from uart_init @ 0x8002154(x)
/ 22: systick_init (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x080018bc      054a           ldr r2, [0x080018d4]        ; [0x80018d4:4]=0x40021000
|           0x080018be      0029           cmp r1, 0                   ; arg2
|       ,=< 0x080018c0      03d0           beq 0x80018ca
|       |   0x080018c2      d169           ldr r1, [r2, 0x1c]
|       |   0x080018c4      0143           orrs r1, r0                 ; arg1
|       |   0x080018c6      d161           str r1, [r2, 0x1c]
|       |   0x080018c8      7047           bx lr
|       |   ; CODE XREF from systick_init @ 0x80018c0(x)
|       `-> 0x080018ca      d169           ldr r1, [r2, 0x1c]
|           0x080018cc      8143           bics r1, r0                 ; arg1
|           0x080018ce      d161           str r1, [r2, 0x1c]
\           0x080018d0      7047           bx lr

; CALL XREF from lcd_gpio_init @ 0x80011e0(x)
/ 22: systick_deinit (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x080018d8      054a           ldr r2, [0x080018f0]        ; [0x80018f0:4]=0x40021000
|           0x080018da      0029           cmp r1, 0                   ; arg2
|       ,=< 0x080018dc      03d0           beq 0x80018e6
|       |   0x080018de      9169           ldr r1, [r2, 0x18]
|       |   0x080018e0      0143           orrs r1, r0                 ; arg1
|       |   0x080018e2      9161           str r1, [r2, 0x18]
|       |   0x080018e4      7047           bx lr
|       |   ; CODE XREF from systick_deinit @ 0x80018dc(x)
|       `-> 0x080018e6      9169           ldr r1, [r2, 0x18]
|           0x080018e8      8143           bics r1, r0                 ; arg1
|           0x080018ea      9161           str r1, [r2, 0x18]
\           0x080018ec      7047           bx lr

; CALL XREF from flash_status_get @ 0x8001fe4(x)
/ 242: spi_init (int16_t arg1);
| `- args(r0)
|           0x080018f4      70b5           push {r4, r5, r6, lr}
|           0x080018f6      3c4c           ldr r4, [0x080019e8]        ; [0x80019e8:4]=0x40021000
|           0x080018f8      6168           ldr r1, [r4, 4]
|           0x080018fa      01f00c01       and r1, r1, 0xc
|           0x080018fe      3b4b           ldr r3, [0x080019ec]        ; [0x80019ec:4]=0x7a1200
|           0x08001900      0029           cmp r1, 0
|       ,=< 0x08001902      0dd0           beq 0x8001920
|       |   0x08001904      0429           cmp r1, 4                   ; 4
|      ,==< 0x08001906      0dd0           beq 0x8001924
|      ||   0x08001908      0829           cmp r1, 8                   ; 8
|     ,===< 0x0800190a      0dd0           beq 0x8001928
|     |||   0x0800190c      0360           str r3, [r0]                ; arg1
|     |||   ; CODE XREFS from spi_init @ 0x8001922(x), 0x8001926(x), 0x8001984(x), 0x800198c(x), 0x8001990(x)
| ....----> 0x0800190e      6168           ldr r1, [r4, 4]
| ::::|||   0x08001910      c1f30211       ubfx r1, r1, 4, 3
| ::::|||   0x08001914      6268           ldr r2, [r4, 4]
| ::::|||   0x08001916      364b           ldr r3, aav.0x08002ca2      ; [0x8002ca2:4]=0x4030201
| ::::|||   0x08001918      1206           lsls r2, r2, 0x18
| ========< 0x0800191a      3ad5           bpl 0x8001992
| ::::|||   0x0800191c      595c           ldrb r1, [r3, r1]
| ========< 0x0800191e      39e0           b 0x8001994
| ::::|||   ; CODE XREF from spi_init @ 0x8001902(x)
| ::::||`-> 0x08001920      0360           str r3, [r0]                ; arg1
| ========< 0x08001922      f4e7           b 0x800190e
| ::::||    ; CODE XREF from spi_init @ 0x8001906(x)
| ::::|`--> 0x08001924      0360           str r3, [r0]                ; arg1
| `=======< 0x08001926      f2e7           b 0x800190e
|  :::|     ; CODE XREF from spi_init @ 0x800190a(x)
|  :::`---> 0x08001928      6168           ldr r1, [r4, 4]
|  :::      0x0800192a      01f48035       and r5, r1, 0x10000
|  :::      0x0800192e      6168           ldr r1, [r4, 4]
|  :::      0x08001930      304a           ldr r2, [0x080019f4]        ; [0x80019f4:4]=0x603c0000
|  :::      0x08001932      1140           ands r1, r2
|  :::      0x08001934      11f0c04f       tst.w r1, 0x60000000
|  :::  ,=< 0x08001938      05d1           bne 0x8001946
|  :::  |   0x0800193a      c1f38342       ubfx r2, r1, 0x12, 4
|  :::  |   0x0800193e      0f2a           cmp r2, 0xf                 ; 15
|  ::: ,==< 0x08001940      01d0           beq 0x8001946
|  ::: ||   0x08001942      0222           movs r2, 2
|  :::,===< 0x08001944      00e0           b 0x8001948
|  :::|||   ; CODE XREFS from spi_init @ 0x8001938(x), 0x8001940(x)
|  :::|``-> 0x08001946      0122           movs r2, 1
|  :::|     ; CODE XREF from spi_init @ 0x8001944(x)
|  :::`---> 0x08001948      01f0c046       and r6, r1, 0x60000000
|  :::      0x0800194c      c1f38341       ubfx r1, r1, 0x12, 4
|  :::      0x08001950      41ea5661       orr.w r1, r1, r6, lsr 25
|  :::      0x08001954      1144           add r1, r2
|  :::  ,=< 0x08001956      65b1           cbz r5, 0x8001972
|  :::  |   0x08001958      6268           ldr r2, [r4, 4]
|  :::  |   0x0800195a      9203           lsls r2, r2, 0xe
|  ::: ,==< 0x0800195c      0dd5           bpl 0x800197a
|  ::: ||   0x0800195e      626d           ldr r2, [r4, 0x54]
|  ::: ||   0x08001960      02f44052       and r2, r2, 0x3000
|  ::: ||   0x08001964      120b           lsrs r2, r2, 0xc
|  ::: ||   0x08001966      921c           adds r2, r2, 2
|  ::: ||   0x08001968      b3fbf2f2       udiv r2, r3, r2
|  ::: ||   0x0800196c      4a43           muls r2, r1, r2
|  ::: ||   0x0800196e      0260           str r2, [r0]                ; arg1
|  :::,===< 0x08001970      06e0           b 0x8001980
|  :::|||   ; CODE XREF from spi_init @ 0x8001956(x)
|  :::||`-> 0x08001972      214a           ldr r2, [0x080019f8]        ; [0x80019f8:4]=0x3d0900
|  :::||    0x08001974      5143           muls r1, r2, r1
|  :::||    0x08001976      0160           str r1, [r0]                ; arg1
|  :::||,=< 0x08001978      02e0           b 0x8001980
|  :::|||   ; CODE XREF from spi_init @ 0x800195c(x)
|  :::|`--> 0x0800197a      1c4a           ldr r2, [0x080019ec]        ; [0x80019ec:4]=0x7a1200
|  :::| |   0x0800197c      5143           muls r1, r2, r1
|  :::| |   0x0800197e      0160           str r1, [r0]                ; arg1
|  :::| |   ; CODE XREFS from spi_init @ 0x8001970(x), 0x8001978(x)
|  :::`-`-> 0x08001980      6168           ldr r1, [r4, 4]
|  :::      0x08001982      0029           cmp r1, 0
|  `======< 0x08001984      c3db           blt 0x800190e
|   ::      0x08001986      1d49           ldr r1, [0x080019fc]        ; [0x80019fc:4]=0x44aa200
|   ::      0x08001988      0268           ldr r2, [r0]                ; arg1
|   ::      0x0800198a      8a42           cmp r2, r1
|   `=====< 0x0800198c      bfd9           bls 0x800190e
|    :      0x0800198e      0160           str r1, [r0]                ; arg1
|    `====< 0x08001990      bde7           b 0x800190e
|           ; CODE XREF from spi_init @ 0x800191a(x)
| --------> 0x08001992      0021           movs r1, 0
|           ; CODE XREF from spi_init @ 0x800191e(x)
| --------> 0x08001994      0268           ldr r2, [r0]                ; 0x8002ca2
|                                                                      ; aav.0x08002ca2 ; arg1
|           0x08001996      ca40           lsrs r2, r1
|           0x08001998      4260           str r2, [r0, 4]             ; arg1
|           0x0800199a      6168           ldr r1, [r4, 4]             ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x0800199c      c1f30121       ubfx r1, r1, 8, 2
|           0x080019a0      6568           ldr r5, [r4, 4]             ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x080019a2      6d05           lsls r5, r5, 0x15
|       ,=< 0x080019a4      01d5           bpl 0x80019aa
|       |   0x080019a6      595c           ldrb r1, [r3, r1]
|      ,==< 0x080019a8      00e0           b 0x80019ac
|      ||   ; CODE XREF from spi_init @ 0x80019a4(x)
|      |`-> 0x080019aa      0021           movs r1, 0
|      |    ; CODE XREF from spi_init @ 0x80019a8(x)
|      `--> 0x080019ac      22fa01f1       lsr.w r1, r2, r1
|           0x080019b0      8160           str r1, [r0, 8]             ; arg1
|           0x080019b2      6168           ldr r1, [r4, 4]             ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x080019b4      c1f3c121       ubfx r1, r1, 0xb, 2
|           0x080019b8      6568           ldr r5, [r4, 4]             ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x080019ba      ad04           lsls r5, r5, 0x12
|       ,=< 0x080019bc      01d5           bpl 0x80019c2
|       |   0x080019be      595c           ldrb r1, [r3, r1]
|      ,==< 0x080019c0      00e0           b 0x80019c4
|      ||   ; CODE XREF from spi_init @ 0x80019bc(x)
|      |`-> 0x080019c2      0021           movs r1, 0
|      |    ; CODE XREF from spi_init @ 0x80019c0(x)
|      `--> 0x080019c4      ca40           lsrs r2, r1
|           0x080019c6      c260           str r2, [r0, 0xc]           ; arg1
|           0x080019c8      6168           ldr r1, [r4, 4]             ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x080019ca      c1f38131       ubfx r1, r1, 0xe, 2
|           0x080019ce      6368           ldr r3, [r4, 4]             ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x080019d0      db00           lsls r3, r3, 3
|       ,=< 0x080019d2      01d5           bpl 0x80019d8
|       |   0x080019d4      41f00401       orr r1, r1, 4
|       |   ; CODE XREF from spi_init @ 0x80019d2(x)
|       `-> 0x080019d8      054b           ldr r3, aav.0x08002ca2      ; [0x8002ca2:4]=0x4030201
|           0x080019da      0833           adds r3, 8
|           0x080019dc      595c           ldrb r1, [r3, r1]           ; 0x8002ca2
|                                                                      ; aav.0x08002ca2
|           0x080019de      b2fbf1f1       udiv r1, r2, r1
|           0x080019e2      0161           str r1, [r0, 0x10]          ; arg1
\           0x080019e4      70bd           pop {r4, r5, r6, pc}
            ; DATA XREF from spi_init @ +0x108(r)
            ; CODE XREFS from clock_setup @ 0x8001b7e(r), 0x8001ba2(x)
/ 26: fcn.08001a00 (uint32_t arg1);
| `- args(r0)
|           0x08001a00      0649           ldr r1, [0x08001a1c]        ; [0x8001a1c:4]=0x40021000
|           0x08001a02      0128           cmp r0, 1                   ; 1 ; arg1
|       ,=< 0x08001a04      04d0           beq 0x8001a10
|       |   0x08001a06      486d           ldr r0, [r1, 0x54]
|       |   0x08001a08      20f03000       bic r0, r0, 0x30
|       |   0x08001a0c      4865           str r0, [r1, 0x54]
|       |   0x08001a0e      7047           bx lr
|       |   ; CODE XREF from fcn.08001a00 @ 0x8001a04(x)
|       `-> 0x08001a10      486d           ldr r0, [r1, 0x54]
|           0x08001a12      40f03000       orr r0, r0, 0x30
|           0x08001a16      4865           str r0, [r1, 0x54]
\           0x08001a18      7047           bx lr

; CALL XREF from fcn.08001494 @ 0x800149a(x)
/ 24: fcn.08001a20 (int16_t arg1);
| `- args(r0)
|           0x08001a20      0021           movs r1, 0
|           0x08001a22      0180           strh r1, [r0]               ; arg1
|           0x08001a24      4180           strh r1, [r0, 2]            ; arg1
|           0x08001a26      8180           strh r1, [r0, 4]            ; arg1
|           0x08001a28      c180           strh r1, [r0, 6]            ; arg1
|           0x08001a2a      0181           strh r1, [r0, 8]            ; arg1
|           0x08001a2c      4181           strh r1, [r0, 0xa]          ; arg1
|           0x08001a2e      8181           strh r1, [r0, 0xc]          ; arg1
|           0x08001a30      c181           strh r1, [r0, 0xe]          ; arg1
|           0x08001a32      0721           movs r1, 7
|           0x08001a34      0182           strh r1, [r0, 0x10]         ; arg1
\           0x08001a36      7047           bx lr

; CALL XREF from fcn.08001494 @ 0x80014de(x)
/ 24: fcn.08001a38 (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x08001a38      0029           cmp r1, 0                   ; arg2
|       ,=< 0x08001a3a      04d0           beq 0x8001a46
|       |   0x08001a3c      0188           ldrh r1, [r0]               ; arg1
|       |   0x08001a3e      41f04001       orr r1, r1, 0x40
|       |   0x08001a42      0180           strh r1, [r0]               ; arg1
|       |   0x08001a44      7047           bx lr
|       |   ; CODE XREF from fcn.08001a38 @ 0x8001a3a(x)
|       `-> 0x08001a46      0188           ldrh r1, [r0]               ; arg1
|           0x08001a48      21f04001       bic r1, r1, 0x40
|           0x08001a4c      0180           strh r1, [r0]               ; arg1
\           0x08001a4e      7047           bx lr

; CALL XREFS from lcd_spi_write_cmd @ 0x8001be4(x), 0x8001c06(x)
            ; CALL XREFS from lcd_fill_color @ 0x8001cf8(x), 0x8001d1a(x)
/ 14: fcn.08001a50 (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x08001a50      0246           mov r2, r0                  ; arg1
|           0x08001a52      0020           movs r0, 0
|           0x08001a54      1289           ldrh r2, [r2, 8]
|           0x08001a56      0a42           tst r2, r1                  ; arg2
|       ,=< 0x08001a58      00d0           beq 0x8001a5c
|       |   0x08001a5a      0120           movs r0, 1
|       |   ; CODE XREF from fcn.08001a50 @ 0x8001a58(x)
\       `-> 0x08001a5c      7047           bx lr

; CALL XREF from lcd_spi_write_cmd @ 0x8001c10(x)
            ; CALL XREF from lcd_fill_color @ 0x8001d02(x)
/ 4: fcn.08001a5e (int16_t arg1);
| `- args(r0)
|           0x08001a5e      8089           ldrh r0, [r0, 0xc]          ; arg1
\           0x08001a60      7047           bx lr

; CALL XREF from lcd_spi_write_cmd @ 0x8001bf0(x)
            ; CALL XREF from lcd_fill_color @ 0x8001cdc(x)
/ 4: fcn.08001a62 (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x08001a62      8181           strh r1, [r0, 0xc]          ; arg2
\           0x08001a64      7047           bx lr

; CALL XREF from fcn.08001494 @ 0x80014d6(x)
/ 84: fcn.08001a66 (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x08001a66      30b5           push {r4, r5, lr}
|           0x08001a68      0288           ldrh r2, [r0]               ; arg1
|           0x08001a6a      02f44152       and r2, r2, 0x3040
|           0x08001a6e      8b89           ldrh r3, [r1, 0xc]          ; arg2
|           0x08001a70      1b04           lsls r3, r3, 0x10
|       ,=< 0x08001a72      04d5           bpl 0x8001a7e
|       |   0x08001a74      8388           ldrh r3, [r0, 4]            ; arg1
|       |   0x08001a76      43f48073       orr r3, r3, 0x100
|       |   0x08001a7a      8380           strh r3, [r0, 4]            ; arg1
|      ,==< 0x08001a7c      03e0           b 0x8001a86
|      ||   ; CODE XREF from fcn.08001a66 @ 0x8001a72(x)
|      |`-> 0x08001a7e      8388           ldrh r3, [r0, 4]            ; arg1
|      |    0x08001a80      23f48073       bic r3, r3, 0x100
|      |    0x08001a84      8380           strh r3, [r0, 4]            ; arg1
|      |    ; CODE XREF from fcn.08001a66 @ 0x8001a7c(x)
|      `--> 0x08001a86      0b88           ldrh r3, [r1]               ; arg2
|           0x08001a88      4c88           ldrh r4, [r1, 2]            ; arg2
|           0x08001a8a      cd88           ldrh r5, [r1, 6]            ; arg2
|           0x08001a8c      2343           orrs r3, r4
|           0x08001a8e      8c88           ldrh r4, [r1, 4]            ; arg2
|           0x08001a90      2c43           orrs r4, r5
|           0x08001a92      2343           orrs r3, r4
|           0x08001a94      0c89           ldrh r4, [r1, 8]            ; arg2
|           0x08001a96      2343           orrs r3, r4
|           0x08001a98      4c89           ldrh r4, [r1, 0xa]          ; arg2
|           0x08001a9a      2343           orrs r3, r4
|           0x08001a9c      8c89           ldrh r4, [r1, 0xc]          ; arg2
|           0x08001a9e      c4f30e04       ubfx r4, r4, 0, 0xf
|           0x08001aa2      2343           orrs r3, r4
|           0x08001aa4      cc89           ldrh r4, [r1, 0xe]          ; arg2
|           0x08001aa6      2343           orrs r3, r4
|           0x08001aa8      1343           orrs r3, r2
|           0x08001aaa      0380           strh r3, [r0]               ; arg1
|           0x08001aac      828b           ldrh r2, [r0, 0x1c]         ; arg1
|           0x08001aae      22f40062       bic r2, r2, 0x800
|           0x08001ab2      8283           strh r2, [r0, 0x1c]         ; arg1
|           0x08001ab4      098a           ldrh r1, [r1, 0x10]         ; arg2
|           0x08001ab6      0182           strh r1, [r0, 0x10]         ; arg2
\           0x08001ab8      30bd           pop {r4, r5, pc}

; CALL XREF from lcd_draw_text @ 0x800178e(x)
            ; CALL XREF from app_validate_jump @ 0x8002342(x)
/ 56: svccall_handler (int16_t arg1, int16_t arg2, int16_t arg3, int16_t arg4);
| `- args(r0, r1, r2, r3)
|           0x08001aba      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|           0x08001abe      0746           mov r7, r0                  ; arg1
|           0x08001ac0      1646           mov r6, r2                  ; arg3
|           0x08001ac2      491e           subs r1, r1, 1              ; arg2
|           0x08001ac4      8cb2           uxth r4, r1                 ; arg2
|           0x08001ac6      5b1e           subs r3, r3, 1              ; arg4
|           0x08001ac8      9db2           uxth r5, r3
|           0x08001aca      2a20           movs r0, 0x2a               ; '*'
|           0x08001acc      00f04cfc       bl model_xor_decode
|           0x08001ad0      3846           mov r0, r7                  ; int16_t arg1
|           0x08001ad2      00f085fb       bl fcn.080021e0
|           0x08001ad6      2046           mov r0, r4                  ; int16_t arg1
|           0x08001ad8      00f082fb       bl fcn.080021e0
|           0x08001adc      2b20           movs r0, 0x2b               ; '+'
|           0x08001ade      00f043fc       bl model_xor_decode
|           0x08001ae2      3046           mov r0, r6                  ; int16_t arg1
|           0x08001ae4      00f07cfb       bl fcn.080021e0
|           0x08001ae8      2846           mov r0, r5
|           0x08001aea      bde8f041       pop.w {r4, r5, r6, r7, r8, lr}
\       ,=< 0x08001aee      00f077bb       b.w fcn.080021e0

:   ; CALL XREF from system_init @ 0x8001ed4(x)
/ 180: clock_setup ();
| afv: vars(2:sp[0xc..0x10])
|       :   0x08001af4      aff30080       nop.w
|       :   0x08001af8      1cb5           push {r2, r3, r4, lr}
|       :   0x08001afa      0022           movs r2, 0
|       :   0x08001afc      0192           str r2, [var_4h]
|       :   0x08001afe      0092           str r2, [sp]
|       :   0x08001b00      294c           ldr r4, [0x08001ba8]        ; [0x8001ba8:4]=0x40021000
|       :   0x08001b02      2068           ldr r0, [r4]
|       :   0x08001b04      40f00100       orr r0, r0, 1
|       :   0x08001b08      2060           str r0, [r4]
|       :   0x08001b0a      4ff6ff70       movw r0, 0xffff
|       :   ; CODE XREF from clock_setup @ 0x8001b24(x)
|      .--> 0x08001b0e      2168           ldr r1, [r4]
|      ::   0x08001b10      01f00201       and r1, r1, 2
|      ::   0x08001b14      0091           str r1, [sp]
|      ::   0x08001b16      0199           ldr r1, [var_4h]
|      ::   0x08001b18      491c           adds r1, r1, 1
|      ::   0x08001b1a      0191           str r1, [var_4h]
|      ::   0x08001b1c      0099           ldr r1, [sp]
|     ,===< 0x08001b1e      11b9           cbnz r1, 0x8001b26
|     |::   0x08001b20      0199           ldr r1, [var_4h]
|     |::   0x08001b22      8142           cmp r1, r0
|     |`==< 0x08001b24      f3d1           bne 0x8001b0e
|     | :   ; CODE XREF from clock_setup @ 0x8001b1e(x)
|     `---> 0x08001b26      2068           ldr r0, [r4]
|       :   0x08001b28      8007           lsls r0, r0, 0x1e
|      ,==< 0x08001b2a      02d5           bpl 0x8001b32
|      |:   0x08001b2c      0120           movs r0, 1
|      |:   0x08001b2e      0090           str r0, [sp]
|     ,===< 0x08001b30      00e0           b 0x8001b34
|     ||:   ; CODE XREF from clock_setup @ 0x8001b2a(x)
|     |`--> 0x08001b32      0092           str r2, [sp]
|     | :   ; CODE XREF from clock_setup @ 0x8001b30(x)
|     `---> 0x08001b34      0098           ldr r0, [sp]
|       :   0x08001b36      0128           cmp r0, 1                   ; 1
|      ,==< 0x08001b38      35d1           bne 0x8001ba6
|      |:   0x08001b3a      6068           ldr r0, [r4, 4]
|      |:   0x08001b3c      6060           str r0, [r4, 4]
|      |:   0x08001b3e      6068           ldr r0, [r4, 4]
|      |:   0x08001b40      20f46050       bic r0, r0, 0x3800
|      |:   0x08001b44      6060           str r0, [r4, 4]
|      |:   0x08001b46      6068           ldr r0, [r4, 4]
|      |:   0x08001b48      40f40050       orr r0, r0, 0x2000
|      |:   0x08001b4c      6060           str r0, [r4, 4]
|      |:   0x08001b4e      6068           ldr r0, [r4, 4]
|      |:   0x08001b50      20f4e060       bic r0, r0, 0x700
|      |:   0x08001b54      6060           str r0, [r4, 4]
|      |:   0x08001b56      6068           ldr r0, [r4, 4]
|      |:   0x08001b58      40f48060       orr r0, r0, 0x400
|      |:   0x08001b5c      6060           str r0, [r4, 4]
|      |:   0x08001b5e      6068           ldr r0, [r4, 4]
|      |:   0x08001b60      1249           ldr r1, [0x08001bac]        ; [0x8001bac:4]=0x1fc0ffff
|      |:   0x08001b62      0840           ands r0, r1
|      |:   0x08001b64      6060           str r0, [r4, 4]
|      |:   0x08001b66      6068           ldr r0, [r4, 4]
|      |:   0x08001b68      1149           ldr r1, [0x08001bb0]        ; [0x8001bb0:4]=0xa0340000
|      |:   0x08001b6a      0843           orrs r0, r1
|      |:   0x08001b6c      6060           str r0, [r4, 4]
|      |:   0x08001b6e      2068           ldr r0, [r4]
|      |:   0x08001b70      40f08070       orr r0, r0, 0x1000000
|      |:   0x08001b74      2060           str r0, [r4]
|      |:   ; CODE XREF from clock_setup @ 0x8001b7a(x)
|     .---> 0x08001b76      2068           ldr r0, [r4]
|     :|:   0x08001b78      8001           lsls r0, r0, 6
|     `===< 0x08001b7a      fcd5           bpl 0x8001b76
|      |:   0x08001b7c      0120           movs r0, 1
|      |:   0x08001b7e      fff73fff       bl fcn.08001a00
|      |:   0x08001b82      6068           ldr r0, [r4, 4]
|      |:   0x08001b84      20f00300       bic r0, r0, 3
|      |:   0x08001b88      6060           str r0, [r4, 4]
|      |:   0x08001b8a      6068           ldr r0, [r4, 4]
|      |:   0x08001b8c      40f00200       orr r0, r0, 2
|      |:   0x08001b90      6060           str r0, [r4, 4]
|      |:   ; CODE XREF from clock_setup @ 0x8001b9a(x)
|     .---> 0x08001b92      6068           ldr r0, [r4, 4]
|     :|:   0x08001b94      c0f38100       ubfx r0, r0, 2, 2
|     :|:   0x08001b98      0228           cmp r0, 2                   ; 2
|     `===< 0x08001b9a      fad1           bne 0x8001b92
|      |:   0x08001b9c      bde81c40       pop.w {r2, r3, r4, lr}
|      |:   0x08001ba0      0020           movs r0, 0
|      |`=< 0x08001ba2      fff72dbf       b.w fcn.08001a00
|      |    ; CODE XREF from clock_setup @ 0x8001b38(x)
\      `--> 0x08001ba6      1cbd           pop {r2, r3, r4, pc}

; CALL XREF from lcd_init_st7789 @ 0x8001d9a(x)
/ 20: fcn.08001bb4 ();
|           0x08001bb4      10b5           push {r4, lr}
|           0x08001bb6      0120           movs r0, 1
|           0x08001bb8      00f064f8       bl lcd_set_window
|           0x08001bbc      c007           lsls r0, r0, 0x1f
|       ,=< 0x08001bbe      01d0           beq 0x8001bc4
|       |   0x08001bc0      0120           movs r0, 1
|       |   0x08001bc2      10bd           pop {r4, pc}
|       |   ; CODE XREF from fcn.08001bb4 @ 0x8001bbe(x)
|       `-> 0x08001bc4      0020           movs r0, 0
\           0x08001bc6      10bd           pop {r4, pc}

; CALL XREF from crc_ccitt_spi @ 0x8000450(x)
            ; CALL XREF from lcd_spi_write_data @ 0x8001c5c(x)
            ; CALL XREF from lcd_set_window @ 0x8001caa(x)
/ 80: lcd_spi_write_cmd ();
|           0x08001bc8      70b5           push {r4, r5, r6, lr}
|           0x08001bca      0024           movs r4, 0
|           0x08001bcc      4ff49675       mov.w r5, 0x12c
|           0x08001bd0      114e           ldr r6, [0x08001c18]        ; [0x8001c18:4]=0x40003800
|       ,=< 0x08001bd2      05e0           b 0x8001be0
|       |   ; CODE XREF from lcd_spi_write_cmd @ 0x8001bea(x)
|      .--> 0x08001bd4      641c           adds r4, r4, 1
|      :|   0x08001bd6      a4b2           uxth r4, r4
|      :|   0x08001bd8      ac42           cmp r4, r5
|     ,===< 0x08001bda      01d9           bls 0x8001be0
|     |:|   0x08001bdc      0020           movs r0, 0
|     |:|   0x08001bde      70bd           pop {r4, r5, r6, pc}
|     |:|   ; CODE XREFS from lcd_spi_write_cmd @ 0x8001bd2(x), 0x8001bda(x)
|     `-`-> 0x08001be0      0221           movs r1, 2                  ; uint32_t arg2
|      :    0x08001be2      3046           mov r0, r6                  ; int16_t arg1
|      :    0x08001be4      fff734ff       bl fcn.08001a50
|      :    0x08001be8      0028           cmp r0, 0
|      `==< 0x08001bea      f3d0           beq 0x8001bd4
|           0x08001bec      a521           movs r1, 0xa5               ; int16_t arg2
|           0x08001bee      3046           mov r0, r6                  ; int16_t arg1
|           0x08001bf0      fff737ff       bl fcn.08001a62
|       ,=< 0x08001bf4      05e0           b 0x8001c02
|       |   ; CODE XREF from lcd_spi_write_cmd @ 0x8001c0c(x)
|      .--> 0x08001bf6      641c           adds r4, r4, 1
|      :|   0x08001bf8      a4b2           uxth r4, r4
|      :|   0x08001bfa      ac42           cmp r4, r5
|     ,===< 0x08001bfc      01d9           bls 0x8001c02
|     |:|   0x08001bfe      0020           movs r0, 0
|     |:|   0x08001c00      70bd           pop {r4, r5, r6, pc}
|     |:|   ; CODE XREFS from lcd_spi_write_cmd @ 0x8001bf4(x), 0x8001bfc(x)
|     `-`-> 0x08001c02      0121           movs r1, 1                  ; uint32_t arg2
|      :    0x08001c04      3046           mov r0, r6                  ; int16_t arg1
|      :    0x08001c06      fff723ff       bl fcn.08001a50
|      :    0x08001c0a      0028           cmp r0, 0
|      `==< 0x08001c0c      f3d0           beq 0x8001bf6
|           0x08001c0e      3046           mov r0, r6                  ; int16_t arg1
|           0x08001c10      fff725ff       bl fcn.08001a5e
|           0x08001c14      c0b2           uxtb r0, r0
\           0x08001c16      70bd           pop {r4, r5, r6, pc}

:   ; CALL XREFS from check_update_button @ 0x80004dc(x), 0x800050a(x)
        :   ; CALL XREF from spi_cmd_read_status @ 0x80009ca(x)
        :   ; CODE XREF from flash_clear_flags @ 0x800222e(x)
        :   ; CODE XREF from flash_erase_page @ 0x800229a(x)
/ 98: lcd_spi_write_data (int16_t arg1, int16_t arg2, int16_t arg3);
| `- args(r0, r1, r2)
|       :   0x08001c1c      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|       :   0x08001c20      0446           mov r4, r0                  ; arg1
|       :   0x08001c22      0d46           mov r5, r1                  ; arg2
|       :   0x08001c24      1646           mov r6, r2                  ; arg3
|       :   0x08001c26      dff85880       ldr.w r8, [0x08001c80]      ; [0x8001c80:4]=0x40010c00
|       :   0x08001c2a      4ff48057       mov.w r7, 0x1000
|       :   0x08001c2e      3946           mov r1, r7
|       :   0x08001c30      4046           mov r0, r8
|       :   0x08001c32      fff7b6fa       bl fcn.080011a2
|       :   0x08001c36      0120           movs r0, 1
|       :   0x08001c38      fef7e7fd       bl fcn.0800080a
|       :   0x08001c3c      0320           movs r0, 3
|       :   0x08001c3e      00f047f8       bl lcd_fill_color
|       :   0x08001c42      c4f30740       ubfx r0, r4, 0x10, 8
|       :   0x08001c46      00f043f8       bl lcd_fill_color
|       :   0x08001c4a      c4f30720       ubfx r0, r4, 8, 8
|       :   0x08001c4e      00f03ff8       bl lcd_fill_color
|       :   0x08001c52      e0b2           uxtb r0, r4
|       :   0x08001c54      00f03cf8       bl lcd_fill_color
|       :   0x08001c58      0024           movs r4, 0
|      ,==< 0x08001c5a      05e0           b 0x8001c68
|      |:   ; CODE XREF from lcd_spi_write_data @ 0x8001c6a(x)
|     .---> 0x08001c5c      fff7b4ff       bl lcd_spi_write_cmd
|     :|:   0x08001c60      05f8010b       strb r0, [r5], 1
|     :|:   0x08001c64      641c           adds r4, r4, 1
|     :|:   0x08001c66      a4b2           uxth r4, r4
|     :|:   ; CODE XREF from lcd_spi_write_data @ 0x8001c5a(x)
|     :`--> 0x08001c68      b442           cmp r4, r6
|     `===< 0x08001c6a      f7d3           blo 0x8001c5c
|       :   0x08001c6c      3946           mov r1, r7
|       :   0x08001c6e      4046           mov r0, r8
|       :   0x08001c70      fff799fa       bl fcn.080011a6
|       :   0x08001c74      bde8f041       pop.w {r4, r5, r6, r7, r8, lr}
|       :   0x08001c78      0120           movs r0, 1
\       `=< 0x08001c7a      fef7c6bd       b.w fcn.0800080a

; DATA XREF from clock_setup @ +0xbe(r)
            ; CALL XREF from fcn.08001bb4 @ 0x8001bb8(r)
/ 70: lcd_set_window (uint32_t arg1);
| `- args(r0)
|           0x08001c84      70b5           push {r4, r5, r6, lr}
|           0x08001c86      0228           cmp r0, 2                   ; 2 ; arg1
|       ,=< 0x08001c88      1bd0           beq 0x8001cc2
|       |   0x08001c8a      0328           cmp r0, 3                   ; 3 ; arg1
|      ,==< 0x08001c8c      1bd0           beq 0x8001cc6
|      ||   0x08001c8e      0524           movs r4, 5
|      ||   ; CODE XREFS from lcd_set_window @ 0x8001cc4(x), 0x8001cc8(x)
|    ..---> 0x08001c90      0e4e           ldr r6, [0x08001ccc]        ; [0x8001ccc:4]=0x40010c00
|    ::||   0x08001c92      4ff48055       mov.w r5, 0x1000
|    ::||   0x08001c96      2946           mov r1, r5
|    ::||   0x08001c98      3046           mov r0, r6
|    ::||   0x08001c9a      fff782fa       bl fcn.080011a2
|    ::||   0x08001c9e      0120           movs r0, 1
|    ::||   0x08001ca0      fef7b3fd       bl fcn.0800080a
|    ::||   0x08001ca4      2046           mov r0, r4
|    ::||   0x08001ca6      00f013f8       bl lcd_fill_color
|    ::||   0x08001caa      fff78dff       bl lcd_spi_write_cmd
|    ::||   0x08001cae      0446           mov r4, r0
|    ::||   0x08001cb0      2946           mov r1, r5
|    ::||   0x08001cb2      3046           mov r0, r6
|    ::||   0x08001cb4      fff777fa       bl fcn.080011a6
|    ::||   0x08001cb8      0120           movs r0, 1
|    ::||   0x08001cba      fef7a6fd       bl fcn.0800080a
|    ::||   0x08001cbe      2046           mov r0, r4
|    ::||   0x08001cc0      70bd           pop {r4, r5, r6, pc}
|    ::||   ; CODE XREF from lcd_set_window @ 0x8001c88(x)
|    ::|`-> 0x08001cc2      3524           movs r4, 0x35               ; '5'
|    `====< 0x08001cc4      e4e7           b 0x8001c90
|     :|    ; CODE XREF from lcd_set_window @ 0x8001c8c(x)
|     :`--> 0x08001cc6      1524           movs r4, 0x15
\     `===< 0x08001cc8      e2e7           b 0x8001c90

; XREFS: CALL 0x0800042a  CALL 0x08000430  CALL 0x08000436  CALL 0x0800043c  CALL 0x08001c3e  
            ; XREFS: CALL 0x08001c46  CALL 0x08001c4e  CALL 0x08001c54  CALL 0x08001ca6  CALL 0x08001d56  
            ; XREFS: CALL 0x08001d5e  CALL 0x08001d66  CALL 0x08001d6c  CALL 0x08001d76  CALL 0x08001e30  
/ 86: lcd_fill_color (int16_t arg1);
| `- args(r0)
|           0x08001cd0      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|           0x08001cd4      0024           movs r4, 0
|           0x08001cd6      144f           ldr r7, [0x08001d28]        ; [0x8001d28:4]=0x40003800
|           0x08001cd8      0146           mov r1, r0                  ; int16_t arg2
|           0x08001cda      3846           mov r0, r7                  ; int16_t arg1
|           0x08001cdc      fff7c1fe       bl fcn.08001a62
|           0x08001ce0      4ff49675       mov.w r5, 0x12c
|       ,=< 0x08001ce4      06e0           b 0x8001cf4
|       |   ; CODE XREF from lcd_fill_color @ 0x8001cfe(x)
|      .--> 0x08001ce6      641c           adds r4, r4, 1
|      :|   0x08001ce8      a4b2           uxth r4, r4
|      :|   0x08001cea      ac42           cmp r4, r5
|     ,===< 0x08001cec      02d9           bls 0x8001cf4
|     |:|   0x08001cee      0020           movs r0, 0
|     |:|   ; CODE XREFS from lcd_fill_color @ 0x8001d14(x), 0x8001d24(x)
|   ..----> 0x08001cf0      bde8f081       pop.w {r4, r5, r6, r7, r8, pc}
|   ::|:|   ; CODE XREFS from lcd_fill_color @ 0x8001ce4(x), 0x8001cec(x)
|   ::`-`-> 0x08001cf4      0121           movs r1, 1                  ; uint32_t arg2
|   :: :    0x08001cf6      3846           mov r0, r7                  ; int16_t arg1
|   :: :    0x08001cf8      fff7aafe       bl fcn.08001a50
|   :: :    0x08001cfc      0028           cmp r0, 0
|   :: `==< 0x08001cfe      f2d0           beq 0x8001ce6
|   ::      0x08001d00      3846           mov r0, r7                  ; int16_t arg1
|   ::      0x08001d02      fff7acfe       bl fcn.08001a5e
|   ::      0x08001d06      c6b2           uxtb r6, r0
|   ::  ,=< 0x08001d08      05e0           b 0x8001d16
|   ::  |   ; CODE XREF from lcd_fill_color @ 0x8001d20(x)
|   :: .--> 0x08001d0a      641c           adds r4, r4, 1
|   :: :|   0x08001d0c      a4b2           uxth r4, r4
|   :: :|   0x08001d0e      ac42           cmp r4, r5
|   ::,===< 0x08001d10      01d9           bls 0x8001d16
|   ::|:|   0x08001d12      0020           movs r0, 0
|   `=====< 0x08001d14      ece7           b 0x8001cf0
|    :|:|   ; CODE XREFS from lcd_fill_color @ 0x8001d08(x), 0x8001d10(x)
|    :`-`-> 0x08001d16      8021           movs r1, 0x80               ; uint32_t arg2
|    : :    0x08001d18      3846           mov r0, r7                  ; int16_t arg1
|    : :    0x08001d1a      fff799fe       bl fcn.08001a50
|    : :    0x08001d1e      0128           cmp r0, 1                   ; 1
|    : `==< 0x08001d20      f3d0           beq 0x8001d0a
|    :      0x08001d22      3046           mov r0, r6
\    `====< 0x08001d24      e4e7           b 0x8001cf0

::   ; CALL XREF from check_update_button @ 0x8000554(x)
/ 230: lcd_init_st7789 (int16_t arg1, int16_t arg2, int16_t arg3);
| `- args(r0, r1, r2)
|      ::   0x08001db0      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|      ::   0x08001db4      0646           mov r6, r0                  ; arg1
|      ::   0x08001db6      1446           mov r4, r2                  ; arg3
|      ::   0x08001db8      0f46           mov r7, r1                  ; arg2
|      ::   0x08001dba      f0b2           uxtb r0, r6
|      ::   0x08001dbc      c0f58075       rsb.w r5, r0, 0x100
|      ::   0x08001dc0      ac42           cmp r4, r5
|     ,===< 0x08001dc2      22d9           bls 0x8001e0a
|     |::   0x08001dc4      2a46           mov r2, r5
|     |::   0x08001dc6      3946           mov r1, r7
|     |::   0x08001dc8      3046           mov r0, r6
|     |::   0x08001dca      fff7afff       bl 0x8001d2c
|     |::   0x08001dce      641b           subs r4, r4, r5
|     |::   0x08001dd0      2e44           add r6, r5
|     |::   0x08001dd2      3d44           add r5, r7
|     |::   0x08001dd4      4ff48077       mov.w r7, 0x100
|    ,====< 0x08001dd8      13e0           b 0x8001e02
|    ||::   ; CODE XREF from lcd_init_st7789 @ 0x8001e04(x)
|   .-----> 0x08001dda      3a46           mov r2, r7
|   :||::   0x08001ddc      9442           cmp r4, r2
|  ,======< 0x08001dde      0ad9           bls 0x8001df6
|  |:||::   0x08001de0      2946           mov r1, r5
|  |:||::   0x08001de2      3046           mov r0, r6
|  |:||::   0x08001de4      fff7a2ff       bl 0x8001d2c
|  |:||::   0x08001de8      a4f58074       sub.w r4, r4, 0x100
|  |:||::   0x08001dec      06f58076       add.w r6, r6, 0x100
|  |:||::   0x08001df0      05f58075       add.w r5, r5, 0x100
| ,=======< 0x08001df4      05e0           b 0x8001e02
| ||:||::   ; CODE XREF from lcd_init_st7789 @ 0x8001dde(x)
| |`------> 0x08001df6      a2b2           uxth r2, r4
| | :||::   0x08001df8      2946           mov r1, r5
| | :||::   0x08001dfa      3046           mov r0, r6
| | :||::   0x08001dfc      fff796ff       bl 0x8001d2c
| | :||::   0x08001e00      0024           movs r4, 0
| | :||::   ; CODE XREFS from lcd_init_st7789 @ 0x8001dd8(x), 0x8001df4(x)
| `--`----> 0x08001e02      002c           cmp r4, 0
|   `=====< 0x08001e04      e9d1           bne 0x8001dda
|     |::   0x08001e06      bde8f081       pop.w {r4, r5, r6, r7, r8, pc}
|     |::   ; CODE XREF from lcd_init_st7789 @ 0x8001dc2(x)
|     `---> 0x08001e0a      a2b2           uxth r2, r4
|      ::   0x08001e0c      3046           mov r0, r6
|      ::   0x08001e0e      bde8f041       pop.w {r4, r5, r6, r7, r8, lr}
|      `==< 0x08001e12      fff78bbf       b.w 0x8001d2c
..
        :   ; CALL XREF from lcd_init_st7789 @ 0x8001d3a(x)
            ; DATA XREF from lcd_backlight_on @ 0x8001e1a(r)
        |   ; CALL XREF from lcd_gpio_init @ 0x80003fa(x)
|     |||   ; CODE XREF from systick_handler @ 0x8001e62(x)
|     | |   ; CODE XREF from systick_handler @ 0x8001e66(x)
|      ||   ; CODE XREF from systick_handler @ 0x8001e80(x)
        |   ; DATA XREF from systick_handler @ 0x8001e50(r)
        |   ; DATA XREF from systick_handler @ 0x8001e6e(r)

:   ; CALL XREF from lcd_init_st7789 @ 0x8001d3a(x)
/ 46: lcd_backlight_on ();
|       :   0x08001e18      70b5           push {r4, r5, r6, lr}
|       :   0x08001e1a      0b4d           ldr r5, [0x08001e48]        ; [0x8001e48:4]=0x40010c00
|       :   0x08001e1c      4ff48054       mov.w r4, 0x1000
|       :   0x08001e20      2146           mov r1, r4
|       :   0x08001e22      2846           mov r0, r5
|       :   0x08001e24      fff7bdf9       bl fcn.080011a2
|       :   0x08001e28      0120           movs r0, 1
|       :   0x08001e2a      fef7eefc       bl fcn.0800080a
|       :   0x08001e2e      0620           movs r0, 6
|       :   0x08001e30      fff74eff       bl lcd_fill_color
|       :   0x08001e34      2146           mov r1, r4
|       :   0x08001e36      2846           mov r0, r5
|       :   0x08001e38      fff7b5f9       bl fcn.080011a6
|       :   0x08001e3c      bde87040       pop.w {r4, r5, r6, lr}
|       :   0x08001e40      0120           movs r0, 1
\       `=< 0x08001e42      fef7e2bc       b.w fcn.0800080a

; CALL XREF from lcd_gpio_init @ 0x80003fa(x)
/ 54: systick_handler ();
|           0x08001e50      0d48           ldr r0, [0x08001e88]        ; [0x8001e88:4]=0x20000020
|           0x08001e52      4ff47a71       mov.w r1, 0x3e8
|           0x08001e56      0068           ldr r0, [r0]
|           0x08001e58      b0fbf1f0       udiv r0, r0, r1
|           0x08001e5c      401e           subs r0, r0, 1
|           0x08001e5e      b0f1807f       cmp.w r0, 0x1000000
|       ,=< 0x08001e62      01d3           blo 0x8001e68
|       |   0x08001e64      0120           movs r0, 1
|      ,==< 0x08001e66      0ae0           b 0x8001e7e
|      ||   ; CODE XREF from systick_handler @ 0x8001e62(x)
|      |`-> 0x08001e68      4ff0e021       mov.w r1, -0x1fff2000
|      |    0x08001e6c      4861           str r0, [r1, 0x14]
|      |    0x08001e6e      074a           ldr r2, [0x08001e8c]        ; [0x8001e8c:4]=0xe000ed23
|      |    0x08001e70      f020           movs r0, 0xf0
|      |    0x08001e72      1070           strb r0, [r2]
|      |    0x08001e74      0020           movs r0, 0
|      |    0x08001e76      8861           str r0, [r1, 0x18]
|      |    0x08001e78      0720           movs r0, 7
|      |    0x08001e7a      0861           str r0, [r1, 0x10]
|      |    0x08001e7c      0020           movs r0, 0
|      |    ; CODE XREF from systick_handler @ 0x8001e66(x)
|      `--> 0x08001e7e      0028           cmp r0, 0
|       ,=< 0x08001e80      00d0           beq 0x8001e84
|      @=-> 0x08001e82      fee7           b 0x8001e82
|       |   ; CODE XREF from systick_handler @ 0x8001e80(x)
\       `-> 0x08001e84      7047           bx lr

/ 84: system_init ();
|           0x08001e90      10b5           push {r4, lr}
|           0x08001e92      1448           ldr r0, [0x08001ee4]        ; [0x8001ee4:4]=0xe000ed88
|           0x08001e94      0168           ldr r1, [r0]
|           0x08001e96      41f47001       orr r1, r1, 0xf00000
|           0x08001e9a      0160           str r1, [r0]
|           0x08001e9c      1248           ldr r0, [0x08001ee8]        ; [0x8001ee8:4]=0x40021000
|           0x08001e9e      0168           ldr r1, [r0]
|           0x08001ea0      41f00101       orr r1, r1, 1
|           0x08001ea4      0160           str r1, [r0]
|           0x08001ea6      4168           ldr r1, [r0, 4]
|           0x08001ea8      104a           ldr r2, [0x08001eec]        ; [0x8001eec:4]=0xe8ff000c
|           0x08001eaa      1140           ands r1, r2
|           0x08001eac      4160           str r1, [r0, 4]
|           0x08001eae      0168           ldr r1, [r0]
|           0x08001eb0      0f4a           ldr r2, [0x08001ef0]        ; [0x8001ef0:4]=0xfef6ffff
|           0x08001eb2      1140           ands r1, r2
|           0x08001eb4      0160           str r1, [r0]
|           0x08001eb6      0168           ldr r1, [r0]
|           0x08001eb8      21f48021       bic r1, r1, 0x40000
|           0x08001ebc      0160           str r1, [r0]
|           0x08001ebe      4168           ldr r1, [r0, 4]
|           0x08001ec0      0c4a           ldr r2, [0x08001ef4]        ; [0x8001ef4:4]=0x1700ffff
|           0x08001ec2      1140           ands r1, r2
|           0x08001ec4      4160           str r1, [r0, 4]
|           0x08001ec6      016b           ldr r1, [r0, 0x30]
|           0x08001ec8      0b4a           ldr r2, [0x08001ef8]        ; [0x8001ef8:4]=0xfefeff00
|           0x08001eca      1140           ands r1, r2
|           0x08001ecc      0163           str r1, [r0, 0x30]
|           0x08001ece      4ff41f01       mov.w r1, 0x9f0000
|           0x08001ed2      8160           str r1, [r0, 8]
|           0x08001ed4      fff70efe       bl clock_setup
|           0x08001ed8      0249           ldr r1, [0x08001ee4]        ; [0x8001ee4:4]=0xe000ed88
|           0x08001eda      4ff00060       mov.w r0, fcn.08000000      ; 0x8000000
|                                                                      ; r15
|           0x08001ede      8039           subs r1, 0x80
|           0x08001ee0      0860           str r0, [r1]
\           0x08001ee2      10bd           pop {r4, pc}

; CALL XREFS from uart_update_mode @ 0x8000240(x), 0x800027c(x)
            ; CALL XREFS from check_update_button @ 0x8000574(x), 0x80005b2(x)
; --------------------------------------------------------------------------
; timer_reset: Reset the UART protocol watchdog timer.
; Clears the timer state structure at 0x20000454:
;   [0] = counter (u16), [2] = flags (u8), [3] = flags (u8)
; --------------------------------------------------------------------------
/ 12: timer_reset ();
|           0x08001f08      0248           ldr r0, [0x08001f14]        ; [0x8001f14:4]=0x20000454
|           0x08001f0a      0021           movs r1, 0
|           0x08001f0c      c170           strb r1, [r0, 3]
|           0x08001f0e      0180           strh r1, [r0]
|           0x08001f10      8170           strb r1, [r0, 2]
\           0x08001f12      7047           bx lr

; CALL XREF from uart_init @ 0x80021ce(x)
/ 24: timer_delay_us (int16_t arg1, uint32_t arg2);
| `- args(r0, r1)
|           0x08001f18      0029           cmp r1, 0                   ; arg2
|       ,=< 0x08001f1a      04d0           beq 0x8001f26
|       |   0x08001f1c      8189           ldrh r1, [r0, 0xc]          ; arg1
|       |   0x08001f1e      41f40051       orr r1, r1, 0x2000
|       |   0x08001f22      8181           strh r1, [r0, 0xc]          ; arg1
|       |   0x08001f24      7047           bx lr
|       |   ; CODE XREF from timer_delay_us @ 0x8001f1a(x)
|       `-> 0x08001f26      8189           ldrh r1, [r0, 0xc]          ; arg1
|           0x08001f28      21f40051       bic r1, r1, 0x2000
|           0x08001f2c      8181           strh r1, [r0, 0xc]          ; arg1
\           0x08001f2e      7047           bx lr

; CALL XREF from flash_lock @ 0x800206e(x)
/ 64: timer_delay_ms (int16_t arg1, int16_t arg2);
| `- args(r0, r1)
|           0x08001f30      70b5           push {r4, r5, r6, lr}
|           0x08001f32      0024           movs r4, 0
|           0x08001f34      40f66a12       movw r2, 0x96a
|           0x08001f38      c1f34213       ubfx r3, r1, 5, 3           ; arg2
|           0x08001f3c      01f01f05       and r5, r1, 0x1f            ; arg2
|           0x08001f40      0126           movs r6, 1
|           0x08001f42      06fa05f2       lsl.w r2, r6, r5
|           0x08001f46      012b           cmp r3, 1                   ; 1
|       ,=< 0x08001f48      0cd0           beq 0x8001f64
|       |   0x08001f4a      022b           cmp r3, 2                   ; 2
|      ,==< 0x08001f4c      0dd0           beq 0x8001f6a
|      ||   0x08001f4e      838a           ldrh r3, [r0, 0x14]         ; arg1
|      ||   0x08001f50      1340           ands r3, r2
|      ||   ; CODE XREFS from timer_delay_ms @ 0x8001f68(x), 0x8001f6e(x)
|    ..---> 0x08001f52      090a           lsrs r1, r1, 8              ; arg2
|    ::||   0x08001f54      8e40           lsls r6, r1                 ; arg2
|    ::||   0x08001f56      0088           ldrh r0, [r0]               ; arg1
|    ::||   0x08001f58      3040           ands r0, r6
|   ,=====< 0x08001f5a      0bb1           cbz r3, 0x8001f60
|  ,======< 0x08001f5c      00b1           cbz r0, 0x8001f60
|  ||::||   0x08001f5e      0124           movs r4, 1
|  ||::||   ; CODE XREFS from timer_delay_ms @ 0x8001f5a(x), 0x8001f5c(x)
|  ``-----> 0x08001f60      2046           mov r0, r4
|    ::||   0x08001f62      70bd           pop {r4, r5, r6, pc}
|    ::||   ; CODE XREF from timer_delay_ms @ 0x8001f48(x)
|    ::|`-> 0x08001f64      8389           ldrh r3, [r0, 0xc]          ; arg1
|    ::|    0x08001f66      1340           ands r3, r2
|    `====< 0x08001f68      f3e7           b 0x8001f52
|     :|    ; CODE XREF from timer_delay_ms @ 0x8001f4c(x)
|     :`--> 0x08001f6a      038a           ldrh r3, [r0, 0x10]         ; arg1
|     :     0x08001f6c      1340           ands r3, r2
\     `===< 0x08001f6e      f0e7           b 0x8001f52

; CALL XREF from uart_init @ 0x80021c6(x)
/ 54: delay_loop (int16_t arg2);
| `- args(r1)
|           0x08001f70      10b5           push {r4, lr}
|           0x08001f72      40f66a13       movw r3, 0x96a
|           0x08001f76      c1f34213       ubfx r3, r1, 5, 3           ; arg2
|           0x08001f7a      01f01f04       and r4, r1, 0x1f            ; arg2
|           0x08001f7e      0121           movs r1, 1
|           0x08001f80      a140           lsls r1, r4
|           0x08001f82      012b           cmp r3, 1                   ; 1
|       ,=< 0x08001f84      07d0           beq 0x8001f96
|       |   0x08001f86      022b           cmp r3, 2                   ; 2
|      ,==< 0x08001f88      07d0           beq 0x8001f9a
|      ||   0x08001f8a      1430           adds r0, 0x14
|      ||   ; CODE XREFS from delay_loop @ 0x8001f98(x), 0x8001f9c(x)
|   ,..---> 0x08001f8c      3ab1           cbz r2, 0x8001f9e
|   |::||   0x08001f8e      0268           ldr r2, [r0]
|   |::||   0x08001f90      0a43           orrs r2, r1
|   |::||   0x08001f92      0260           str r2, [r0]
|   |::||   0x08001f94      10bd           pop {r4, pc}
|   |::||   ; CODE XREF from delay_loop @ 0x8001f84(x)
|   |::|`-> 0x08001f96      0c30           adds r0, 0xc
|   |`====< 0x08001f98      f8e7           b 0x8001f8c
|   | :|    ; CODE XREF from delay_loop @ 0x8001f88(x)
|   | :`--> 0x08001f9a      1030           adds r0, 0x10
|   | `===< 0x08001f9c      f6e7           b 0x8001f8c
|   |       ; CODE XREF from delay_loop @ 0x8001f8c(x)
|   `-----> 0x08001f9e      0268           ldr r2, [r0]
|           0x08001fa0      8a43           bics r2, r1
|           0x08001fa2      0260           str r2, [r0]
\           0x08001fa4      10bd           pop {r4, pc}

; CALL XREF from uart_init @ 0x80021ba(x)
/ 182: flash_status_get (int16_t arg1, int16_t arg2);
| `- args(r0, r1) vars(3:sp[0x14..0x20])
|           0x08001fa8      30b5           push {r4, r5, lr}
|           0x08001faa      85b0           sub sp, 0x14
|           0x08001fac      0446           mov r4, r0                  ; arg1
|           0x08001fae      0d46           mov r5, r1                  ; arg2
|           0x08001fb0      208a           ldrh r0, [r4, 0x10]
|           0x08001fb2      4cf6ff71       movw r1, 0xcfff
|           0x08001fb6      0840           ands r0, r1
|           0x08001fb8      e988           ldrh r1, [r5, 6]
|           0x08001fba      0143           orrs r1, r0
|           0x08001fbc      2182           strh r1, [r4, 0x10]
|           0x08001fbe      a089           ldrh r0, [r4, 0xc]
|           0x08001fc0      4ef6f311       movw r1, 0xe9f3
|           0x08001fc4      0840           ands r0, r1
|           0x08001fc6      a988           ldrh r1, [r5, 4]
|           0x08001fc8      2a89           ldrh r2, [r5, 8]
|           0x08001fca      1143           orrs r1, r2
|           0x08001fcc      6a89           ldrh r2, [r5, 0xa]
|           0x08001fce      0243           orrs r2, r0
|           0x08001fd0      1143           orrs r1, r2
|           0x08001fd2      a181           strh r1, [r4, 0xc]
|           0x08001fd4      a08a           ldrh r0, [r4, 0x14]
|           0x08001fd6      4ff6ff41       movw r1, 0xfcff
|           0x08001fda      0840           ands r0, r1
|           0x08001fdc      a989           ldrh r1, [r5, 0xc]
|           0x08001fde      0143           orrs r1, r0
|           0x08001fe0      a182           strh r1, [r4, 0x14]
|           0x08001fe2      6846           mov r0, sp
|           0x08001fe4      fff786fc       bl spi_init
|           0x08001fe8      1d48           ldr r0, [0x08002060]        ; [0x8002060:4]=0x40013800
|           0x08001fea      8442           cmp r4, r0
|       ,=< 0x08001fec      01d1           bne 0x8001ff2
|       |   0x08001fee      0398           ldr r0, [var_ch]
|      ,==< 0x08001ff0      00e0           b 0x8001ff4
|      ||   ; CODE XREF from flash_status_get @ 0x8001fec(x)
|      |`-> 0x08001ff2      0298           ldr r0, [var_8h]
|      |    ; CODE XREF from flash_status_get @ 0x8001ff0(x)
|      `--> 0x08001ff4      a189           ldrh r1, [r4, 0xc]
|           0x08001ff6      0904           lsls r1, r1, 0x10
|       ,=< 0x08001ff8      08d5           bpl 0x800200c
|       |   0x08001ffa      00ebc001       add.w r1, r0, r0, lsl 3
|       |   0x08001ffe      01eb0010       add.w r0, r1, r0, lsl 4
|       |   0x08002002      2968           ldr r1, [r5]
|       |   0x08002004      4900           lsls r1, r1, 1
|       |   0x08002006      b0fbf1f0       udiv r0, r0, r1
|      ,==< 0x0800200a      07e0           b 0x800201c
|      ||   ; CODE XREF from flash_status_get @ 0x8001ff8(x)
|      |`-> 0x0800200c      00ebc001       add.w r1, r0, r0, lsl 3
|      |    0x08002010      01eb0010       add.w r0, r1, r0, lsl 4
|      |    0x08002014      2968           ldr r1, [r5]
|      |    0x08002016      8900           lsls r1, r1, 2
|      |    0x08002018      b0fbf1f0       udiv r0, r0, r1
|      |    ; CODE XREF from flash_status_get @ 0x800200a(x)
|      `--> 0x0800201c      6422           movs r2, 0x64               ; 'd'
|           0x0800201e      b0fbf2f1       udiv r1, r0, r2
|           0x08002022      0901           lsls r1, r1, 4
|           0x08002024      0b09           lsrs r3, r1, 4
|           0x08002026      6ff01805       mvn r5, 0x18
|           0x0800202a      6b43           muls r3, r5, r3
|           0x0800202c      00eb8300       add.w r0, r0, r3, lsl 2
|           0x08002030      a389           ldrh r3, [r4, 0xc]
|           0x08002032      1d04           lsls r5, r3, 0x10
|           0x08002034      4ff03203       mov.w r3, 0x32              ; '2'
|       ,=< 0x08002038      07d5           bpl 0x800204a
|       |   0x0800203a      03ebc000       add.w r0, r3, r0, lsl 3
|       |   0x0800203e      b0fbf2f0       udiv r0, r0, r2
|       |   0x08002042      00f00700       and r0, r0, 7
|       |   0x08002046      0843           orrs r0, r1
|      ,==< 0x08002048      06e0           b 0x8002058
|      ||   ; CODE XREF from flash_status_get @ 0x8002038(x)
|      |`-> 0x0800204a      03eb0010       add.w r0, r3, r0, lsl 4
|      |    0x0800204e      b0fbf2f0       udiv r0, r0, r2
|      |    0x08002052      00f00f00       and r0, r0, 0xf
|      |    0x08002056      0843           orrs r0, r1
|      |    ; CODE XREF from flash_status_get @ 0x8002048(x)
|      `--> 0x08002058      2081           strh r0, [r4, 8]
|           0x0800205a      05b0           add sp, 0x14
\           0x0800205c      30bd           pop {r4, r5, pc}

; CODE XREF from system_init @ +0x6c(x)
/ 80: flash_lock ();
|           0x08002064      10b5           push {r4, lr}
|           0x08002066      134c           ldr r4, [0x080020b4]        ; [0x80020b4:4]=0x40004c00
|           0x08002068      40f22551       movw r1, 0x525
|           0x0800206c      2046           mov r0, r4
|           0x0800206e      fff75fff       bl timer_delay_ms
|           0x08002072      0028           cmp r0, 0
|       ,=< 0x08002074      1dd0           beq 0x80020b2
|       |   0x08002076      4ff6df70       movw r0, 0xffdf
|       |   0x0800207a      2080           strh r0, [r4]
|       |   0x0800207c      201d           adds r0, r4, 4
|       |   0x0800207e      0088           ldrh r0, [r0]
|       |   0x08002080      c0f30801       ubfx r1, r0, 0, 9
|       |   0x08002084      0c48           ldr r0, [0x080020b8]        ; [0x80020b8:4]=0x20000008
|       |   0x08002086      0160           str r1, [r0]
|       |   0x08002088      0c48           ldr r0, [0x080020bc]        ; [0x80020bc:4]=0x20000044
|       |   0x0800208a      90f81224       ldrb.w r2, [r0, 0x412]
|       |   0x0800208e      002a           cmp r2, 0
|      ,==< 0x08002090      0fd1           bne 0x80020b2
|      ||   0x08002092      b0f81024       ldrh.w r2, [r0, 0x410]
|      ||   0x08002096      8154           strb r1, [r0, r2]
|      ||   0x08002098      00f58260       add.w r0, r0, 0x410
|      ||   0x0800209c      0188           ldrh r1, [r0]
|      ||   0x0800209e      4ff48262       mov.w r2, 0x410
|      ||   0x080020a2      491c           adds r1, r1, 1
|      ||   0x080020a4      b1fbf2f3       udiv r3, r1, r2
|      ||   0x080020a8      02fb1311       mls r1, r2, r3, r1
|      ||   0x080020ac      0180           strh r1, [r0]
|      ||   0x080020ae      0021           movs r1, 0
|      ||   0x080020b0      c170           strb r1, [r0, 3]
|      ||   ; CODE XREFS from flash_lock @ 0x8002074(x), 0x8002090(x)
\      ``-> 0x080020b2      10bd           pop {r4, pc}

; CALL XREF from uart_init @ 0x8002192(x)
/ 22: flash_program_word (int16_t arg1);
| `- args(r0)
|           0x08002110      4ff41651       mov.w r1, 0x2580
|           0x08002114      0160           str r1, [r0]                ; arg1
|           0x08002116      0021           movs r1, 0
|           0x08002118      8180           strh r1, [r0, 4]            ; arg1
|           0x0800211a      c180           strh r1, [r0, 6]            ; arg1
|           0x0800211c      0181           strh r1, [r0, 8]            ; arg1
|           0x0800211e      0c22           movs r2, 0xc
|           0x08002120      4281           strh r2, [r0, 0xa]          ; arg1
|           0x08002122      8181           strh r1, [r0, 0xc]          ; arg1
\           0x08002124      7047           bx lr

; CALL XREF from main @ 0x80022b0(x)
/ 138: uart_init ();
| afv: vars(9:sp[0x9..0x1c])
|           0x0800214c      10b5           push {r4, lr}
|           0x0800214e      86b0           sub sp, 0x18
|           0x08002150      0121           movs r1, 1
|           0x08002152      c804           lsls r0, r1, 0x13
|           0x08002154      fff7b2fb       bl systick_init
|           ; DATA XREF from flash_program_word @ +0x34(r)
|           0x08002158      05a8           add r0, var_14h
|           0x0800215a      fff726f8       bl fcn.080011aa
|           0x0800215e      4ff48060       mov.w r0, 0x400
|           0x08002162      adf81400       strh.w r0, [var_14h]
|           0x08002166      0220           movs r0, 2
|           0x08002168      8df81600       strb.w r0, [var_16h]
|           0x0800216c      1820           movs r0, 0x18
|           0x0800216e      8df81700       strb.w r0, [var_17h]
|           0x08002172      194c           ldr r4, [0x080021d8]        ; [0x80021d8:4]=0x40011000
|           0x08002174      05a9           add r1, var_14h
|           0x08002176      2046           mov r0, r4
|           0x08002178      fef792fd       bl spi_flash_read_id
|           0x0800217c      e114           asrs r1, r4, 0x13
|           0x0800217e      adf81410       strh.w r1, [var_14h]
|           0x08002182      4821           movs r1, 0x48               ; 'H'
|           0x08002184      8df81710       strb.w r1, [var_17h]
|           0x08002188      05a9           add r1, var_14h
|           0x0800218a      2046           mov r0, r4
|           0x0800218c      fef788fd       bl spi_flash_read_id
|           0x08002190      01a8           add r0, var_4h
|           0x08002192      fff7bdff       bl flash_program_word
|           0x08002196      4ff4e130       mov.w r0, 0x1c200
|           0x0800219a      0190           str r0, [var_4h]
|           0x0800219c      0020           movs r0, 0
|           0x0800219e      adf80800       strh.w r0, [var_8h]
|           0x080021a2      adf80a00       strh.w r0, [var_ah]
|           0x080021a6      adf80c00       strh.w r0, [var_ch]
|           0x080021aa      adf81000       strh.w r0, [var_10h]
|           0x080021ae      0c20           movs r0, 0xc
|           0x080021b0      adf80e00       strh.w r0, [var_eh]
|           0x080021b4      094c           ldr r4, [0x080021dc]        ; [0x80021dc:4]=0x40004c00
|           0x080021b6      01a9           add r1, var_4h
|           0x080021b8      2046           mov r0, r4
|           0x080021ba      fff7f5fe       bl flash_status_get
|           0x080021be      0122           movs r2, 1
|           0x080021c0      40f22551       movw r1, 0x525
|           0x080021c4      2046           mov r0, r4
|           0x080021c6      fff7d3fe       bl delay_loop
|           0x080021ca      0121           movs r1, 1
|           0x080021cc      2046           mov r0, r4
|           0x080021ce      fff7a3fe       bl timer_delay_us
|           0x080021d2      06b0           add sp, 0x18
\           0x080021d4      10bd           pop {r4, pc}

; XREFS: CALL 0x080017b2  CALL 0x080017ba  CALL 0x08001ad2  CALL 0x08001ad8  CALL 0x08001ae4  
            ; XREFS: CODE 0x08001aee  DATA 0x080021dc  CALL 0x08002354  CALL 0x08002458  CALL 0x08002482  
            ; XREFS: CALL 0x08002488  CALL 0x08002494  CODE 0x0800249e  
/ 20: fcn.080021e0 (int16_t arg1);
| `- args(r0)
|           0x080021e0      10b5           push {r4, lr}
|           0x080021e2      0446           mov r4, r0                  ; arg1
|           0x080021e4      200a           lsrs r0, r4, 8
|           0x080021e6      00f0eff8       bl model_xor_encode
|           0x080021ea      e0b2           uxtb r0, r4
|           0x080021ec      bde81040       pop.w {r4, lr}
\       ,=< 0x080021f0      00f0eab8       b.w model_xor_encode

; CALL XREFS from libc_init_array @ 0x8000206(r), 0x8000210(x)
            ; NULL XREF from aav.0x08002ca2 @ +0x1e(r)
/ 14: memcpy_init (uint32_t arg3);
| `- args(r2)
|       ,=< 0x080021f4      02e0           b 0x80021fc
|       |   ; CODE XREF from memcpy_init @ 0x80021fe(x)
|      .--> 0x080021f6      08c8           ldm r0!, {r3}
|      :|   0x080021f8      121f           subs r2, r2, 4              ; arg3
|      :|   0x080021fa      08c1           stm r1!, {r3}
|      :|   ; CODE XREF from memcpy_init @ 0x80021f4(x)
|      :`-> 0x080021fc      002a           cmp r2, 0                   ; arg3
|      `==< 0x080021fe      fad1           bne 0x80021f6
\           0x08002200      7047           bx lr

:   ; CALL XREF from flash_program @ 0x8001546(x)
/ 32: flash_clear_flags (int16_t arg1);
| `- args(r0)
|       :   0x08002212      0246           mov r2, r0                  ; arg1
|       :   0x08002214      4ff4ae10       mov.w r0, 0x15c000
|       :   0x08002218      1278           ldrb r2, [r2]
|       :   0x0800221a      203a           subs r2, 0x20
|       :   0x0800221c      5e2a           cmp r2, 0x5e                ; 94
|      ,==< 0x0800221e      05d8           bhi 0x800222c
|      |:   0x08002220      c2ebc200       rsb r0, r2, r2, lsl 3
|      |:   0x08002224      00eb4210       add.w r0, r0, r2, lsl 5
|      |:   0x08002228      00f5ae10       add.w r0, r0, 0x15c000
|      |:   ; CODE XREF from flash_clear_flags @ 0x800221e(x)
|      `--> 0x0800222c      2722           movs r2, 0x27               ; '\''
\       `=< 0x0800222e      fff7f5bc       b.w lcd_spi_write_data

:   ; CALL XREF from flash_program @ 0x800151a(x)
/ 108: flash_erase_page (int16_t arg1);
| `- args(r0)
|       :   0x08002232      30b4           push {r4, r5}
|       :   0x08002234      0246           mov r2, r0                  ; arg1
|       :   0x08002236      4ff46020       mov.w r0, 0xe0000
|       :   0x0800223a      1378           ldrb r3, [r2]
|       :   0x0800223c      5278           ldrb r2, [r2, 1]
|       :   0x0800223e      a3f1a404       sub.w r4, r3, 0xa4
|       :   0x08002242      042c           cmp r4, 4                   ; 4
|      ,==< 0x08002244      01d8           bhi 0x800224a
|      |:   0x08002246      a12a           cmp r2, 0xa1                ; 161
|     ,===< 0x08002248      25d2           bhs 0x8002296
|     ||:   ; CODE XREF from flash_erase_page @ 0x8002244(x)
|     |`--> 0x0800224a      a3f1a104       sub.w r4, r3, 0xa1
|     | :   0x0800224e      082c           cmp r4, 8                   ; 8
|     |,==< 0x08002250      0fd8           bhi 0x8002272
|     ||:   0x08002252      a12a           cmp r2, 0xa1                ; 161
|    ,====< 0x08002254      0dd3           blo 0x8002272
|    |||:   0x08002256      c4eb0410       rsb r0, r4, r4, lsl 4
|    |||:   0x0800225a      00eb4410       add.w r0, r0, r4, lsl 5
|    |||:   0x0800225e      02eb4000       add.w r0, r2, r0, lsl 1
|    |||:   0x08002262      a138           subs r0, 0xa1
|    |||:   0x08002264      c0eb0010       rsb r0, r0, r0, lsl 4
|    |||:   0x08002268      00eb8000       add.w r0, r0, r0, lsl 2
|    |||:   0x0800226c      00f56020       add.w r0, r0, 0xe0000
|   ,=====< 0x08002270      11e0           b 0x8002296
|   ||||:   ; CODE XREFS from flash_erase_page @ 0x8002250(x), 0x8002254(x)
|   |`-`--> 0x08002272      b03b           subs r3, 0xb0
|   | | :   0x08002274      472b           cmp r3, 0x47                ; 71
|   | |,==< 0x08002276      0ed8           bhi 0x8002296
|   | ||:   0x08002278      a12a           cmp r2, 0xa1                ; 161
|   |,====< 0x0800227a      0cd3           blo 0x8002296
|   ||||:   0x0800227c      c3eb0310       rsb r0, r3, r3, lsl 4
|   ||||:   0x08002280      00eb4310       add.w r0, r0, r3, lsl 5
|   ||||:   0x08002284      02eb4000       add.w r0, r2, r0, lsl 1
|   ||||:   0x08002288      a138           subs r0, 0xa1
|   ||||:   0x0800228a      c0eb0010       rsb r0, r0, r0, lsl 4
|   ||||:   0x0800228e      00eb8000       add.w r0, r0, r0, lsl 2
|   ||||:   0x08002292      00f56020       add.w r0, r0, 0xe0000
|   ||||:   ; CODE XREFS from flash_erase_page @ 0x8002248(x), 0x8002270(x), 0x8002276(x), 0x800227a(x)
|   ````--> 0x08002296      30bc           pop {r4, r5}
|       :   0x08002298      4b22           movs r2, 0x4b               ; 'K'
\       `=< 0x0800229a      fff7bfbc       b.w lcd_spi_write_data

; CODE XREF from stack_init @ 0x8000186(x)
/ 94: int main (int argc, char **argv, char **envp);
|           0x080022a0      10b5           push {r4, lr}
|           0x080022a2      0021           movs r1, 0
|           0x080022a4      4ff00060       mov.w r0, fcn.08000000      ; 0x8000000
|                                                                      ; r15
|           0x080022a8      fff7eafa       bl gpio_init
|           0x080022ac      fef7a4f8       bl lcd_gpio_init
|           0x080022b0      fff74cff       bl uart_init
|           0x080022b4      62b6           cpsie i
|           0x080022b6      fef7f1f8       bl check_update_button
|           0x080022ba      0128           cmp r0, 1                   ; 1
|       ,=< 0x080022bc      01d1           bne 0x80022c2
|       |   0x080022be      fdf7b1ff       bl uart_update_mode
|       |   ; CODE XREF from main @ 0x80022bc(x)
|       `-> 0x080022c2      fef7c7f9       bl check_spi_model
|       ,=< 0x080022c6      20b1           cbz r0, 0x80022d2
|       |   0x080022c8      fef7baf9       bl check_spi_flag
|       |   0x080022cc      0128           cmp r0, 1                   ; 1
|      ,==< 0x080022ce      01d0           beq 0x80022d4
|     ,===< 0x080022d0      04e0           b 0x80022dc
|    @|||   ; CODE XREF from main @ 0x80022c6(x)
|    @==`-> 0x080022d2      fee7           b 0x80022d2
|     ||    ; CODE XREF from main @ 0x80022ce(x)
|     |`--> 0x080022d4      00f014fd       bl aav.0x08002d00
|     |     0x080022d8      fef70cfa       bl clear_spi_update
|     |     ; CODE XREF from main @ 0x80022d0(x)
|     `---> 0x080022dc      0849           ldr r1, aav.0x08003000      ; [0x8002300:4]=0x8003000 aav.0x08003000
|           0x080022de      0868           ldr r0, [r1]
|           0x080022e0      084a           ldr r2, [0x08002304]        ; [0x8002304:4]=0x2ffe0000
|           0x080022e2      1040           ands r0, r2
|           0x080022e4      b0f1005f       cmp.w r0, 0x20000000
|       ,=< 0x080022e8      08d1           bne 0x80022fc
|       |   0x080022ea      72b6           cpsid i
|       |   0x080022ec      4868           ldr r0, [r1, 4]
|       |   0x080022ee      064a           ldr r2, [0x08002308]        ; [0x8002308:4]=0x20000000
|       |   0x080022f0      5060           str r0, [r2, 4]
|       |   0x080022f2      1060           str r0, [r2]
|       |   0x080022f4      0968           ldr r1, [r1]
|       |   0x080022f6      81f30888       invalid
..
|       |   ; CODE XREF from main @ 0x80022e8(x)
|       `-> 0x080022fc      0020           movs r0, 0
\           0x080022fe      10bd           pop {r4, pc}

; CALL XREF from flash_program @ 0x8001572(x)
/ 72: app_validate_jump (int16_t arg1, int16_t arg2, int16_t arg3, int16_t arg4, int16_t arg_18h);
| `- args(r0, r1, r2, r3)
|           0x08002320      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|           0x08002324      0446           mov r4, r0                  ; arg1
|           0x08002326      ddf81880       ldr.w r8, [arg_18h]         ; 0x178000
|                                                                      ; r13
|           0x0800232a      0846           mov r0, r1                  ; arg2
|           0x0800232c      1646           mov r6, r2                  ; arg3
|           0x0800232e      1f46           mov r7, r3                  ; arg4
|           0x08002330      002f           cmp r7, 0
|       ,=< 0x08002332      17d0           beq 0x8002364
|       |   0x08002334      002e           cmp r6, 0
|      ,==< 0x08002336      15d0           beq 0x8002364
|      ||   0x08002338      e119           adds r1, r4, r7
|      ||   0x0800233a      8bb2           uxth r3, r1
|      ||   0x0800233c      8119           adds r1, r0, r6             ; int16_t arg2
|      ||   0x0800233e      89b2           uxth r1, r1
|      ||   0x08002340      2246           mov r2, r4                  ; int16_t arg3
|      ||   0x08002342      fff7bafb       bl svccall_handler
|      ||   0x08002346      00f06ff8       bl fcn.08002428
|      ||   0x0800234a      0025           movs r5, 0
|     ,===< 0x0800234c      08e0           b 0x8002360
|     |||   ; CODE XREF from app_validate_jump @ 0x8002362(x)
|    .----> 0x0800234e      0024           movs r4, 0
|   ,=====< 0x08002350      03e0           b 0x800235a
|   |:|||   ; CODE XREF from app_validate_jump @ 0x800235c(x)
|  .------> 0x08002352      4046           mov r0, r8                  ; int16_t arg1
|  :|:|||   0x08002354      fff744ff       bl fcn.080021e0
|  :|:|||   0x08002358      641c           adds r4, r4, 1
|  :|:|||   ; CODE XREF from app_validate_jump @ 0x8002350(x)
|  :`-----> 0x0800235a      b442           cmp r4, r6
|  `======< 0x0800235c      f9d3           blo 0x8002352
|    :|||   0x0800235e      6d1c           adds r5, r5, 1
|    :|||   ; CODE XREF from app_validate_jump @ 0x800234c(x)
|    :`---> 0x08002360      bd42           cmp r5, r7
|    `====< 0x08002362      f4d3           blo 0x800234e
|      ||   ; CODE XREFS from app_validate_jump @ 0x8002332(x), 0x8002336(x)
\      ``-> 0x08002364      bde8f081       pop.w {r4, r5, r6, r7, r8, pc}

:   ; XREFS(23)
/ 92: model_xor_decode (int16_t arg1);
| `- args(r0)
|       :   0x08002368      70b5           push {r4, r5, r6, lr}
|       :   0x0800236a      0446           mov r4, r0                  ; arg1
|       :   0x0800236c      154d           ldr r5, [0x080023c4]        ; [0x80023c4:4]=0x40011400
|       :   0x0800236e      0221           movs r1, 2
|       :   0x08002370      2846           mov r0, r5
|       :   0x08002372      fef716ff       bl fcn.080011a2
|       :   0x08002376      0821           movs r1, 8
|       :   0x08002378      2846           mov r0, r5
|       :   0x0800237a      fef712ff       bl fcn.080011a2
|       :   0x0800237e      0121           movs r1, 1
|       :   0x08002380      2846           mov r0, r5
|       :   0x08002382      fef70eff       bl fcn.080011a2
|       :   0x08002386      2846           mov r0, r5                  ; int16_t arg1
|       :   0x08002388      fef727fa       bl fcn.080007da
|       :   0x0800238c      64f31f20       bfi r0, r4, 8, 0x18
|       :   0x08002390      0146           mov r1, r0                  ; int16_t arg2
|       :   0x08002392      2846           mov r0, r5                  ; int16_t arg1
|       :   0x08002394      fef711ff       bl fcn.080011ba
|       :   0x08002398      00bf           nop
|       :   0x0800239a      00bf           nop
|       :   0x0800239c      00bf           nop
|       :   0x0800239e      00bf           nop
|       :   0x080023a0      00bf           nop
|       :   0x080023a2      00bf           nop
|       :   0x080023a4      00bf           nop
|       :   0x080023a6      00bf           nop
|       :   0x080023a8      00bf           nop
|       :   0x080023aa      00bf           nop
|       :   0x080023ac      00bf           nop
|       :   0x080023ae      00bf           nop
|       :   0x080023b0      0121           movs r1, 1
|       :   0x080023b2      2846           mov r0, r5
|       :   0x080023b4      fef7f7fe       bl fcn.080011a6
|       :   0x080023b8      2846           mov r0, r5
|       :   0x080023ba      bde87040       pop.w {r4, r5, r6, lr}
|       :   0x080023be      0221           movs r1, 2
\       `=< 0x080023c0      fef7f1be       b.w fcn.080011a6

:   ; XREFS(50)
/ 92: model_xor_encode (int16_t arg1);
| `- args(r0)
|       :   0x080023c8      70b5           push {r4, r5, r6, lr}
|       :   0x080023ca      0446           mov r4, r0                  ; arg1
|       :   0x080023cc      154d           ldr r5, [0x08002424]        ; [0x8002424:4]=0x40011400
|       :   0x080023ce      0221           movs r1, 2
|       :   0x080023d0      2846           mov r0, r5
|       :   0x080023d2      fef7e6fe       bl fcn.080011a2
|       :   0x080023d6      0821           movs r1, 8
|       :   0x080023d8      2846           mov r0, r5
|       :   0x080023da      fef7e4fe       bl fcn.080011a6
|       :   0x080023de      0121           movs r1, 1
|       :   0x080023e0      2846           mov r0, r5
|       :   0x080023e2      fef7defe       bl fcn.080011a2
|       :   0x080023e6      2846           mov r0, r5
|       :   0x080023e8      fef7f7f9       bl fcn.080007da
|       :   0x080023ec      64f31f20       bfi r0, r4, 8, 0x18
|       :   0x080023f0      0146           mov r1, r0
|       :   0x080023f2      2846           mov r0, r5
|       :   0x080023f4      fef7e1fe       bl fcn.080011ba
|       :   0x080023f8      00bf           nop
|       :   0x080023fa      00bf           nop
|       :   0x080023fc      00bf           nop
|       :   0x080023fe      00bf           nop
|       :   0x08002400      00bf           nop
|       :   0x08002402      00bf           nop
|       :   0x08002404      00bf           nop
|       :   0x08002406      00bf           nop
|       :   0x08002408      00bf           nop
|       :   0x0800240a      00bf           nop
|       :   0x0800240c      00bf           nop
|       :   0x0800240e      00bf           nop
|       :   0x08002410      0121           movs r1, 1
|       :   0x08002412      2846           mov r0, r5
|       :   0x08002414      fef7c7fe       bl fcn.080011a6
|       :   0x08002418      2846           mov r0, r5
|       :   0x0800241a      bde87040       pop.w {r4, r5, r6, lr}
|       :   0x0800241e      0221           movs r1, 2
\       `=< 0x08002420      fef7c1be       b.w fcn.080011a6

:   ; CALL XREF from lcd_draw_text @ 0x8001798(x)
        :   ; CALL XREF from app_validate_jump @ 0x8002346(x)
        :   ; CALL XREF from fcn.0800242e @ 0x8002450(x)
/ 6: fcn.08002428 ();
|       :   0x08002428      2c20           movs r0, 0x2c               ; ','
\       `=< 0x0800242a      fff79dbf       b.w model_xor_decode

; CALL XREF from flash_erase_sector @ 0x800231a(x)
/ 56: fcn.0800242e (int16_t arg1, int16_t arg2, int16_t arg3, int16_t arg4, int16_t arg_18h);
| `- args(r0, r1, r2, r3)
|           0x0800242e      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|           0x08002432      0746           mov r7, r0                  ; arg1
|           0x08002434      069e           ldr r6, [arg_18h]           ; 0x178000
|                                                                      ; r13
|           0x08002436      1046           mov r0, r2                  ; arg3
|           0x08002438      0024           movs r4, 0
|           0x0800243a      c21b           subs r2, r0, r7
|           0x0800243c      92b2           uxth r2, r2
|           0x0800243e      5b1a           subs r3, r3, r1             ; arg4
|           0x08002440      9bb2           uxth r3, r3
|           0x08002442      02fb03f5       mul r5, r2, r3
|           0x08002446      0028           cmp r0, 0
|       ,=< 0x08002448      0bd0           beq 0x8002462
|       |   0x0800244a      3846           mov r0, r7                  ; int16_t arg1
|       |   0x0800244c      00f00bf8       bl fcn.08002466
|       |   0x08002450      fff7eaff       bl fcn.08002428
|      ,==< 0x08002454      03e0           b 0x800245e
|      ||   ; CODE XREF from fcn.0800242e @ 0x8002460(x)
|     .---> 0x08002456      3046           mov r0, r6                  ; int16_t arg1
|     :||   0x08002458      fff7c2fe       bl fcn.080021e0
|     :||   0x0800245c      641c           adds r4, r4, 1
|     :||   ; CODE XREF from fcn.0800242e @ 0x8002454(x)
|     :`--> 0x0800245e      ac42           cmp r4, r5
|     `===< 0x08002460      f9d3           blo 0x8002456
|       |   ; CODE XREF from fcn.0800242e @ 0x8002448(x)
\       `-> 0x08002462      bde8f081       pop.w {r4, r5, r6, r7, r8, pc}

:   ; CALL XREF from fcn.0800242e @ 0x800244c(x)
/ 60: fcn.08002466 (int16_t arg1, int16_t arg2, int16_t arg3, int16_t arg4);
| `- args(r0, r1, r2, r3)
|       :   0x08002466      2de9f041       push.w {r4, r5, r6, r7, r8, lr}
|       :   0x0800246a      0546           mov r5, r0                  ; arg1
|       :   0x0800246c      0c46           mov r4, r1                  ; arg2
|       :   0x0800246e      a818           adds r0, r5, r2             ; arg3
|       :   0x08002470      401e           subs r0, r0, 1
|       :   0x08002472      86b2           uxth r6, r0
|       :   0x08002474      e018           adds r0, r4, r3             ; arg4
|       :   0x08002476      401e           subs r0, r0, 1
|       :   0x08002478      87b2           uxth r7, r0
|       :   0x0800247a      2a20           movs r0, 0x2a               ; '*'
|       :   0x0800247c      fff774ff       bl model_xor_decode
|       :   0x08002480      2846           mov r0, r5
|       :   0x08002482      fff7adfe       bl fcn.080021e0
|       :   0x08002486      3046           mov r0, r6
|       :   0x08002488      fff7aafe       bl fcn.080021e0
|       :   0x0800248c      2b20           movs r0, 0x2b               ; '+'
|       :   0x0800248e      fff76bff       bl model_xor_decode
|       :   0x08002492      2046           mov r0, r4
|       :   0x08002494      fff7a4fe       bl fcn.080021e0
|       :   0x08002498      3846           mov r0, r7
|       :   0x0800249a      bde8f041       pop.w {r4, r5, r6, r7, r8, lr}
\       `=< 0x0800249e      fff79fbe       b.w fcn.080021e0


; ==========================================================================
; SECTION: Padding/Data (0x08002500 - 0x08002FFF)
; ==========================================================================
; Remaining bytes after code section.
; Mostly 0xFF (erased flash), may contain data tables or strings.
;
    ; 0x08002520: 00 00 C0 E0 F8 FC FC FE FE FF FF FF FF FF FF FF  |................|
    ; 0x08002590: FF FF FF FF FF FF FF FF FF FF FF FE FE FE FC F8  |................|
    ; 0x080025a0: F0 C0 FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002610: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x08002620: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002690: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x080026a0: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002710: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x08002720: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002790: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x080027a0: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002810: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x08002820: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002890: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x080028a0: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002910: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x08002920: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002990: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x080029a0: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002a10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF  |................|
    ; 0x08002a20: FF FF 3F FF FF FF FF FE FE FE FE FE FE FE FE FE  |..?.............|
    ; 0x08002a30: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE  |................|
    ; 0x08002a40: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE  |................|
    ; 0x08002a50: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE  |................|
    ; 0x08002a60: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE  |................|
    ; 0x08002a70: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE  |................|
    ; 0x08002a80: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE FE  |................|
    ; 0x08002a90: FE FE FE FE FE FE FE FE FE FE FE FE FE FE FF FF  |................|
    ; 0x08002aa0: FF 7F 00 00 01 03 07 0F 0F 0F 0F 0F 0F 0F 0F 0F  |................|
    ; 0x08002ab0: 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F  |................|
    ; 0x08002ac0: 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F FF FF FF  |................|
    ; 0x08002af0: FF FF FF FF FF FF FF 0F 0F 0F 0F 0F 0F 0F 0F 0F  |................|
    ; 0x08002b00: 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F  |................|
    ; 0x08002b10: 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 0F 07 03 01  |................|
    ; 0x08002b40: 00 00 00 00 00 00 00 00 00 00 00 00 00 FF FF FF  |................|
    ; 0x08002b70: FF FF FF FF FF FF FF 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002bb0: 00 00 70 F8 FC FC FC FC FC FC FC FC FC FC FC FC  |..p.............|
    ; 0x08002bc0: FC FC FC FC FC FC FC FC FC FC FC FC FC FF FF FF  |................|
    ; 0x08002bf0: FF FF FF FF FF FF FF FC FC FC FC FC FC FC FC FC  |................|
    ; 0x08002c00: FC FC FC FC FC FC FC FC FC FC FC FC FC FC FC FC  |................|
    ; 0x08002c10: FC F8 70 00 00 00 00 00 00 00 00 00 00 00 00 00  |..p.............|
    ; 0x08002c30: 00 00 00 00 01 01 01 01 01 01 01 01 01 01 01 01  |................|
    ; 0x08002c40: 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01  |................|
    ; 0x08002c50: 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01  |................|
    ; 0x08002c60: 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01  |................|
    ; 0x08002c70: 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01  |................|
    ; 0x08002c80: 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01 01  |................|
    ; 0x08002c90: 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  |................|
    ; 0x08002ca0: 00 00 01 02 03 04 06 07 08 09 02 04 06 08 02 0C  |................|
    ; 0x08002cb0: 08 10 00 00 68 2D 00 08 00 00 00 20 34 00 00 00  |....h-..... 4...|
    ; 0x08002cc0: F4 21 00 08 9C 2D 00 08 34 00 00 20 84 08 00 00  |.!...-..4.. ....|
    ; 0x08002cd0: 04 22 00 08 00 00 00 00 00 00 00 00 00 00 00 00  |."..............|
    ; 0x08002d30: 05 95 72 B6 FD F7 66 FF 31 46 0A 48 FD F7 12 FF  |..r...f.1F.H....|
    ; 0x08002d40: 08 48 21 46 00 1D FD F7 0D FF 06 48 29 46 08 30  |.H!F.......H)F.0|
    ; 0x08002d50: FD F7 08 FF FD F7 FA FE 62 B6 06 B0 70 BD 00 00  |........b...p...|
    ; 0x08002d60: E8 F7 FF 1F 00 2E 00 08 00 00 00 00 00 00 00 00  |................|
    ; 0x08002d80: 00 00 00 00 00 00 00 00 00 0E 27 07 00 00 00 00  |..........'.....|
    ; 0x08002d90: 00 00 00 00 01 02 03 04 06 07 08 09 FF FF FF FF  |................|
    ; 0x08002e00: 87 49 09 4D 17 D4 74 4A 90 9D 7D 07 FF FF FF FF  |.I.M..tJ..}.....|
;
; Known data locations:
;   0x080003A4: Model string = "RT-950      "
;
; End of bootloader binary (0x08002FFF)
; Application starts at 0x08003000
;
; ==========================================================================
