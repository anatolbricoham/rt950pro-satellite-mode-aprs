/*
 * gps.h - GPS module interface for the RT-950 Pro
 *
 * Connected via USART3 at 9600 baud (PB10/PB11 default, no remap).
 * Receives NMEA sentences ($GPGGA, $GPRMC and GN variants).
 *
 * GPS module power controlled by PA8 (GPS_ENABLE), active HIGH.
 *
 * OEM V0.27 references:
 *   USART3 init        @ 0x08013B20 (BRR=6250, PB10 AF_PP, PB11 IPU)
 *   GPS enable (PA8 SET)  @ 0x080095A6 (gpio_bits_set GPIOA 0x100)
 *   GPS disable (PA8 CLR) @ 0x08013FE0 (gpio_bits_reset GPIOA 0x100)
 *   usart3_audio_init     @ 0x08013FCC (mode conflict: CLRs PA8 for audio)
 *   gpio_modes_init       @ 0x0801391C (configures PA8 as OUTPUT)
 *
 * NOTE: OEM shares USART3 between GPS and serial audio modes.
 * When g_config[1]==1 (audio mode), PA8 is CLR'd and GPS is powered down.
 * Our implementation keeps GPS always-on; audio mode exclusion is TBD.
 */

#ifndef APP_GPS_H
#define APP_GPS_H

#include <stdint.h>

typedef struct {
    float latitude;           /* decimal degrees, negative = south */
    float longitude;          /* decimal degrees, negative = west */
    float altitude;           /* meters above MSL */
    float hdop;               /* horizontal dilution of precision */
    uint8_t fix_quality;      /* 0=none, 1=GPS, 2=DGPS */
    uint8_t num_satellites;   /* satellites in use */
    uint8_t hour, minute, second;  /* UTC time */
    /* Date from $xxRMC (added for satellite tracking) */
    uint8_t day, month;       /* 1-31, 1-12; 0 = not yet received */
    uint16_t year;            /* full year, e.g. 2026; 0 = unknown */
    uint8_t rmc_valid;        /* 1 = last RMC had status 'A' */
    uint8_t rmc_seq;          /* increments on every RMC with a valid date */
} gps_data_t;

void gps_init(void);
void gps_process(void);
const gps_data_t *gps_get_data(void);

/*
 * GPS power control (PA8).
 * OEM: SET @ 0x080095A6 (enable), CLR @ 0x08013FE0 (disable).
 * gps_init() calls gps_enable() automatically.
 */
void gps_enable(void);
void gps_disable(void);

/* Raw ring buffer access (for diagnostics/hw_test) */
int      gps_rx_available(void);
uint8_t  gps_rx_read(void);

#endif /* APP_GPS_H */
