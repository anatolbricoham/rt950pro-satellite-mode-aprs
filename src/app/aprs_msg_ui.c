/*
 * aprs_msg_ui.c - APRS messaging screens (240 x 320)
 *
 *   INBOX    newest first. In/out arrows, unread mark, delivery status.
 *   VIEW     full message; MENU = reply.
 *   COMPOSE  addressee + text with multi-tap entry (67 characters).
 *
 * Keys:  encoder = move,  MENU = open / next / send,  # = back,
 *        * = new message (inbox) / delete char (editor),
 *        B = quick text (editor), 2-9 / 0 / 1 = multi-tap letters.
 */

#include "app/aprs_msg.h"
#include "app/aprs.h"
#include "app/display.h"
#include "app/keypad.h"
#include "drivers/lcd.h"

#include <string.h>

extern uint32_t get_tick(void);

typedef enum { MS_INBOX = 0, MS_VIEW, MS_TO, MS_TEXT } ms_screen_t;

static uint8_t     active;
static ms_screen_t scr;
static uint8_t     dirty;
static uint8_t     sel, top;
static uint32_t    last_draw, last_sig;

/* editor */
static char    ed_to[APRS_MSG_CALL_MAX];
static char    ed_text[APRS_MSG_TEXT_MAX + 1];
static char   *ed_buf;
static uint8_t ed_max, ed_len;
static int8_t  tap_key = -1;
static uint8_t tap_idx;
static uint32_t tap_ms;
static uint8_t quick_idx;
static const char *status_line;

static const char *const keymap[10] = {
    " 0", "1.,-?!/@#*", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9",
};
static const char *const quick[] = {
    "QSL? 73", "QRV", "QRT, 73", "Thanks for the QSO, 73", "CQ CQ", "Going QRT", "Hi from RT-950 Pro",
};

#define C_BG   COLOR_BLACK
#define C_HDR  COLOR_YELLOW
#define C_TXT  COLOR_WHITE
#define C_DIM  COLOR_GRAY
#define C_DARK COLOR_DARK_GRAY
#define C_OK   COLOR_GREEN
#define C_INF  COLOR_CYAN
#define C_BAD  COLOR_RED
#define C_SEL  0x2945

static int8_t key_digit(uint8_t k)
{
    switch (k) {
    case KEY_0: return 0; case KEY_1: return 1; case KEY_2: return 2; case KEY_3: return 3;
    case KEY_4: return 4; case KEY_5: return 5; case KEY_6: return 6; case KEY_7: return 7;
    case KEY_8: return 8; case KEY_9: return 9; default: return -1;
    }
}

static void commit_tap(void) { if (tap_key >= 0 && ed_len < ed_max) ed_len++; tap_key = -1; ed_buf[ed_len] = '\0'; }

static void ed_start(char *buf, uint8_t max)
{
    ed_buf = buf; ed_max = max; ed_len = (uint8_t)strlen(buf); tap_key = -1;
}

static void ed_key(uint8_t key)
{
    int8_t d = key_digit(key);
    uint32_t now = get_tick();
    if (d >= 0) {
        if (tap_key == d && now - tap_ms < 900U) {
            tap_idx = (uint8_t)((tap_idx + 1) % strlen(keymap[d]));
        } else {
            commit_tap();
            if (ed_len >= ed_max) return;
            tap_key = d; tap_idx = 0;
        }
        ed_buf[ed_len] = keymap[d][tap_idx];
        ed_buf[ed_len + 1] = '\0';
        tap_ms = now;
    } else if (key == KEY_STAR) {
        if (tap_key >= 0) { tap_key = -1; ed_buf[ed_len] = '\0'; }
        else if (ed_len) ed_buf[--ed_len] = '\0';
    }
}

/* ------------------------------------------------------------- drawing */

