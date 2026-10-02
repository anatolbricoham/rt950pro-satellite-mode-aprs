/*
 * flash_xor.h - Flash data XOR obfuscation for RT-950 Pro
 *
 * OEM firmware applies selective XOR obfuscation to certain flash sectors
 * during read/write. The transform is its own inverse (XOR encode == decode).
 *
 * OEM implementation: eeprom_block_copy @ 0x0801F8D0 (60 bytes)
 * Key location: RAM 0x20000220, loaded from flash 0x0805C53E
 *
 * Sectors using XOR obfuscation (OEM flash_data_load @ 0x08007358):
 *   0x8000 (Settings A / Channel Config) - YES
 *   0xB000 (Extended Radio Settings)     - YES
 *   0x9000 (Settings B / VFO Config)     - NO
 *   0xF000 (Calibration)                 - NO
 */

#ifndef DRIVERS_FLASH_XOR_H
#define DRIVERS_FLASH_XOR_H

#include <stdint.h>

/* OEM XOR key: 4 bytes, all 0x41 ("AAAA") */
#define FLASH_XOR_KEY_SIZE  4
extern const uint8_t flash_xor_key[FLASH_XOR_KEY_SIZE];

/*
 * flash_xor_copy - Selective XOR obfuscation/deobfuscation.
 *
 * Copies 'len' bytes from src to dst, applying XOR with a rotating
 * 4-byte key.  Certain bytes are copied as-is to preserve flash
 * sentinel values (erased state, zero-fill, key collisions).
 *
 * Skip XOR when ANY of these hold for the current byte:
 *   - key_byte == 0x20 (space)
 *   - src_byte == 0x00
 *   - src_byte == 0xFF (NOR flash erased state)
 *   - src_byte == key_byte
 *   - src_byte == (key_byte ^ 0xFF)
 *
 * The transform is symmetric: applying it twice yields the original data.
 *
 * @param dst   Destination buffer (may alias src for in-place transform)
 * @param src   Source buffer
 * @param len   Number of bytes to process
 */
void flash_xor_copy(uint8_t *dst, const uint8_t *src, uint16_t len);

/*
 * flash_xor_inplace - Apply XOR obfuscation in-place.
 *
 * Convenience wrapper: flash_xor_copy(buf, buf, len).
 */
static inline void flash_xor_inplace(uint8_t *buf, uint16_t len)
{
    flash_xor_copy(buf, buf, len);
}

#endif /* DRIVERS_FLASH_XOR_H */
