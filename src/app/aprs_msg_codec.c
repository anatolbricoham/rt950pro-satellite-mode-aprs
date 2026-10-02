/*
 * aprs_msg_codec.c - APRS message formatting/parsing and AX.25 UI frames
 *
 * Pure functions (no hardware): compiled into the firmware and into the
 * host test bench (tests/test_aprs_msg.py checks them against an
 * independent Python implementation and the APRS 1.0.1 examples).
 */

#include "app/aprs_msg.h"
#include <string.h>

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

int aprs_msg_format(char *out, uint16_t max, const char *to,
                    const char *text, const char *id)
{
    uint16_t p = 0;
    uint16_t tl = (uint16_t)strlen(text);
    uint16_t il = id ? (uint16_t)strlen(id) : 0;
    uint16_t need = 1 + APRS_MSG_ADDR_LEN + 1 + tl + (il ? 1 + il : 0) + 1;
    if (!to || !to[0] || strlen(to) > APRS_MSG_ADDR_LEN || tl > APRS_MSG_TEXT_MAX ||
        il > APRS_MSG_ID_MAX || need > max)
        return -1;
    /* '|', '~' and '{' are not allowed in message text */
    for (uint16_t i = 0; i < tl; i++)
        if (text[i] == '|' || text[i] == '~' || text[i] == '{' ||
            (uint8_t)text[i] < 0x20 || (uint8_t)text[i] > 0x7E)
            return -1;
    out[p++] = ':';
    for (uint16_t i = 0; i < APRS_MSG_ADDR_LEN; i++)
        out[p++] = (i < strlen(to)) ? up(to[i]) : ' ';
    out[p++] = ':';
    memcpy(&out[p], text, tl); p += tl;
    if (il) { out[p++] = '{'; memcpy(&out[p], id, il); p += il; }
    out[p] = '\0';
    return (int)p;
}

int aprs_msg_format_ack(char *out, uint16_t max, const char *to,
                        const char *id, uint8_t reject)
{
    char t[3 + APRS_MSG_ID_MAX + 1];
    if (!id || !id[0] || strlen(id) > APRS_MSG_ID_MAX) return -1;
    memcpy(t, reject ? "rej" : "ack", 3);
    strcpy(&t[3], id);
    return aprs_msg_format(out, max, to, t, 0);
}

aprs_msg_type_t aprs_msg_parse(const uint8_t *info, uint16_t len,
                               aprs_msg_fields_t *f)
{
    memset(f, 0, sizeof *f);
    /* minimum ":XXXXXXXXX:" */
    if (len < 11 || info[0] != ':' || info[10] != ':') return APRS_MT_NONE;

    uint8_t n = 0;
    for (uint8_t i = 1; i <= APRS_MSG_ADDR_LEN; i++)
        if (n < APRS_MSG_CALL_MAX - 1) f->addressee[n++] = (char)info[i];
    while (n && f->addressee[n - 1] == ' ') n--;
    f->addressee[n] = '\0';
    if (!n) return APRS_MT_NONE;

    const uint8_t *t = &info[11];
    uint16_t tl = (uint16_t)(len - 11);
    /* strip trailing CR/LF */
    while (tl && (t[tl - 1] == '\r' || t[tl - 1] == '\n')) tl--;

    /* ack / rej: "ackNNNNN" with nothing else (alphanumeric id) */
    if (tl >= 4 && tl <= 3 + APRS_MSG_ID_MAX &&
        (memcmp(t, "ack", 3) == 0 || memcmp(t, "rej", 3) == 0)) {
        uint8_t ok = 1;
        for (uint16_t i = 3; i < tl; i++) {
            char c = (char)t[i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                { ok = 0; break; }
        }
        if (ok) {
            memcpy(f->id, &t[3], tl - 3);
            f->id[tl - 3] = '\0';
            f->type = (t[0] == 'a') ? APRS_MT_ACK : APRS_MT_REJ;
            return f->type;
        }
    }

    /* message id: last '{' followed by 1..5 chars (reply-ack "}xx" ignored) */
    uint16_t text_len = tl;
    for (int16_t i = (int16_t)tl - 1; i >= 0 && i >= (int16_t)tl - 1 - APRS_MSG_ID_MAX - 3; i--) {
        if (t[i] == '{') {
            uint16_t il = (uint16_t)(tl - i - 1);
            uint16_t stop = il;
            for (uint16_t k = 0; k < il; k++)
                if (t[i + 1 + k] == '}') { stop = k; break; }
            if (stop >= 1 && stop <= APRS_MSG_ID_MAX) {
                memcpy(f->id, &t[i + 1], stop);
                f->id[stop] = '\0';
                text_len = (uint16_t)i;
            }
            break;
        }
    }
    if (text_len > APRS_MSG_TEXT_MAX) text_len = APRS_MSG_TEXT_MAX;
    memcpy(f->text, t, text_len);
    f->text[text_len] = '\0';

    f->type = (strncmp(f->addressee, "BLN", 3) == 0) ? APRS_MT_BULLETIN : APRS_MT_MESSAGE;
    return f->type;
}

static void split_call(const char *call, char base[7], uint8_t *ssid)
{
    uint8_t n = 0;
    *ssid = 0;
    while (*call && *call != '-' && n < 6) base[n++] = up(*call++);
    base[n] = '\0';
    while (*call && *call != '-') call++;       /* over-long call: cut */
    if (*call == '-') {
        call++;
        uint8_t v = 0;
        while (*call >= '0' && *call <= '9') v = (uint8_t)(v * 10 + (*call++ - '0'));
        *ssid = v;
    }
}

uint8_t aprs_msg_is_for_me(const char *addressee, const char *mycall, uint8_t myssid)
{
    char a[7], m[7];
    uint8_t as, ms;
    split_call(addressee, a, &as);
    split_call(mycall, m, &ms);
    if (!m[0]) return 0;
    if (strcmp(a, m) != 0) return 0;
    return as == (myssid & 0x0F);
}

static uint8_t put_addr(uint8_t *p, const char *call, uint8_t last)
{
    char base[7];
    uint8_t ssid;
    split_call(call, base, &ssid);
    if (!base[0] || ssid > 15) return 0;
    for (uint8_t i = 0; i < 6; i++)
        p[i] = (uint8_t)(((i < strlen(base)) ? base[i] : ' ') << 1);
    p[6] = (uint8_t)(0x60 | (ssid << 1) | (last ? 1 : 0));
    return 7;
}

uint16_t ax25_build_ui(uint8_t *frame, uint16_t max, const char *dest,
                       const char *src, const char *const *digis,
                       uint8_t ndigi, const uint8_t *info, uint16_t info_len)
{
    uint16_t need = (uint16_t)(14 + 7 * ndigi + 2 + info_len);
    if (need > max || ndigi > 8) return 0;
    uint16_t p = 0;
    if (!put_addr(&frame[p], dest, 0)) return 0;
    p += 7;
    if (!put_addr(&frame[p], src, ndigi == 0)) return 0;
    p += 7;
    for (uint8_t i = 0; i < ndigi; i++) {
        if (!put_addr(&frame[p], digis[i], i == ndigi - 1)) return 0;
        p += 7;
    }
    frame[p++] = 0x03;      /* UI */
    frame[p++] = 0xF0;      /* no layer 3 */
    memcpy(&frame[p], info, info_len);
    return (uint16_t)(p + info_len);
}

uint16_t ax25_fcs(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return (uint16_t)(crc ^ 0xFFFF);
}