static void hdr(const char *title)
{
    char buf[40];
    const aprs_config_t *c = aprs_config_get();
    lcd_fill_rect(0, LAYOUT_STATUS_H, LCD_WIDTH, 15, C_BG);
    lcd_draw_string(4, 23, title, C_HDR, C_BG);
    uint8_t n = 0;
    for (; n < 6 && c->callsign[n] && c->callsign[n] != ' '; n++) buf[n] = c->callsign[n];
    if (!n) { strcpy(buf, "NO CALL"); n = 7; }
    else if (c->ssid) { buf[n++] = '-'; if (c->ssid >= 10) buf[n++] = '1'; buf[n++] = (char)('0' + c->ssid % 10); }
    buf[n] = '\0';
    display_draw_text((uint16_t)(LCD_WIDTH - 6 * n - 4), 24, buf, n == 7 && buf[0] == 'N' ? C_BAD : C_INF, C_BG);
    display_draw_hline(0, 35, LCD_WIDTH, C_DARK);
}

static void keys(const char *a, const char *b)
{
    lcd_fill_rect(0, 292, LCD_WIDTH, 28, C_BG);
    display_draw_hline(0, 292, LCD_WIDTH, C_DARK);
    display_draw_text(4, 297, a, C_DARK, C_BG);
    if (b) display_draw_text(4, 307, b, C_DARK, C_BG);
}

static void age_str(char *out, uint32_t tick)
{
    uint32_t s = (get_tick() - tick) / 1000U;
    if (s < 60) { out[0] = (char)('0' + s / 10); out[1] = (char)('0' + s % 10); out[2] = 's'; out[3] = 0; }
    else if (s < 3600) { uint32_t m = s / 60; out[0] = (char)('0' + m / 10); out[1] = (char)('0' + m % 10); out[2] = 'm'; out[3] = 0; }
    else { uint32_t h = s / 3600; if (h > 99) h = 99; out[0] = (char)('0' + h / 10); out[1] = (char)('0' + h % 10); out[2] = 'h'; out[3] = 0; }
}

/* wrap text into lines of `w` chars */
static uint16_t draw_wrapped(uint16_t x, uint16_t y, const char *t, uint8_t w, uint16_t fg, uint8_t max_lines)
{
    char line[41];
    uint8_t lines = 0;
    while (*t && lines < max_lines) {
        uint8_t n = (uint8_t)strlen(t);
        if (n > w) {
            n = w;
            for (uint8_t k = w; k > w / 2; k--) if (t[k] == ' ') { n = k; break; }
        }
        memcpy(line, t, n); line[n] = 0;
        display_draw_text(x, y, line, fg, C_BG);
        t += n; while (*t == ' ') t++;
        y = (uint16_t)(y + 11); lines++;
    }
    return y;
}

static void draw_inbox(void)
{
    uint8_t n = aprs_msg_count();
    char buf[44], age[5];
    lcd_fill_rect(0, 36, LCD_WIDTH, 256, C_BG);
    if (!n) {
        lcd_draw_string(8, 70, "No messages", C_DIM, C_BG);
        display_draw_text(8, 92, "* = write a new message", C_DIM, C_BG);
        display_draw_text(8, 104, aprs_msg_cfg()->rx_enable ? "RX: listening on APRS channel" : "RX: off (menu APRS > Msg RX)",
                          aprs_msg_cfg()->rx_enable ? C_OK : C_BAD, C_BG);
        return;
    }
    if (sel >= n) sel = (uint8_t)(n - 1);
    if (sel < top) top = sel;
    if (sel >= top + 9) top = (uint8_t)(sel - 8);
    for (uint8_t r = 0; r < 9 && top + r < n; r++) {
        const aprs_msg_t *m = aprs_msg_get((uint8_t)(top + r));
        uint16_t y = (uint16_t)(40 + r * 28);
        uint16_t bg = (top + r == sel) ? C_SEL : C_BG;
        lcd_fill_rect(0, y, LCD_WIDTH, 27, bg);
        uint8_t in = m->flags & APRS_MF_INCOMING;
        const char *st = in ? ((m->flags & APRS_MF_UNREAD) ? "*" : " ")
                            : (m->flags & APRS_MF_ACKED) ? "OK" : (m->flags & APRS_MF_FAILED) ? "X" : "..";
        uint16_t sc = in ? C_INF : (m->flags & APRS_MF_ACKED) ? C_OK : (m->flags & APRS_MF_FAILED) ? C_BAD : C_DIM;
        strcpy(buf, in ? "< " : "> ");
        strcat(buf, m->peer);
        lcd_draw_string(4, (uint16_t)(y + 3), buf, (m->flags & APRS_MF_UNREAD) ? C_HDR : C_TXT, bg);
        age_str(age, m->tick);
        display_draw_text(LCD_WIDTH - 50, (uint16_t)(y + 3), age, C_DIM, bg);
        display_draw_text(LCD_WIDTH - 20, (uint16_t)(y + 3), st, sc, bg);
        strncpy(buf, m->text, 38); buf[38] = 0;
        display_draw_text(4, (uint16_t)(y + 15), buf, (m->flags & APRS_MF_BULLETIN) ? C_HDR : C_DIM, bg);
    }
}

