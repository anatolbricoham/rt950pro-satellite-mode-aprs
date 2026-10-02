/*
 * aprs_msg.h - APRS text messaging (APRS 1.0.1 chapter 14) for the RT-950 Pro
 *
 *   :ADDRESSEE:message text{12345     message with id (ack requested)
 *   :ADDRESSEE:ack12345                acknowledgement
 *   :ADDRESSEE:rej12345                reject
 *   :BLN1     :bulletin text           bulletin / announcement
 *
 * Split in two layers:
 *   - codec (aprs_msg_codec.c): pure formatting/parsing + AX.25 UI frame
 *     building and FCS.  No hardware access, verified on the host.
 *   - engine (aprs_msg.c): inbox/outbox, ids, retries, automatic acks,
 *     TX through the BK4829 AFSK path of aprs.c, RX hook.
 *   - UI (aprs_msg_ui.c): inbox, message view, compose (multi-tap).
 */

#ifndef APP_APRS_MSG_H
#define APP_APRS_MSG_H

#include <stdint.h>

#define APRS_MSG_ADDR_LEN    9      /* addressee field, space padded      */
#define APRS_MSG_TEXT_MAX    67     /* max message text (spec)            */
#define APRS_MSG_ID_MAX      5      /* message number, 1-5 alphanumerics  */
#define APRS_MSG_CALL_MAX    10     /* "EA7ABC-15" + NUL                  */
#define APRS_MSG_INBOX       24     /* messages kept in RAM               */
#define APRS_MSG_TOCALL      "APZ950"   /* experimental tocall (APZxxx)   */

/* ---------------------------------------------------------------- codec */

typedef enum {
    APRS_MT_NONE = 0,       /* not a message frame                       */
    APRS_MT_MESSAGE,        /* text (id optional)                        */
    APRS_MT_ACK,
    APRS_MT_REJ,
    APRS_MT_BULLETIN,       /* addressee BLNx / BLNxxxxx                 */
} aprs_msg_type_t;

typedef struct {
    aprs_msg_type_t type;
    char addressee[APRS_MSG_CALL_MAX];          /* trimmed               */
    char text[APRS_MSG_TEXT_MAX + 1];
    char id[APRS_MSG_ID_MAX + 1];               /* "" if none            */
} aprs_msg_fields_t;

/* ":TO       :text{id" -> returns length (without NUL) or -1 */
int aprs_msg_format(char *out, uint16_t max, const char *to,
                    const char *text, const char *id);
/* ":TO       :ack<id>" / "rej<id>" */
int aprs_msg_format_ack(char *out, uint16_t max, const char *to,
                        const char *id, uint8_t reject);
/* Parse an APRS information field. Returns the type. */
aprs_msg_type_t aprs_msg_parse(const uint8_t *info, uint16_t len,
                               aprs_msg_fields_t *out);
/* "EA7ABC-7" vs my call "EA7ABC" + ssid 7 (case-insensitive, -0 == none) */
uint8_t aprs_msg_is_for_me(const char *addressee, const char *mycall, uint8_t myssid);

/* AX.25 UI frame (without FCS). digis may be NULL when ndigi == 0.
 * Callsigns are "CALL" or "CALL-SSID". Returns length or 0 on error. */
uint16_t ax25_build_ui(uint8_t *frame, uint16_t max, const char *dest,
                       const char *src, const char *const *digis,
                       uint8_t ndigi, const uint8_t *info, uint16_t info_len);
/* AX.25 FCS (CRC-16/X.25), to be appended little-endian */
uint16_t ax25_fcs(const uint8_t *data, uint16_t len);

/* --------------------------------------------------------------- engine */

#define APRS_MF_INCOMING  0x01
#define APRS_MF_UNREAD    0x02
#define APRS_MF_ACKED     0x04      /* outgoing: ack received             */
#define APRS_MF_FAILED    0x08      /* outgoing: retries exhausted        */
#define APRS_MF_BULLETIN  0x10
#define APRS_MF_FOR_ME    0x20      /* incoming addressed to my call      */

typedef struct {
    char     peer[APRS_MSG_CALL_MAX];   /* from (incoming) / to (outgoing) */
    char     text[APRS_MSG_TEXT_MAX + 1];
    char     id[APRS_MSG_ID_MAX + 1];
    uint32_t tick;                      /* get_tick() when stored         */
    uint8_t  flags;                     /* APRS_MF_*                      */
    uint8_t  tries;                     /* outgoing transmissions so far  */
} aprs_msg_t;

typedef struct {
    uint32_t magic;                     /* "AMSG"                          */
    uint8_t  rx_enable;                 /* keep the AFSK demodulator on    */
    uint8_t  auto_ack;                  /* answer messages for me with ack */
    uint8_t  retries;                   /* 0..5 extra transmissions        */
    uint8_t  show_all;                  /* list messages not for me too    */
    uint32_t crc32;
} aprs_msg_cfg_t;

#define APRS_MSG_CFG_FLASH_ADDR  0x0C9000UL   /* after SAT_CFG */

void     aprs_msg_init(void);
void     aprs_msg_poll(void);           /* every 100 ms                   */
int      aprs_msg_send(const char *to, const char *text);   /* 0 = queued */
uint8_t  aprs_msg_count(void);
const aprs_msg_t *aprs_msg_get(uint8_t i);      /* 0 = newest              */
void     aprs_msg_mark_read(uint8_t i);
uint8_t  aprs_msg_unread(void);
void     aprs_msg_clear(void);
const aprs_msg_cfg_t *aprs_msg_cfg(void);
void     aprs_msg_cfg_set_rx(uint8_t on);
void     aprs_msg_cfg_set_auto_ack(uint8_t on);
void     aprs_msg_cfg_set_retries(uint8_t n);

/* Called by aprs.c for every valid received frame */
void     aprs_msg_on_rx(const char *src, const uint8_t *info, uint16_t len);

/* ------------------------------------------------------------------ UI */
void     aprs_msg_ui_enter(void);
void     aprs_msg_ui_exit(void);
uint8_t  aprs_msg_ui_is_active(void);
void     aprs_msg_ui_handle_key(uint8_t key);
void     aprs_msg_ui_handle_encoder(int8_t dir);
void     aprs_msg_ui_draw(void);

#endif /* APP_APRS_MSG_H */
