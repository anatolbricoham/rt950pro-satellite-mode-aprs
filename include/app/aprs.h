/*
 * aprs.h - APRS via BK4829 hardware AFSK for the RT-950 Pro
 *
 * CRITICAL DISCOVERY: The BK4829 transceiver handles ALL Bell 202 AFSK
 * modulation and demodulation in hardware. The MCU's role is:
 *   1. Build AX.25 frame in RAM (MIC-E format)
 *   2. Program BK4829 FSK registers
 *   3. Load data into BK4829 FIFO (reg 0x5F)
 *   4. Poll for completion (reg 0x0C bit 0)
 *
 * NO software DDS, sine tables, NRZI, bit stuffing, CRC, DAC, or ADC.
 *
 * OEM firmware functions (V0.27 binary, base 0x08000000):
 * OEM binary labels these as "BK4819" functions (legacy naming - actual chip is BK4829):
 *   APRS_SetPTT        @ fw 0x0800E920
 *   APRS_CheckReady    @ fw 0x0800E9C4
 *   APRS_ReadStatus    @ fw 0x0800E9F0
 *   APRS_StartTX       @ fw 0x0800EA78
 *   APRS_MicE_EncodeDest  @ 0x08028FE4 - Encode lat into dest addr
 *   APRS_MicE_BuildInfo   @ 0x0800AC2C - Build MIC-E info field
 *   APRS_TX_Trigger       @ 0x0800C97C - Build frame, trigger TX
 *
 * Corrected function names:
 *   0x0800F354 -> APRS_Display_StatusBar (NOT APRS_Modem_CheckReady)
 *   0x0800F408 -> APRS_Display_TXPacket  (NOT APRS_Modem_StartTX)
 *   0x0800B024 -> bk4829_init mid-entry  (NOT Modem_Scramble_Data)
 *   0x0800DCA0 -> GPS_CoordConvert       (NOT AudioDMA_Trigger)
 */

#ifndef APP_APRS_H
#define APP_APRS_H

#include <stdint.h>

/* BK4829 FSK register values (from firmware @ 0x0801EB8C) ----------- */
#define BK_FSK_REG58_TX     0x00C9  /* TX mode, baud divider ~1200 */
#define BK_FSK_REG58_RX     0x3FC3  /* RX correlator mode */
#define BK_FSK_REG70_TX     0x00AC  /* AF mod: AFSK TX */
#define BK_FSK_REG70_RX     0x00EC  /* AF demod: AFSK RX */
#define BK_FSK_REG72_TX     0x60CA  /* Mark=1200Hz, Space=2200Hz */
#define BK_FSK_REG72_RX     0x3065  /* RX bandpass filters */
#define BK_FSK_REG5C_TX     0x5665  /* TX deviation */
#define BK_FSK_REG5C_RX     0xA730  /* RX filter config */
#define BK_FSK_REG5D_TX     0x0F00  /* TX preamble timing */
#define BK_FSK_REG5D_RX     0x0D00  /* RX timing */
#define BK_FSK_SYNC_LO      0xFB72  /* Sync word bytes [1:0] */
#define BK_FSK_SYNC_HI      0x4099  /* Sync word bytes [3:2] */

/* APRS constants ---------------------------------------------------- */
#define APRS_MAX_FRAME       128    /* Max AX.25 frame bytes */
#define APRS_MAX_COMMENT      64    /* Max comment string */
#define APRS_FIFO_WORDS        7    /* BK4829 FIFO depth (14 bytes) */
#define APRS_TX_POLL_MS        5    /* Polling interval during TX */
#define APRS_TX_TIMEOUT_MS  1000    /* TX completion timeout */
#define APRS_PREAMBLE_MS      20    /* Preamble delay before data */

/* APRS symbol defaults (from firmware @ 0x0800AE60) ----------------- */
#define APRS_SYM_JOGGER     '['     /* Index 0 */
#define APRS_SYM_BICYCLE    'b'     /* Index 1 */
#define APRS_SYM_CAR        '>'     /* Index 2 */
#define APRS_SYM_RV         'R'     /* Index 3 */
#define APRS_SYM_DEFAULT    'K'     /* Fallback: SUV */
#define APRS_SYM_TABLE_PRI  '/'     /* Primary symbol table */

/* Modem state ------------------------------------------------------- */
typedef enum {
    APRS_IDLE = 0,
    APRS_TX_KEYING,         /* PTT asserted, waiting TX delay */
    APRS_TX_SENDING,        /* BK4829 FSK transmitting */
    APRS_TX_COMPLETE,       /* TX done, cleanup */
    APRS_RX_LISTENING,      /* BK4829 FSK RX active */
} aprs_state_t;