static void draw_view(void)
{
    const aprs_msg_t *m = aprs_msg_get(sel);
    char buf[44];
    lcd_fill_rect(0, 36, LCD_WIDTH, 256, C_BG);
    if (!m) return;
    uint8_t in = m->flags & APRS_MF_INCOMING;
    strcpy(buf, in ? "From: " : "To:   ");
    strcat(buf, m->peer);
    lcd_draw_string(4, 44, buf, C_TXT, C_BG);
    strcpy(buf, "Id: ");
    strcat(buf, m->id[0] ? m->id : "-");
    if (!in) {
        strcat(buf, (m->flags & APRS_MF_ACKED) ? "  DELIVERED" : (m->flags & APRS_MF_FAILED) ? "  NOT ACKED" : "  sending..");
    } else if (m->flags & APRS_MF_BULLETIN) {
        strcat(buf, "  BULLETIN");
    }
    display_draw_text(4, 60, buf, C_DIM, C_BG);
    display_draw_hline(0, 72, LCD_WIDTH, C_DARK);
    draw_wrapped(4, 80, m->text, 38, C_TXT, 8);
}

static void draw_editor(void)
{
    lcd_fill_rect(0, 36, LCD_WIDTH, 256, C_BG);
    lcd_draw_string(4, 44, "To:", scr == MS_TO ? C_HDR : C_DIM, C_BG);
    lcd_draw_string(36, 44, ed_to, C_TXT, C_BG);
    if (scr == MS_TO) lcd_fill_rect((uint16_t)(36 + 8 * strlen(ed_to)), 53, 8, 2, C_INF);

    lcd_draw_string(4, 66, "Text:", scr == MS_TEXT ? C_HDR : C_DIM, C_BG);
    char cnt[12];
    uint8_t l = (uint8_t)strlen(ed_text);
    cnt[0] = (char)('0' + l / 10); cnt[1] = (char)('0' + l % 10); cnt[2] = '/'; cnt[3] = '6'; cnt[4] = '7'; cnt[5] = 0;
    display_draw_text(LCD_WIDTH - 36, 67, cnt, C_DIM, C_BG);
    uint16_t y = draw_wrapped(4, 80, ed_text, 38, C_TXT, 8);
    if (scr == MS_TEXT) lcd_fill_rect(4, (uint16_t)(y), 40, 2, C_INF);

    if (status_line) display_draw_text(4, 270, status_line, C_INF, C_BG);
}

static void draw(uint8_t full)
{
    if (full) display_draw_status_bar();
    switch (scr) {
    case MS_INBOX:
        hdr("APRS MESSAGES");
        draw_inbox();
        if (full) keys("Enc:select MENU:open *:new", "#:exit");
        break;
    case MS_VIEW:
        hdr("MESSAGE");
        draw_view();
        if (full) keys("MENU:reply  #:back", 0);
        break;
    case MS_TO:
    case MS_TEXT:
        hdr("NEW MESSAGE");
        draw_editor();
        if (full) keys(scr == MS_TO ? "2-9:letters 0:spc *:del MENU:next" : "2-9:letters *:del B:quick text",
                       scr == MS_TO ? "#:cancel" : "MENU:SEND  #:back");
        break;
    }
}

