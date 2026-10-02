/*
 * aprs_msg.c - APRS messaging engine (inbox, outbox, acks, retries)
 *
 *  - Outgoing messages get a numeric id ("{N") and are retransmitted at
 *    30 s, 60 s, 120 s ... until an ack arrives or `retries` is exhausted.
 *  - Incoming messages addressed to my call (callsign + SSID from the APRS
 *    CPS settings) are stored, beep, and are acked automatically (once per
 *    id: duplicates caused by digipeaters are acked again but not stored).
 *  - Acks for my outgoing ids mark the message as delivered.
 *  - Bulletins (BLNx) are stored; other traffic only if "show all" is on.
 *
 * TX uses aprs_send_frame() (BK4829 AFSK) with the path configured in the
 * radio's APRS settings.  The inbox lives in RAM (lost at power off).
 */

#include "app/aprs_msg.h"
#include "app/aprs.h"
#include "app/audio.h"
#include "app/satellite.h"     /* sat_crc32() */
#include "drivers/spi.h"

#include <stddef.h>
#include <string.h>

extern uint32_t get_tick(void);

static aprs_msg_t     box[APRS_MSG_INBOX];   /* ring, head = newest */
static uint8_t        n_box, head;
static aprs_msg_cfg_t cfg;
static uint32_t       next_id;

/* last seen incoming ids (dedupe digipeated copies) */
#define SEEN_N 8
static struct { char from[APRS_MSG_CALL_MAX]; char id[APRS_MSG_ID_MAX + 1]; } seen[SEEN_N];
static uint8_t seen_pos;

/* pending acks to transmit (small queue) */
#define ACKQ_N 4
static struct { char to[APRS_MSG_CALL_MAX]; char id[APRS_MSG_ID_MAX + 1]; uint32_t due; } ackq[ACKQ_N];
static uint8_t ackq_n;

static size_t slen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

/* ---------------------------------------------------------------- cfg */

static void cfg_save(void)
{
    cfg.magic = 0x47534D41UL;  /* "AMSG" */
    cfg.crc32 = sat_crc32(0, (const uint8_t *)&cfg, offsetof(aprs_msg_cfg_t, crc32));
    spi_flash_erase_4k(APRS_MSG_CFG_FLASH_ADDR);
    spi_flash_write_page(APRS_MSG_CFG_FLASH_ADDR, (const uint8_t *)&cfg, sizeof cfg);
}

static void cfg_load(void)
{
    spi_flash_read(APRS_MSG_CFG_FLASH_ADDR, (uint8_t *)&cfg, sizeof cfg);
    if (cfg.magic != 0x47534D41UL ||
        cfg.crc32 != sat_crc32(0, (const uint8_t *)&cfg, offsetof(aprs_msg_cfg_t, crc32))) {
        memset(&cfg, 0, sizeof cfg);
        cfg.rx_enable = 1;
        cfg.auto_ack = 1;
        cfg.retries = 3;
        cfg.show_all = 0;
    }
}

const aprs_msg_cfg_t *aprs_msg_cfg(void) { return &cfg; }
void aprs_msg_cfg_set_rx(uint8_t on)
{
    cfg.rx_enable = on ? 1 : 0;
    cfg_save();
    if (cfg.rx_enable) aprs_rx_start(); else aprs_stop();
}
void aprs_msg_cfg_set_auto_ack(uint8_t on) { cfg.auto_ack = on ? 1 : 0; cfg_save(); }
void aprs_msg_cfg_set_retries(uint8_t n)   { cfg.retries = (n > 5) ? 5 : n; cfg_save(); }

/* -------------------------------------------------------------- store */

static aprs_msg_t *box_push(void)
{
    head = (uint8_t)((head + APRS_MSG_INBOX - 1) % APRS_MSG_INBOX);
    if (n_box < APRS_MSG_INBOX) n_box++;
    memset(&box[head], 0, sizeof box[head]);
    box[head].tick = get_tick();
    return &box[head];
}

uint8_t aprs_msg_count(void) { return n_box; }

const aprs_msg_t *aprs_msg_get(uint8_t i)
{
    if (i >= n_box) return 0;
    return &box[(head + i) % APRS_MSG_INBOX];
}

void aprs_msg_mark_read(uint8_t i)
{
    if (i < n_box) box[(head + i) % APRS_MSG_INBOX].flags &= (uint8_t)~APRS_MF_UNREAD;
}

uint8_t aprs_msg_unread(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < n_box; i++)
        if (aprs_msg_get(i)->flags & APRS_MF_UNREAD) n++;
    return n;
}

void aprs_msg_clear(void) { n_box = 0; head = 0; }

/* ----------------------------------------------------------------- TX */

