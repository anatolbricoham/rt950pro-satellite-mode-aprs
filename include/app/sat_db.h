/*
 * sat_db.h - Satellite database binary format (SPI flash + PC tool)
 *
 * This layout is shared byte-for-byte with tools/rt950_sat.py (pack_db()).
 * Everything is little-endian, naturally aligned, no compiler padding.
 *
 * SPI flash map (custom firmware only, outside every OEM/CPS region):
 *
 *   0x0C0000 - 0x0C7FFF  SAT_DB   (32 KB) header + satellites + pass table
 *   0x0C8000 - 0x0C8FFF  SAT_CFG  ( 4 KB) user settings for satellite mode
 *
 * The OEM splash image ends at 0x0B5800 and the first OEM font starts at
 * 0x15C000, so this window is unused by both the OEM and custom firmware.
 *
 *   +--------------------+  0x0000
 *   | sat_db_header_t    |  64 bytes
 *   +--------------------+  0x0040
 *   | sat_db_sat_t[n]    |  n * 128 bytes   (n <= SAT_DB_MAX_SATS)
 *   +--------------------+
 *   | sat_db_pass_t[m]   |  m * 16 bytes    (m <= SAT_DB_MAX_PASSES)
 *   +--------------------+
 *
 * payload_crc32 covers every byte after the header (satellites + passes).
 * header_crc32 covers header bytes 0..59.  CRC-32/ISO-HDLC (zlib.crc32).
 */

#ifndef APP_SAT_DB_H
#define APP_SAT_DB_H

#include <stdint.h>

#define SAT_DB_FLASH_ADDR     0x0C0000UL
#define SAT_DB_FLASH_SIZE     0x008000UL     /* 32 KB, 8 x 4 KB sectors */
#define SAT_CFG_FLASH_ADDR    0x0C8000UL
#define SAT_CFG_FLASH_SIZE    0x001000UL

#define SAT_DB_MAGIC          0x44544153UL   /* "SATD" little-endian */
#define SAT_DB_VERSION        1
#define SAT_DB_MAX_SATS       48
#define SAT_DB_MAX_PASSES     1024
#define SAT_DB_NAME_LEN       12
#define SAT_DB_INFO_LEN       24

/* Transponder / operating mode of a satellite record */
typedef enum {
    SAT_MODE_FM          = 0,   /* FM voice repeater (uplink + downlink)   */
    SAT_MODE_FM_DATA     = 1,   /* FM packet / APRS digipeater             */
    SAT_MODE_LIN_INV     = 2,   /* Linear transponder, inverting (SSB/CW)  */
    SAT_MODE_LIN_NONINV  = 3,   /* Linear transponder, non-inverting       */
    SAT_MODE_RX_ONLY     = 4,   /* Beacon / telemetry / SSTV, listen only  */
    SAT_MODE_COUNT
} sat_mode_t;

/* sat_db_sat_t.flags */
#define SAT_FLAG_ENABLED      0x01
#define SAT_FLAG_FAVORITE     0x02
#define SAT_FLAG_DEEP_SPACE   0x04   /* period >= 225 min: not supported   */
#define SAT_FLAG_SCHEDULED    0x08   /* transponder only on published schedule */
#define SAT_FLAG_SUNLIT_ONLY  0x10   /* do not use in eclipse (e.g. AO-91) */

/* sat_db_header_t.flags */
#define SAT_HDR_FLAG_HAS_QTH  0x01

typedef struct {
    uint32_t magic;            /*  0  "SATD"                                */
    uint16_t version;          /*  4  SAT_DB_VERSION                        */
    uint16_t header_size;      /*  6  sizeof(sat_db_header_t) = 64          */
    uint16_t sat_count;        /*  8                                        */
    uint16_t sat_rec_size;     /* 10  sizeof(sat_db_sat_t) = 128            */
    uint16_t pass_count;       /* 12                                        */
    uint16_t pass_rec_size;    /* 14  sizeof(sat_db_pass_t) = 16            */
    uint32_t created_unix;     /* 16  UTC seconds when the file was built   */
    int32_t  qth_lat_e6;       /* 20  QTH used for pass table, deg * 1e6    */
    int32_t  qth_lon_e6;       /* 24  east positive                         */
    int16_t  qth_alt_m;        /* 28                                        */
    int8_t   min_el_deg;       /* 30  horizon mask used for pass table      */
    uint8_t  flags;            /* 31  SAT_HDR_FLAG_*                        */
    uint32_t pass_start_unix;  /* 32  pass table window                     */
    uint32_t pass_end_unix;    /* 36                                        */
    char     locator[8];       /* 40  Maidenhead locator, NUL padded        */
    uint32_t payload_crc32;    /* 48                                        */
    uint8_t  reserved[8];      /* 52                                        */
    uint32_t header_crc32;     /* 60  CRC over bytes 0..59                  */
} sat_db_header_t;             /* 64 bytes */

