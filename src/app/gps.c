/*
 * gps.c - GPS NMEA parser for the RT-950 Pro
 *
 * GPS module connected via USART3 at 9600 baud (PB10 TX, PB11 RX).
 * Parses $GPGGA and $GPRMC sentences for position, altitude, time, HDOP.
 *
 * GPS module power controlled by PA8 (GPS_ENABLE), active HIGH.
 * OEM V0.27: PA8 SET @ 0x080095A6, CLR @ 0x08013FE0.
 * OEM shares USART3 between GPS and serial audio - modes are exclusive.
 *
 * OEM V0.27 references:
 *   USART3 init           @ 0x08013B20 (BRR=6250 for 9600 baud)
 *   usart3_audio_init     @ 0x08013FCC (CLRs PA8 when audio mode active)
 *   gpio_modes_init       @ 0x0801391C (PA8 configured as GPIO output)
 *   GPS_Parse_NMEA        @ 0x08009FB4 (OEM NMEA handling - minimal)
 */

#include "app/gps.h"
#include "drivers/uart.h"
#include "drivers/gpio.h"
#include "rt950_pinmap.h"
#include "at32f403a.h"
#include <string.h>

/* Ring buffer for USART3 RX */
#define GPS_BUF_SIZE    256
static volatile uint8_t  rx_buf[GPS_BUF_SIZE];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;

/* Raw ring buffer access for diagnostics */
int gps_rx_available(void)
{
    return rx_head != rx_tail;
}

uint8_t gps_rx_read(void)
{
    if (rx_head == rx_tail) return 0;
    uint8_t ch = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & (GPS_BUF_SIZE - 1);
    return ch;
}

/* NMEA sentence accumulator -------------------------------------------- */
#define NMEA_MAX_LEN    128
static char nmea_line[NMEA_MAX_LEN];
static uint8_t nmea_idx;
static uint8_t nmea_receiving;

/* Parsed GPS data ------------------------------------------------------ */
static gps_data_t gps_data;

/* Helpers -------------------------------------------------------------- */

static int parse_int(const char *s, int digits)
{
    int val = 0;
    for (int i = 0; i < digits && s[i] >= '0' && s[i] <= '9'; i++)
        val = val * 10 + (s[i] - '0');
    return val;
}

static float parse_float(const char *s)
{
    float result = 0.0f;
    float sign = 1.0f;
    if (*s == '-') { sign = -1.0f; s++; }

    while (*s >= '0' && *s <= '9')
        result = result * 10.0f + (*s++ - '0');

    if (*s == '.') {
        s++;
        float frac = 0.1f;
        while (*s >= '0' && *s <= '9') {
            result += (*s++ - '0') * frac;
            frac *= 0.1f;
        }
    }
    return sign * result;
}

/* Convert NMEA ddmm.mmmm to decimal degrees */
static float nmea_to_degrees(const char *s)
{
    float raw = parse_float(s);
    int degrees = (int)(raw / 100.0f);
    float minutes = raw - degrees * 100.0f;
    return (float)degrees + minutes / 60.0f;
}

/* Find the nth comma-delimited field (0-indexed) */
static const char *nmea_field(const char *sentence, int n)
{
    const char *p = sentence;
    for (int i = 0; i < n; i++) {
        p = strchr(p, ',');
        if (!p) return "";
        p++;
    }
    return p;
}

/* NMEA sentence parsers ------------------------------------------------ */

/* $GPGGA,hhmmss.ss,ddmm.mmmm,N,dddmm.mmmm,E,q,nn,hdop,alt,M,geoid,M,,*cs */
static void parse_gga(const char *sentence)
{
    const char *field;

    /* Field 1: UTC time hhmmss.ss */
    field = nmea_field(sentence, 1);
    if (*field >= '0') {
        gps_data.hour   = (uint8_t)parse_int(field, 2);
        gps_data.minute = (uint8_t)parse_int(field + 2, 2);
        gps_data.second = (uint8_t)parse_int(field + 4, 2);
    }

    /* Field 2,3: Latitude + N/S */
    field = nmea_field(sentence, 2);
    if (*field >= '0') {
        gps_data.latitude = nmea_to_degrees(field);
        field = nmea_field(sentence, 3);
        if (*field == 'S') gps_data.latitude = -gps_data.latitude;
    }

    /* Field 4,5: Longitude + E/W */
    field = nmea_field(sentence, 4);
    if (*field >= '0') {
        gps_data.longitude = nmea_to_degrees(field);
        field = nmea_field(sentence, 5);
        if (*field == 'W') gps_data.longitude = -gps_data.longitude;
    }

    /* Field 6: Fix quality (0=invalid, 1=GPS, 2=DGPS) */
    field = nmea_field(sentence, 6);
    gps_data.fix_quality = (uint8_t)parse_int(field, 1);

    /* Field 7: Number of satellites */
    field = nmea_field(sentence, 7);
    gps_data.num_satellites = (uint8_t)parse_int(field, 2);

    /* Field 9: Altitude (meters) */
    field = nmea_field(sentence, 9);
    if (*field >= '0' || *field == '-')
        gps_data.altitude = parse_float(field);

    /* Field 8: HDOP (horizontal dilution of precision) */
    field = nmea_field(sentence, 8);
    if (*field >= '0')
        gps_data.hdop = parse_float(field);
}