static void my_call(char out[APRS_MSG_CALL_MAX])
{
    const aprs_config_t *c = aprs_config_get();
    uint8_t n = 0;
    for (; n < 6 && c->callsign[n] && c->callsign[n] != ' '; n++) out[n] = c->callsign[n];
    if (c->ssid) {
        out[n++] = '-';
        if (c->ssid >= 10) out[n++] = '1';
        out[n++] = (char)('0' + c->ssid % 10);
    }
    out[n] = '\0';
}

static void call_ssid(char *out, const char *base, uint8_t ssid)
{
    uint8_t n = 0;
    for (; n < 6 && base[n] && base[n] != ' '; n++) out[n] = base[n];
    if (ssid) {
        out[n++] = '-';
        if (ssid >= 10) out[n++] = '1';
        out[n++] = (char)('0' + ssid % 10);
    }
    out[n] = '\0';
}

static int transmit_info(const char *info, uint16_t len)
{
    const aprs_config_t *c = aprs_config_get();
    char src[APRS_MSG_CALL_MAX];
    char d1[APRS_MSG_CALL_MAX], d2[APRS_MSG_CALL_MAX];
    const char *digis[2];
    uint8_t nd = 0;

    my_call(src);
    if (!src[0]) return -1;                    /* no callsign programmed */
    switch (c->path_mode) {
    case APRS_PATH_WIDE1_1:     digis[nd++] = "WIDE1-1"; break;
    case APRS_PATH_WIDE1_WIDE2: digis[nd++] = "WIDE1-1"; digis[nd++] = "WIDE2-1"; break;
    case APRS_PATH_CUSTOM:
        if (c->custom_path1[0]) { call_ssid(d1, c->custom_path1, c->digi1_ssid); digis[nd++] = d1; }
        if (c->custom_path2[0]) { call_ssid(d2, c->custom_path2, c->digi2_ssid); digis[nd++] = d2; }
        break;
    default: break;
    }
    uint8_t frame[APRS_MAX_FRAME];
    uint16_t fl = ax25_build_ui(frame, sizeof frame - 2, APRS_MSG_TOCALL, src,
                                digis, nd, (const uint8_t *)info, len);
    if (!fl) return -1;
    return aprs_send_frame(frame, fl);
}

static int send_msg_now(aprs_msg_t *m)
{
    char info[1 + APRS_MSG_ADDR_LEN + 1 + APRS_MSG_TEXT_MAX + 1 + APRS_MSG_ID_MAX + 1];
    int l = aprs_msg_format(info, sizeof info, m->peer, m->text, m->id);
    if (l < 0) return -1;
    m->tries++;
    m->tick = get_tick();
    return transmit_info(info, (uint16_t)l);
}