/* ------------------------------------------------------------- public */

void aprs_msg_ui_enter(void) { active = 1; scr = MS_INBOX; sel = 0; top = 0; dirty = 1; }
void aprs_msg_ui_exit(void)  { active = 0; display_set_mode(DISPLAY_MODE_MAIN); }
uint8_t aprs_msg_ui_is_active(void) { return active; }

static void go(ms_screen_t s) { scr = s; dirty = 1; status_line = 0; }

void aprs_msg_ui_handle_encoder(int8_t dir)
{
    if (scr == MS_INBOX) {
        if (dir > 0 && sel + 1 < aprs_msg_count()) sel++;
        else if (dir < 0 && sel) sel--;
        last_sig = 0;
    }
}

void aprs_msg_ui_handle_key(uint8_t key)
{
    switch (scr) {
    case MS_INBOX:
        if (key == KEY_HASH) { aprs_msg_ui_exit(); return; }
        if (key == KEY_C_MENU && aprs_msg_count()) { aprs_msg_mark_read(sel); go(MS_VIEW); }
        else if (key == KEY_STAR) { ed_to[0] = 0; ed_text[0] = 0; ed_start(ed_to, APRS_MSG_ADDR_LEN); go(MS_TO); }
        break;
    case MS_VIEW:
        if (key == KEY_HASH) go(MS_INBOX);
        else if (key == KEY_C_MENU) {
            const aprs_msg_t *m = aprs_msg_get(sel);
            if (m) {
                strncpy(ed_to, m->peer, APRS_MSG_ADDR_LEN); ed_to[APRS_MSG_ADDR_LEN] = 0;
                ed_text[0] = 0;
                ed_start(ed_text, APRS_MSG_TEXT_MAX);
                go(MS_TEXT);
            }
        }
        break;
    case MS_TO:
        if (key == KEY_HASH) { go(MS_INBOX); break; }
        if (key == KEY_C_MENU) {
            commit_tap();
            if (ed_to[0]) { ed_start(ed_text, APRS_MSG_TEXT_MAX); go(MS_TEXT); }
            break;
        }
        ed_key(key);
        last_sig = 0;
        break;
    case MS_TEXT:
        if (key == KEY_HASH) { commit_tap(); ed_start(ed_to, APRS_MSG_ADDR_LEN); go(MS_TO); break; }
        if (key == KEY_B_SCAN) {
            commit_tap();
            strncpy(ed_text, quick[quick_idx], APRS_MSG_TEXT_MAX);
            quick_idx = (uint8_t)((quick_idx + 1) % (sizeof quick / sizeof quick[0]));
            ed_start(ed_text, APRS_MSG_TEXT_MAX);
            last_sig = 0;
            break;
        }
        if (key == KEY_C_MENU) {
            commit_tap();
            if (!ed_text[0]) break;
            int r = aprs_msg_send(ed_to, ed_text);
            if (r == -1) { status_line = "Invalid text/addressee ({|~ not allowed)"; last_sig = 0; break; }
            sel = 0;
            go(MS_INBOX);
            break;
        }
        ed_key(key);
        last_sig = 0;
        break;
    }
}

void aprs_msg_ui_draw(void)
{
    if (!active) return;
    uint32_t now = get_tick();
    if (now - last_draw > 200U) dirty = 1;
    last_draw = now;
    /* multi-tap timeout */
    if (tap_key >= 0 && now - tap_ms > 900U && (scr == MS_TO || scr == MS_TEXT)) { commit_tap(); last_sig = 0; }
    uint32_t sig = (uint32_t)aprs_msg_count() * 131U + aprs_msg_unread() * 7U + (now / 1000U) + sel * 1000003U
                   + (uint32_t)strlen(ed_text) * 17U + (uint32_t)strlen(ed_to) * 19U + (uint32_t)tap_idx;
    if (!dirty && sig == last_sig) return;
    uint8_t full = dirty;
    dirty = 0;
    last_sig = sig;
    draw(full);
}