/* Digipeater path modes (from firmware) ----------------------------- */
typedef enum {
    APRS_PATH_NONE = 0,     /* Direct, no digipeater */
    APRS_PATH_WIDE1_1,      /* WIDE1-1 only */
    APRS_PATH_WIDE1_WIDE2,  /* WIDE1-1, WIDE2-1 */
    APRS_PATH_CUSTOM,       /* User-defined paths */
} aprs_path_mode_t;

/* Beacon TX mode ----------------------------------------------------- */
typedef enum {
    APRS_BEACON_MANUAL = 0,     /* Manual trigger only */
    APRS_BEACON_TIMED,          /* Periodic timer */
} aprs_beacon_mode_t;

/* MIC-E message types ----------------------------------------------- */
typedef enum {
    MICE_OFF_DUTY = 0, MICE_EN_ROUTE, MICE_IN_SERVICE, MICE_RETURNING,
    MICE_COMMITTED, MICE_SPECIAL, MICE_PRIORITY, MICE_EMERGENCY,
} mice_msg_type_t;

/* Display unit preferences ------------------------------------------ */
typedef enum { UNIT_DMS = 0, UNIT_DECIMAL } latlon_unit_t;
typedef enum { UNIT_MPH = 0, UNIT_KMH, UNIT_KNOTS } speed_unit_t;
typedef enum { UNIT_MILES = 0, UNIT_KM } distance_unit_t;
typedef enum { UNIT_FEET = 0, UNIT_METERS } altitude_unit_t;

/* APRS configuration (mirrors CPS flash layout 0x00FFFF-0x01007F) - */
typedef struct {
    /* Block 1: basic (flash 0x00FFFF, 16 bytes) */
    uint8_t  enable;            /* APRS on/off */
    uint8_t  gps_source;        /* 0=manual, 1=GPS module */
    latlon_unit_t latlon_unit;  /* Lat/Lon display format */
    speed_unit_t  speed_unit;   /* Speed display unit */
    distance_unit_t dist_unit;  /* Distance display unit */
    altitude_unit_t alt_unit;   /* Altitude display unit */
    int8_t   timezone;          /* UTC offset, -12..+14 */
    /* Manual position (used when gps_source == 0) */
    uint8_t  lat_ns;            /* 'N' or 'S' */
    uint8_t  lat_deg;           /* Degrees */
    uint8_t  lat_min;           /* Minutes */
    uint8_t  lat_frac;          /* Fractional minutes (0-99) */
    uint8_t  lon_ew;            /* 'E' or 'W' */
    uint8_t  lon_deg;           /* Degrees */
    uint8_t  lon_min;           /* Minutes */
    uint8_t  lon_frac;          /* Fractional minutes (0-99) */
    uint16_t altitude;          /* Meters / 10 */

    /* Block 2: identity & routing (flash 0x01000F, 16 bytes) */
    char     callsign[7];       /* Source callsign (6 + null) */
    uint8_t  ssid;              /* SSID 0-15 */
    aprs_path_mode_t path_mode; /* Digi path mode */
    uint8_t  symbol_idx;        /* <4=lookup table, >=4=custom */
    char     symbol_table;      /* '/' primary or '\\' alternate */
    char     symbol_code;       /* APRS symbol ASCII character */
    uint8_t  tx_priority;       /* TX priority flag */

    /* Block 3: beacon & protocol (flash 0x01001F, 16 bytes) */
    aprs_beacon_mode_t beacon_mode; /* Manual or timed */
    uint8_t  beacon_interval;   /* Timed interval index (30s,1m,2m,3m,5m,10m,15m,30m) */
    uint8_t  tx_delay;          /* Pre-TX delay, units of 10ms */
    mice_msg_type_t mic_e_type; /* MIC-E message type 0-7 */
    uint8_t  tnc_data_type;     /* 0=APRS, 1=GPS */
    uint8_t  tx_data_reporting; /* Enable TX data logging */
    uint8_t  decode_prompt;     /* Beep on APRS RX decode */
    uint8_t  rx_popup;          /* Auto-popup decoded packets */
    uint8_t  popup_time;        /* Popup display time (seconds) */
    uint8_t  fwd_channel;       /* Forward-to channel number */
    uint8_t  fwd_routing;       /* Forward routing path mode */
    uint8_t  wait_forward;      /* Wait before forwarding (s) */

    /* Block 4: custom routing (flash 0x01002F, 16 bytes) */
    char     custom_path1[7];   /* Digi 1 callsign */
    uint8_t  digi1_ssid;        /* Digi 1 SSID */
    char     custom_path2[7];   /* Digi 2 callsign */
    uint8_t  digi2_ssid;        /* Digi 2 SSID */

    /* Block 5: custom message (flash 0x01003F, 48 bytes) */
    uint8_t  comment_enable;    /* Append comment flag */
    char     comment[APRS_MAX_COMMENT]; /* Comment/status text */
} aprs_config_t;