int aprs_msg_send(const char *to, const char *text)
{
    char tmp[1 + APRS_MSG_ADDR_LEN + 1 + APRS_MSG_TEXT_MAX + 7];
    if (aprs_msg_format(tmp, sizeof tmp, to, text, "1") < 0) return -1;  /* validate */
    aprs_msg_t *m = box_push();
    uint8_t n = 0;
    for (; to[n] && n < APRS_MSG_CALL_MAX - 1; n++)
        m->peer[n] = (to[n] >= 'a' && to[n] <= 'z') ? (char)(to[n] - 32) : to[n];
    m->peer[n] = '\0';
    strncpy(m->text, text, APRS_MSG_TEXT_MAX);
    if (++next_id > 99999U) next_id = 1;
    uint32_t v = next_id; char d[6]; uint8_t k = 0;
    do { d[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (uint8_t i = 0; i < k; i++) m->id[i] = d[k - 1 - i];
    m->id[k] = '\0';
    m->flags = 0;
    m->tries = 0;
    return send_msg_now(m) < 0 ? -2 : 0;      /* -2: stored, TX failed (retried) */
}

/* ----------------------------------------------------------------- RX */

static uint8_t seen_before(const char *from, const char *id)
{
    for (uint8_t i = 0; i < SEEN_N; i++)
        if (seen[i].id[0] && !strcmp(seen[i].from, from) && !strcmp(seen[i].id, id))
            return 1;
    memset(&seen[seen_pos], 0, sizeof seen[seen_pos]);
    memcpy(seen[seen_pos].from, from, slen(from, APRS_MSG_CALL_MAX - 1));
    memcpy(seen[seen_pos].id, id, slen(id, APRS_MSG_ID_MAX));
    seen_pos = (uint8_t)((seen_pos + 1) % SEEN_N);
    return 0;
}

/* "EA7ABC" == "ea7abc-0", "EA7ABC-7" != "EA7ABC" */
static uint8_t same_station(const char *a, const char *b)
{
    char tmp[APRS_MSG_CALL_MAX];
    uint8_t n = 0, ssid = 0;
    while (b[n] && b[n] != '-' && n < 6) { tmp[n] = b[n]; n++; }
    tmp[n] = '\0';
    const char *dash = strchr(b, '-');
    if (dash) while (*++dash >= '0' && *dash <= '9') ssid = (uint8_t)(ssid * 10 + (*dash - '0'));
    return aprs_msg_is_for_me(a, tmp, ssid);
}

static void queue_ack(const char *to, const char *id)
{
    if (ackq_n >= ACKQ_N) return;
    memset(&ackq[ackq_n], 0, sizeof ackq[ackq_n]);
    memcpy(ackq[ackq_n].to, to, slen(to, APRS_MSG_CALL_MAX - 1));
    memcpy(ackq[ackq_n].id, id, slen(id, APRS_MSG_ID_MAX));
    /* small pseudo-random delay (1.5-3.0 s) so we do not collide with
     * the digipeated copy of the message */
    ackq[ackq_n].due = get_tick() + 1500U + (get_tick() % 1500U);
    ackq_n++;
}

void aprs_msg_on_rx(const char *src, const uint8_t *info, uint16_t len)
{
    aprs_msg_fields_t f;
    aprs_msg_type_t t = aprs_msg_parse(info, len, &f);
    if (t == APRS_MT_NONE) return;

    const aprs_config_t *c = aprs_config_get();
    uint8_t for_me = aprs_msg_is_for_me(f.addressee, c->callsign, c->ssid);

    if (t == APRS_MT_ACK || t == APRS_MT_REJ) {
        if (!for_me) return;
        for (uint8_t i = 0; i < n_box; i++) {
            aprs_msg_t *m = &box[(head + i) % APRS_MSG_INBOX];
            if ((m->flags & APRS_MF_INCOMING) || strcmp(m->id, f.id) != 0)
                continue;
            if (!same_station(src, m->peer))
                continue;
            m->flags |= (t == APRS_MT_ACK) ? APRS_MF_ACKED : APRS_MF_FAILED;
            audio_beep_freq(20000, 60);
            return;
        }
        return;
    }

    if (t == APRS_MT_MESSAGE && for_me && f.id[0] && cfg.auto_ack)
        queue_ack(src, f.id);
    if (t == APRS_MT_MESSAGE && f.id[0] && seen_before(src, f.id))
        return;                                  /* duplicate copy */

    if (!(for_me || t == APRS_MT_BULLETIN || cfg.show_all))
        return;

    aprs_msg_t *m = box_push();
    strncpy(m->peer, src, APRS_MSG_CALL_MAX - 1);
    memcpy(m->text, f.text, sizeof m->text);
    memcpy(m->id, f.id, sizeof m->id);
    m->flags = APRS_MF_INCOMING | APRS_MF_UNREAD |
               (for_me ? APRS_MF_FOR_ME : 0) |
               (t == APRS_MT_BULLETIN ? APRS_MF_BULLETIN : 0);
    if (for_me) {
        audio_beep_freq(18000, 80);
        audio_beep_freq(24000, 80);
    }
}

/* --------------------------------------------------------------- poll */

void aprs_msg_init(void)
{
    cfg_load();
    next_id = get_tick() % 900U + 100U;
    if (cfg.rx_enable) aprs_rx_start();
}

void aprs_msg_poll(void)
{
    uint32_t now = get_tick();

    /* received frames (the AFSK demodulator runs in the BK4829) */
    if (cfg.rx_enable) {
        aprs_packet_t pkt;
        aprs_rx_poll_decode(&pkt);   /* calls aprs_msg_on_rx() */
    }

    /* pending acks */
    if (ackq_n && (int32_t)(now - ackq[0].due) >= 0) {
        char info[1 + APRS_MSG_ADDR_LEN + 1 + 3 + APRS_MSG_ID_MAX + 1];
        int l = aprs_msg_format_ack(info, sizeof info, ackq[0].to, ackq[0].id, 0);
        if (l > 0) transmit_info(info, (uint16_t)l);
        memmove(&ackq[0], &ackq[1], sizeof ackq[0] * (ACKQ_N - 1));
        ackq_n--;
    }

    /* retries: 30 s, 60 s, 120 s, ... */
    for (uint8_t i = 0; i < n_box; i++) {
        aprs_msg_t *m = &box[(head + i) % APRS_MSG_INBOX];
        if (m->flags & (APRS_MF_INCOMING | APRS_MF_ACKED | APRS_MF_FAILED)) continue;
        if (m->tries > cfg.retries) {
            /* last copy sent: give the ack 60 s to arrive */
            if (now - m->tick >= 60000U) m->flags |= APRS_MF_FAILED;
            continue;
        }
        uint32_t wait = 30000U << (m->tries ? m->tries - 1 : 0);
        if (now - m->tick < wait) continue;
        send_msg_now(m);
        break;                                   /* one TX per poll */
    }
}