/* $GPRMC,hhmmss.ss,A,ddmm.mmmm,N,dddmm.mmmm,E,spd,cog,ddmmyy,mv,mvE*cs */
static void parse_rmc(const char *sentence)
{
    const char *field;

    /* Field 1: UTC time */
    field = nmea_field(sentence, 1);
    if (*field >= '0') {
        gps_data.hour   = (uint8_t)parse_int(field, 2);
        gps_data.minute = (uint8_t)parse_int(field + 2, 2);
        gps_data.second = (uint8_t)parse_int(field + 4, 2);
    }

    /* Field 2: Status A=active V=void */
    field = nmea_field(sentence, 2);
    if (*field == 'A') {
        /* Field 3,4: Latitude */
        field = nmea_field(sentence, 3);
        if (*field >= '0') {
            gps_data.latitude = nmea_to_degrees(field);
            field = nmea_field(sentence, 4);
            if (*field == 'S') gps_data.latitude = -gps_data.latitude;
        }

        /* Field 5,6: Longitude */
        field = nmea_field(sentence, 5);
        if (*field >= '0') {
            gps_data.longitude = nmea_to_degrees(field);
            field = nmea_field(sentence, 6);
            if (*field == 'W') gps_data.longitude = -gps_data.longitude;
        }

        if (gps_data.fix_quality == 0)
            gps_data.fix_quality = 1;

        /* Field 9: Date ddmmyy (needed for satellite time sync) */
        field = nmea_field(sentence, 9);
        if (field[0] >= '0' && field[0] <= '9' && field[5] >= '0' && field[5] <= '9') {
            uint8_t d = (uint8_t)parse_int(field, 2);
            uint8_t m = (uint8_t)parse_int(field + 2, 2);
            uint8_t y = (uint8_t)parse_int(field + 4, 2);
            if (d >= 1 && d <= 31 && m >= 1 && m <= 12) {
                gps_data.day   = d;
                gps_data.month = m;
                gps_data.year  = (uint16_t)(2000U + y);
                gps_data.rmc_valid = 1;
                gps_data.rmc_seq++;
            }
        }
    } else {
        gps_data.fix_quality = 0;
        gps_data.rmc_valid = 0;
    }
}

/* Process a complete NMEA sentence */
static void process_nmea(const char *sentence)
{
    if (strncmp(sentence, "$GPGGA", 6) == 0 ||
        strncmp(sentence, "$GNGGA", 6) == 0) {
        parse_gga(sentence);
    } else if (strncmp(sentence, "$GPRMC", 6) == 0 ||
               strncmp(sentence, "$GNRMC", 6) == 0) {
        parse_rmc(sentence);
    }
}

/* Public API ----------------------------------------------------------- */

/*
 * GPS module power control via PA8 (GPS_ENABLE).
 * OEM V0.27: SET @ 0x080095A6 (via gpio_bits_set GPIOA 0x100)
 *            CLR @ 0x08013FE0 (via gpio_bits_reset GPIOA 0x100)
 * PA8 is configured as GPIO output by gpio_modes_init @ 0x0801391C.
 */
void gps_enable(void)
{
    gpio_set_pin(GPS_ENABLE_PORT, GPS_ENABLE_PIN);   /* PA8 HIGH = GPS on */
}

void gps_disable(void)
{
    gpio_clear_pin(GPS_ENABLE_PORT, GPS_ENABLE_PIN); /* PA8 LOW = GPS off */
}

void gps_init(void)
{
    memset((void *)&gps_data, 0, sizeof(gps_data));
    rx_head = 0;
    rx_tail = 0;
    nmea_idx = 0;
    nmea_receiving = 0;

    /*
     * Configure PA8 as GPIO output for GPS power control.
     * OEM: gpio_modes_init @ 0x0801391C configures PA8 as output.
     * Enable GPIOA clock (may already be on from system init).
     */
    CRM->APB2EN |= CRM_APB2EN_IOPAEN;
    gpio_config_pin(GPS_ENABLE_PORT, GPS_ENABLE_PIN,
                    GPIO_MODE_OUT_2MHZ, GPIO_CNF_PP);
    gps_enable();   /* Power on GPS module */

    uart_gps_init();  /* USART3 @ 9600 baud */
}

void gps_process(void)
{
    /* Drain the ring buffer */
    while (rx_head != rx_tail) {
        uint8_t ch = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % GPS_BUF_SIZE;

        if (ch == '$') {
            nmea_idx = 0;
            nmea_receiving = 1;
            nmea_line[nmea_idx++] = (char)ch;
        } else if (nmea_receiving) {
            if (ch == '\r' || ch == '\n') {
                nmea_line[nmea_idx] = '\0';
                nmea_receiving = 0;
                if (nmea_idx > 6)
                    process_nmea(nmea_line);
            } else if (nmea_idx < NMEA_MAX_LEN - 1) {
                nmea_line[nmea_idx++] = (char)ch;
            } else {
                nmea_receiving = 0;  /* overflow - discard */
            }
        }
    }
}

const gps_data_t *gps_get_data(void)
{
    return &gps_data;
}

/* USART3 IRQ handler (called from vector table) ------------------------ */

void USART3_IRQHandler(void)
{
    if (USART3->SR & USART_SR_RXNE) {
        uint8_t ch = (uint8_t)(USART3->DR & 0xFF);
        uint16_t next = (rx_head + 1) % GPS_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = ch;
            rx_head = next;
        }
    }
}
