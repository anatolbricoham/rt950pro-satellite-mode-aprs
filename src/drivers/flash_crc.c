/*
 * flash_crc.c - CRC-16 CCITT for RT-950 Pro flash records
 *
 * Reimplements OEM crc16_update @ 0x0800A4B8.
 * Polynomial 0x1021, initial value 0x0000 (XModem variant).
 */

#include "drivers/flash_crc.h"

uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;

    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

int crc16_ccitt_verify(const uint8_t *data, uint16_t crc_offset)
{
    uint16_t computed = crc16_ccitt(data, crc_offset);
    uint16_t stored   = ((uint16_t)data[crc_offset] << 8)
                      | data[crc_offset + 1];
    return (computed == stored) ? 0 : -1;
}

void crc16_ccitt_stamp(uint8_t *data, uint16_t crc_offset)
{
    uint16_t crc = crc16_ccitt(data, crc_offset);
    data[crc_offset]     = (uint8_t)(crc >> 8);
    data[crc_offset + 1] = (uint8_t)(crc & 0xFF);
}
