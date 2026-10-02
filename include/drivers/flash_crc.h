/*
 * flash_crc.h - CRC-16 CCITT for RT-950 Pro flash records
 *
 * Shared CRC-16 implementation used by:
 *   - Wear-leveled flash records (SYSCFG, VFOCFG, EXTCFG, VFOSEL)
 *   - CPS programming protocol framing
 *
 * OEM implementation: crc16_update @ 0x0800A4B8
 *   Polynomial: 0x1021 (CRC-CCITT)
 *   Initial value: 0x0000 (XModem variant)
 *
 * CRC offsets within WL records (OEM verified):
 *   SYSCFG  (128B): CRC at offset 0x7E  (flash_wearleveling.c comment)
 *   VFOCFG  ( 96B): CRC at offset 0x5E  (assembly EEPROM layout)
 *   EXTCFG  (160B): CRC at offset 0x9E  (FLASH_EXTCFG_CRC_OFFSET)
 *   VFOSEL  ( 16B): CRC at offset 0x0E  (FLASH_VFOSEL_CRC_OFFSET)
 *
 * Note: AX.25 FCS uses a DIFFERENT polynomial (0x8408 reflected,
 * init 0xFFFF) - see aprs.c ax25_crc16().
 */

#ifndef DRIVERS_FLASH_CRC_H
#define DRIVERS_FLASH_CRC_H

#include <stdint.h>

/* CRC-16 CCITT offsets within wear-leveled records */
#define FLASH_CRC_SYSCFG_OFFSET     0x7E    /* in 128B record */
#define FLASH_CRC_VFOCFG_OFFSET     0x5E    /* in  96B record */
#define FLASH_CRC_EXTCFG_OFFSET     0x9E    /* in 160B record */
#define FLASH_CRC_VFOSEL_OFFSET     0x0E    /* in  16B record */

/*
 * crc16_ccitt - Compute CRC-16 CCITT over a byte buffer.
 *
 * Polynomial 0x1021, initial value 0x0000 (XModem variant).
 * Bit-by-bit shift-and-XOR, 8 bits per byte.
 *
 * @param data  Pointer to data buffer
 * @param len   Number of bytes to process
 * @return      16-bit CRC value
 */
uint16_t crc16_ccitt(const uint8_t *data, uint16_t len);

/*
 * crc16_ccitt_verify - Verify CRC-16 embedded in a record.
 *
 * Computes CRC over data[0..crc_offset-1] and compares to the
 * big-endian CRC stored at data[crc_offset..crc_offset+1].
 *
 * @param data        Record buffer
 * @param crc_offset  Byte offset of the 2-byte CRC within the record
 * @return            0 if CRC matches, -1 on mismatch
 */
int crc16_ccitt_verify(const uint8_t *data, uint16_t crc_offset);

/*
 * crc16_ccitt_stamp - Compute and embed CRC-16 into a record.
 *
 * Computes CRC over data[0..crc_offset-1] and writes the result
 * as big-endian at data[crc_offset..crc_offset+1].
 *
 * @param data        Record buffer (modified in place)
 * @param crc_offset  Byte offset where the 2-byte CRC will be stored
 */
void crc16_ccitt_stamp(uint8_t *data, uint16_t crc_offset);

#endif /* DRIVERS_FLASH_CRC_H */