typedef struct {
    char     name[SAT_DB_NAME_LEN]; /*   0 display name, space/NUL padded   */
    uint32_t norad;            /*  12  catalog number                       */
    /* Mean elements (TLE, SGP4 / WGS-72) */
    double   epoch_jd;         /*  16  epoch as Julian date (UTC)           */
    double   bstar;            /*  24  1/earth radii                        */
    double   incl_deg;         /*  32                                       */
    double   raan_deg;         /*  40                                       */
    double   ecc;              /*  48                                       */
    double   argp_deg;         /*  56                                       */
    double   mean_anom_deg;    /*  64                                       */
    double   mean_motion_rpd;  /*  72  revolutions per day                  */
    /* Radio */
    uint32_t downlink_hz;      /*  80  0 = none                             */
    uint32_t uplink_hz;        /*  84  0 = none (RX only)                   */
    uint16_t ctcss_up_dhz;     /*  88  uplink CTCSS, 0.1 Hz units, 0 = off  */
    uint16_t ctcss_down_dhz;   /*  90  downlink CTCSS (RX squelch), 0 = off */
    uint16_t arm_tone_dhz;     /*  92  arming tone (SO-50: 744), 0 = none   */
    uint8_t  mode;             /*  94  sat_mode_t                           */
    uint8_t  flags;            /*  95  SAT_FLAG_*                           */
    uint32_t passband_hz;      /*  96  linear transponder width             */
    char     info[SAT_DB_INFO_LEN]; /* 100 free text, e.g. "Arm 74.4 2s"    */
    uint8_t  reserved[4];      /* 124                                       */
} sat_db_sat_t;                /* 128 bytes */

typedef struct {
    uint32_t aos_unix;         /*  0                                        */
    uint16_t duration_s;       /*  4  AOS -> LOS                            */
    uint16_t tca_offset_s;     /*  6  AOS -> TCA                            */
    uint8_t  sat_index;        /*  8  index into the satellite table        */
    uint8_t  max_el_deg;       /*  9                                        */
    uint16_t aos_az_deg;       /* 10                                        */
    uint16_t los_az_deg;       /* 12                                        */
    uint16_t tca_az_deg;       /* 14                                        */
} sat_db_pass_t;               /* 16 bytes */

/* Compile-time layout checks (C11) */
_Static_assert(sizeof(sat_db_header_t) == 64,  "sat_db_header_t must be 64 bytes");
_Static_assert(sizeof(sat_db_sat_t)    == 128, "sat_db_sat_t must be 128 bytes");
_Static_assert(sizeof(sat_db_pass_t)   == 16,  "sat_db_pass_t must be 16 bytes");
_Static_assert(64 + SAT_DB_MAX_SATS * 128 + SAT_DB_MAX_PASSES * 16 <= SAT_DB_FLASH_SIZE,
               "SAT_DB does not fit its flash window");

/* User configuration block stored at SAT_CFG_FLASH_ADDR */
#define SAT_CFG_MAGIC         0x47464353UL   /* "SCFG" */
#define SAT_CFG_VERSION       1

typedef enum {
    SAT_LOC_AUTO   = 0,   /* GPS if it has a fix, otherwise DB QTH / manual */
    SAT_LOC_GPS    = 1,   /* GPS only                                      */
    SAT_LOC_MANUAL = 2,   /* manual / DB QTH only                          */
} sat_loc_src_t;

typedef enum {
    SAT_TX_SAME_VFO = 0,  /* split on one chip (OpenGD77 style, safest)    */
    SAT_TX_VFO_B    = 1,  /* RX on VFO A chip, TX on VFO B chip            */
} sat_tx_mode_t;

typedef struct {
    uint32_t magic;            /* SAT_CFG_MAGIC                             */
    uint8_t  version;
    uint8_t  selected;         /* last tracked satellite index              */
    int8_t   min_el_deg;       /* horizon mask for on-radio prediction      */
    uint8_t  doppler_on;       /* 1 = correct RX/TX for Doppler             */
    uint8_t  tx_mode;          /* sat_tx_mode_t                             */
    uint8_t  loc_src;          /* sat_loc_src_t                             */
    uint8_t  alert_min;        /* AOS alert beep, minutes before (0 = off)  */
    uint8_t  rx_wide;          /* 1 = 25 kHz RX bandwidth, 0 = 12.5 kHz     */
    int32_t  manual_lat_e6;
    int32_t  manual_lon_e6;
    int16_t  manual_alt_m;
    uint8_t  manual_valid;
    uint8_t  reserved[9];
    uint32_t crc32;            /* CRC over bytes 0..31                      */
} sat_cfg_t;                   /* 36 bytes */

_Static_assert(sizeof(sat_cfg_t) == 36, "sat_cfg_t must be 36 bytes");

#endif /* APP_SAT_DB_H */