/* Decoded APRS packet (output of RX parser) ------------------------ */
#define APRS_CALL_MAX   10      /* Callsign + SSID string */
#define APRS_PATH_MAX   64      /* Full digipeater path string */
#define APRS_COMMENT_MAX 48     /* Decoded comment string */

typedef struct {
    char     src_call[APRS_CALL_MAX];   /* Source callsign-SSID */
    char     dst_call[APRS_CALL_MAX];   /* Destination address */
    char     path[APRS_PATH_MAX];       /* Digipeater path */
    /* Decoded position */
    uint8_t  has_position;      /* 1 if position decoded */
    int32_t  lat_deg1e5;        /* Latitude x 100000 (signed) */
    int32_t  lon_deg1e5;        /* Longitude x 100000 (signed) */
    uint16_t altitude;          /* Meters (0 if unknown) */
    uint16_t speed;             /* Knots x 10 */
    uint16_t course;            /* Degrees */
    char     symbol_table;      /* '/' or '\\' */
    char     symbol_code;       /* Symbol character */
    mice_msg_type_t mic_e_msg;  /* MIC-E message type */
    /* Raw comment/status */
    char     comment[APRS_COMMENT_MAX];
    uint8_t  valid;             /* CRC passed */
    /* Raw information field (for APRS messages, see aprs_msg.h) */
    uint8_t  info_raw[100];
    uint8_t  info_len;
} aprs_packet_t;

/* APRS position (from GPS or manual) -------------------------------- */
typedef struct {
    uint8_t  lat_deg;       /* Latitude degrees */
    uint8_t  lat_min;       /* Latitude minutes */
    uint8_t  lat_frac;      /* Latitude fractional minutes */
    uint8_t  lat_ns;        /* 'N' or 'S' */
    uint8_t  lon_deg;       /* Longitude degrees */
    uint8_t  lon_min;       /* Longitude minutes */
    uint8_t  lon_frac;      /* Longitude fractional minutes */
    uint8_t  lon_ew;        /* 'E' or 'W' */
    uint16_t speed;         /* Speed in knots x 10 */
    uint16_t course;        /* Course in degrees */
    uint16_t altitude;      /* Altitude in meters / 10 */
    uint8_t  valid;         /* GPS fix valid */
} aprs_position_t;

/* API --------------------------------------------------------------- */

/* Load APRS config from SPI flash (CPS-programmed addresses) */
void aprs_config_load(aprs_config_t *cfg);

/* Save runtime-modified config back to flash */
void aprs_config_save(const aprs_config_t *cfg);

/* Get pointer to the active (runtime) APRS config */
const aprs_config_t *aprs_config_get(void);

/* Initialize APRS subsystem - loads config from flash */
void aprs_init(void);

/* Check if APRS is idle and ready for a new transmission */
int aprs_is_ready(void);

/* Get current APRS state machine state */
aprs_state_t aprs_get_state(void);

/*
 * Transmit an APRS MIC-E position report.
 * Uses BK4829 hardware AFSK - builds frame, programs FSK, loads FIFO.
 * Blocking: waits for BK4829 to complete (up to APRS_TX_TIMEOUT_MS).
 * Returns 0 on success, -1 on error.
 */
int aprs_send_position(const aprs_position_t *pos);

/*
 * Transmit an arbitrary AX.25 UI frame (without FCS; it is appended here).
 * Keys the transmitter, sends through the BK4829 AFSK FIFO, unkeys.
 * Returns 0 on success, -1 if busy/error.
 */
int aprs_send_frame(const uint8_t *frame, uint16_t len);

/* Enable BK4829 AFSK RX mode for incoming APRS packets */
void aprs_rx_start(void);

/* Stop BK4829 AFSK mode (TX or RX), restore normal radio */
void aprs_stop(void);

/* Poll for received APRS data and decode. Returns 1 if packet decoded. */
int aprs_rx_poll_decode(aprs_packet_t *pkt);

/* Get the last decoded RX packet (valid until next decode) */
const aprs_packet_t *aprs_rx_last_packet(void);

/* Process APRS state machine + beacon timer (call from main loop @ 1Hz) */
void aprs_poll(void);

/* Reset beacon timer countdown (e.g. after manual TX) */
void aprs_beacon_reset(void);

/* Beacon interval lookup table: index -> seconds */
uint16_t aprs_beacon_interval_sec(uint8_t idx);

/* MIC-E encoding helpers -------------------------------------------- */

/* Encode latitude + message bits into AX.25 destination address */
void mice_encode_dest(uint8_t *dest, const aprs_position_t *pos, uint8_t msg_type);

/* Build MIC-E information field (lon, speed, course, symbol, alt) */
uint16_t mice_build_info(uint8_t *buf, uint16_t max_len,
                         const aprs_position_t *pos,
                         const aprs_config_t *cfg);

#endif /* APP_APRS_H */
