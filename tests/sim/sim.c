/*
 * sim.c - Host simulator for the satellite feature of the RT-950 Pro firmware
 *
 * Links the REAL firmware sources (satellite.c, sat_ui.c, sat_pred.c,
 * sat_sgp4.c, sat_math.c, cps.c, flash_crc.c) against small stubs for the
 * hardware: a 2 MB RAM "SPI flash", a 240x320 RGB565 "LCD" frame buffer,
 * VFO/BK4829/GPS/audio stubs and a pseudo-terminal for the CPS UART.
 *
 *   sim cps <flash_out.bin>
 *       Opens a pty, prints "PTY <path>" and serves the CPS protocol until
 *       a WriteEnd ('X') frame arrives, then dumps the flash image.
 *
 *   sim render <flash.bin> <unix_time> <out_prefix> [sat_name]
 *       Boots the satellite engine from the flash image at the given UTC
 *       time (no GPS: QTH from the database), waits for the predictions,
 *       renders LIST, PASSES and TRACK screens (+ a TX frame) to PPM files
 *       and prints the engine state (Doppler, VFOs, PTT hook).
 */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app/satellite.h"
#include "app/vfo.h"
#include "app/gps.h"
#include "app/keypad.h"
#include "app/display.h"
#include "app/cps.h"
#include "app/aprs.h"
#include "app/aprs_msg.h"
#include "drivers/lcd.h"
#include "drivers/bk4829.h"
#include "drivers/uart.h"
#include "sim_extract.h"           /* fonts + CTCSS table cut from the sources */
#include <termios.h>                /* after at32f403a.h: it #defines CR1/CR2 */

/* ===================================================================== */
/*  Time                                                                  */
/* ===================================================================== */
static int      sim_realtime = 1;
static uint32_t sim_ms;
uint32_t get_tick(void)
{
    if (sim_realtime) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
    }
    return sim_ms;
}
void delay_ms(uint32_t ms) { if (!sim_realtime) sim_ms += ms; else usleep(ms * 1000); }

/* ===================================================================== */
/*  SPI flash (2 MB, erased = 0xFF)                                       */
/* ===================================================================== */
static uint8_t flash[0x200000];
void spi_flash_read(uint32_t a, uint8_t *b, uint16_t n) { memcpy(b, flash + a, n); }
void spi_flash_write_page(uint32_t a, const uint8_t *b, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) flash[a + i] &= b[i];     /* NOR semantics */
}
void spi_flash_erase_4k(uint32_t a) { memset(flash + (a & ~0xFFFU), 0xFF, 4096); }

/* ===================================================================== */
/*  LCD frame buffer                                                      */
/* ===================================================================== */
static uint16_t fb[LCD_HEIGHT][LCD_WIDTH];
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    for (uint32_t j = y; j < (uint32_t)y + h && j < LCD_HEIGHT; j++)
        for (uint32_t i = x; i < (uint32_t)x + w && i < LCD_WIDTH; i++)
            fb[j][i] = c;
}
static void px(int x, int y, uint16_t c) { if (x >= 0 && y >= 0 && x < LCD_WIDTH && y < LCD_HEIGHT) fb[y][x] = c; }
static void ch8(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg, int s)
{
    if (ch < 0x20 || ch > 0x7E) ch = ' ';
    const uint8_t *g = &font8x8[(ch - 0x20) * 8];
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            for (int dy = 0; dy < s; dy++)
                for (int dx = 0; dx < s; dx++)
                    px(x + c * s + dx, y + r * s + dy, (g[r] & (0x80 >> c)) ? fg : bg);
}
void lcd_draw_string(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{ for (; *s; s++, x += 8) ch8(x, y, *s, fg, bg, 1); }
void lcd_draw_string_2x(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{ for (; *s; s++, x += 16) ch8(x, y, *s, fg, bg, 2); }
void display_draw_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++, x += DISPLAY_FONT_W + DISPLAY_CHAR_GAP) {
        char ch = (*s < 0x20 || *s > 0x7E) ? '?' : *s;
        const uint8_t *g = font5x7[ch - 0x20];
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                px(x + c, y + r, (g[c] & (1 << r)) ? fg : bg);
    }
}
void display_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t c) { lcd_fill_rect(x, y, w, 1, c); }
void display_set_mode(display_mode_t m) { (void)m; }
void display_draw_status_bar(void)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LAYOUT_STATUS_H, COLOR_BLACK);
    display_draw_text(2, 6, radio_is_transmitting() ? "TX" : "RX",
                      radio_is_transmitting() ? COLOR_RED : COLOR_GREEN, COLOR_BLACK);
    display_draw_text(20, 6, "GP", COLOR_DARK_GRAY, COLOR_BLACK);
    display_draw_text(110, 6, "A", COLOR_YELLOW, COLOR_BLACK);
    sat_draw_icon(150, 4);
    if (aprs_msg_unread()) display_draw_text(136, 13, "MSG", COLOR_YELLOW, COLOR_BLACK);
    /* battery like the real status bar */
    lcd_fill_rect(206, 4, 28, 12, COLOR_WHITE); lcd_fill_rect(207, 5, 26, 10, COLOR_BLACK);
    for (int i = 0; i < 4; i++) lcd_fill_rect(208 + i * 6, 6, 5, 8, COLOR_GREEN);
    lcd_fill_rect(234, 7, 3, 6, COLOR_WHITE);
    display_draw_text(172, 6, "8.1V", COLOR_GRAY, COLOR_BLACK);
    lcd_fill_rect(0, LAYOUT_STATUS_H - 1, LCD_WIDTH, 1, COLOR_DARK_GRAY);
}
static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
    for (int y = 0; y < LCD_HEIGHT; y++)
        for (int x = 0; x < LCD_WIDTH; x++) {
            uint16_t c = fb[y][x];
            unsigned char rgb[3] = { (unsigned char)(((c >> 11) & 31) * 255 / 31),
                                     (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                     (unsigned char)((c & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* ===================================================================== */
/*  Radio stubs                                                           */
/* ===================================================================== */
static vfo_state_t vfos[3] = {
    { .freq_hz = 145500000, .chip = 1, .ctcss_tx_idx = 0xFF, .ctcss_rx_idx = 0xFF, .dcs_code_idx = 0xFF, .bandwidth = 1 },
    { .freq_hz = 433500000, .chip = 0, .ctcss_tx_idx = 0xFF, .ctcss_rx_idx = 0xFF, .dcs_code_idx = 0xFF, .bandwidth = 1 },
    { .freq_hz = 446006250, .chip = 1, .ctcss_tx_idx = 0xFF, .ctcss_rx_idx = 0xFF, .dcs_code_idx = 0xFF },
};
static radio_vfo_t active;
static int txing;
static uint32_t chip_freq[2];
static int chip_tone_tx[2] = { -1, -1 };
radio_vfo_t vfo_get_active(void) { return active; }
void vfo_set_active(radio_vfo_t v) { active = v; }
const vfo_state_t *vfo_get_state(radio_vfo_t v) { return &vfos[v]; }
void vfo_apply(radio_vfo_t v) { chip_freq[vfos[v].chip] = vfos[v].freq_hz; }
void vfo_set_frequency(radio_vfo_t v, uint32_t f) { vfos[v].freq_hz = f; chip_freq[vfos[v].chip] = f; }
void vfo_set_ctcss_tx(radio_vfo_t v, uint8_t i) { vfos[v].ctcss_tx_idx = i; }
void vfo_set_ctcss_rx(radio_vfo_t v, uint8_t i) { vfos[v].ctcss_rx_idx = i; }
void vfo_set_dcs(radio_vfo_t v, uint8_t c, uint8_t p) { vfos[v].dcs_code_idx = c; vfos[v].dcs_polarity = p; }
void vfo_clear_tone(radio_vfo_t v) { vfos[v].ctcss_tx_idx = vfos[v].ctcss_rx_idx = vfos[v].dcs_code_idx = 0xFF; }
void vfo_set_bandwidth(radio_vfo_t v, uint8_t b) { vfos[v].bandwidth = b; }
void vfo_set_modulation(radio_vfo_t v, uint8_t m) { vfos[v].modulation = m; }
void vfo_set_offset_dir(radio_vfo_t v, uint8_t d) { vfos[v].offset_dir = d; }
void vfo_set_offset_freq(radio_vfo_t v, uint32_t f) { vfos[v].offset_freq_hz = f; }
void bk4829_write_reg(uint8_t c, uint8_t r, uint16_t v) { (void)c; (void)r; (void)v; }
void bk4829_set_ctcss_tx(uint8_t c, uint8_t i) { chip_tone_tx[c & 1] = i; }
void bk4829_disable_ctcss(uint8_t c) { chip_tone_tx[c & 1] = -1; }
void bk4829_disable_dcs(uint8_t c) { (void)c; }
int radio_is_transmitting(void) { return txing; }
int sched_enable(const char *n, uint8_t e) { (void)n; (void)e; return 0; }
void audio_beep_freq(uint16_t f, uint16_t d) { printf("  [beep %u.%u Hz %u ms]\n", f / 10, f % 10, d); }
static gps_data_t gps;
const gps_data_t *gps_get_data(void) { return &gps; }
void menu_open_category(uint8_t c) { (void)c; }

/* ===================================================================== */
/*  CPS UART over a pty                                                   */
/* ===================================================================== */
static int pty_fd = -1;
static uint8_t rxq[4096];
static uint16_t rx_h, rx_t;
static int write_end_seen;
static void pump_rx(void)
{
    uint8_t b[256];
    ssize_t n = read(pty_fd, b, sizeof b);
    for (ssize_t i = 0; i < n; i++) { rxq[rx_h] = b[i]; rx_h = (uint16_t)((rx_h + 1) % sizeof rxq); }
}
uint16_t uart_cps_rx_available(void) { return (uint16_t)((rx_h + sizeof rxq - rx_t) % sizeof rxq); }
int16_t uart_cps_rx_read(void)
{
    if (rx_h == rx_t) return -1;
    uint8_t c = rxq[rx_t]; rx_t = (uint16_t)((rx_t + 1) % sizeof rxq);
    return c;
}
void uart_send_buf(USART_TypeDef *u, const uint8_t *b, uint16_t n)
{
    (void)u;
    if (n >= 5 && b[0] == 0xA5 && b[4] == CPS_CMD_WRITE_END) write_end_seen = 1;
    if (write(pty_fd, b, n) < 0) perror("pty write");
}

static int run_cps(const char *out)
{
    pty_fd = posix_openpt(O_RDWR | O_NOCTTY);
    grantpt(pty_fd); unlockpt(pty_fd);
    struct termios t; tcgetattr(pty_fd, &t); cfmakeraw(&t); tcsetattr(pty_fd, TCSANOW, &t);
    fcntl(pty_fd, F_SETFL, O_NONBLOCK);
    printf("PTY %s\n", ptsname(pty_fd)); fflush(stdout);
    memset(flash, 0xFF, sizeof flash);
    sat_init();
    cps_init();
    int idle_after_done = 0;
    for (int it = 0; it < 6000 * 20; it++) {           /* max ~10 min */
        pump_rx();
        int before = cps_is_active();
        cps_poll();
        if (before && !cps_is_active()) idle_after_done = 1;
        if (idle_after_done) break;
        usleep(500);
    }
    FILE *f = fopen(out, "wb");
    fwrite(flash + SAT_DB_FLASH_ADDR, 1, SAT_DB_FLASH_SIZE, f);
    fclose(f);
    printf("CPS session finished; time source=%d sats=%d\n", sat_time_source(), sat_count());
    return 0;
}

/* WriteEnd is consumed by cps.c itself; we detect the session end through
 * cps_is_active() going low (cps.c returns to IDLE after 'X'). */

/* ===================================================================== */
/*  Render scenario                                                       */
/* ===================================================================== */
static void tick(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 100) { sim_ms += 100; sat_poll(); }
}

static void key(uint8_t k) { sat_ui_handle_key(k); }

static void draw_frame(const char *prefix, const char *name)
{
    char path[512];
    sim_ms += 300;                   /* > 200 ms gap: full redraw */
    sat_ui_draw();
    snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    save_ppm(path);
    printf("  wrote %s\n", path);
}

static int run_render(const char *flash_in, uint32_t unix_t, const char *prefix, const char *want)
{
    sim_realtime = 0;
    sim_ms = 1000;
    memset(flash, 0xFF, sizeof flash);
    FILE *f = fopen(flash_in, "rb");
    if (!f) { perror(flash_in); return 1; }
    size_t n = fread(flash + SAT_DB_FLASH_ADDR, 1, SAT_DB_FLASH_SIZE, f);
    fclose(f);
    printf("flash image %zu bytes\n", n);

    sat_init();
    printf("sat_count=%u\n", sat_count());
    if (!sat_count()) return 2;
    sat_time_set(unix_t, SAT_TIME_PC);
    tick(200);
    double lat, lon, alt;
    char loc[7];
    sat_locator(loc);
    printf("observer src=%d %s\n", sat_position(&lat, &lon, &alt), loc);

    /* let the background predictor finish */
    int guard = 0;
    while (!sat_pred_done() && guard++ < 5000) tick(100);
    printf("predictions done after %d ticks\n", guard);
    for (uint8_t i = 0; i < sat_count(); i++) {
        const sat_pass_t *p = sat_next_pass(i);
        int y, mo, d, h, mi, s;
        if (!p || !p->los) { printf("  %-12.12s none\n", sat_get(i)->name); continue; }
        sat_unix_to_ymdhms(p->aos, &y, &mo, &d, &h, &mi, &s);
        printf("  %-12.12s AOS %04d-%02d-%02d %02d:%02d:%02d dur %4us max %2u az %3u>%3u %s\n",
               sat_get(i)->name, y, mo, d, h, mi, s, p->los - p->aos, p->max_el,
               p->aos_az, p->los_az, p->from_db ? "(PC table)" : "(on-radio SGP4)");
    }

    /* choose the satellite to track: remember it as "last used" so the
     * list cursor starts on it, then drive the UI with real key presses */
    uint8_t idx = 0;
    for (uint8_t i = 0; i < sat_count(); i++)
        if (want && strncmp(sat_get(i)->name, want, strlen(want)) == 0) idx = i;
    const sat_pass_t *p = sat_next_pass(idx);
    sat_engage(idx);
    sat_disengage();

    sat_ui_enter();
    draw_frame(prefix, "1_list");
    key(KEY_C_MENU);                    /* LIST -> TRACK (engage) */
    if (!sat_is_engaged() || sat_track()->idx != idx) { printf("engage via UI failed\n"); return 3; }
    key(KEY_1);                         /* TRACK -> PASSES */
    guard = 0;
    while (sat_passlist_busy() && guard++ < 20000) tick(100);
    draw_frame(prefix, "2_passes");
    key(KEY_HASH);                      /* back to TRACK */

    /* jump the clock to 1/3 into the next pass (AOS side, approaching) */
    if (p && p->los) {
        uint32_t t = p->aos + (p->tca - p->aos) / 2;
        sat_time_set(t, SAT_TIME_PC);
    }
    tick(1100);
    const sat_track_t *tr = sat_track();
    printf("TRACK %s: az %.1f el %.1f range %.0f km rr %+.3f km/s\n", sat_get(idx)->name,
           tr->look.az_deg, tr->look.el_deg, tr->look.range_km, tr->look.range_rate_kms);
    printf("  RX %u Hz (dop %+d)  TX %u Hz (dop %+d)  VFO A=%u VFO B=%u chipA=%u chipB=%u\n",
           tr->rx_hz, tr->rx_shift_hz, tr->tx_hz, tr->tx_shift_hz,
           vfos[0].freq_hz, vfos[1].freq_hz, chip_freq[1], chip_freq[0]);
    draw_frame(prefix, "3_track");

    /* arm tone + PTT hook */
    key(KEY_STAR);
    radio_vfo_t tv; uint32_t tf;
    int r = sat_ptt_prepare(&tv, &tf);
    txing = 1;
    printf("PTT hook: ret=%d tx_vfo=%c tx=%u Hz tone_idx=%d (%s)\n", r, 'A' + tv, tf,
           chip_tone_tx[vfos[tv].chip & 1],
           chip_tone_tx[vfos[tv].chip & 1] >= 0 ? "" : "none");
    draw_frame(prefix, "4_track_tx_arm");
    txing = 0;
    sat_ptt_release();

    /* restore check */
    sat_ui_exit();
    printf("after exit: VFO A=%u VFO B=%u active=%c\n", vfos[0].freq_hz, vfos[1].freq_hz, 'A' + active);
    return 0;
}


/* ===================================================================== */
/*  APRS stubs: frames go to stdout as TX lines, RX is injected           */
/* ===================================================================== */
static aprs_config_t acfg;
const aprs_config_t *aprs_config_get(void) { return &acfg; }
void aprs_rx_start(void) {}
void aprs_stop(void) {}
int aprs_rx_poll_decode(aprs_packet_t *pkt) { (void)pkt; return 0; }
int aprs_send_frame(const uint8_t *frame, uint16_t len)
{
    uint16_t fcs = ax25_fcs(frame, len);
    printf("TX ");
    for (uint16_t i = 0; i < len; i++) printf("%02X", frame[i]);
    printf(" %02X%02X\n", fcs & 0xFF, fcs >> 8);
    return 0;
}
static void ui_key(uint8_t k) { aprs_msg_ui_handle_key(k); sim_ms += 1000; }
static void ui_draw(const char *prefix, const char *name)
{
    char path[512];
    sim_ms += 300;
    aprs_msg_ui_draw();
    snprintf(path, sizeof path, "%s_%s.ppm", prefix, name);
    save_ppm(path);
    printf("  wrote %s\n", path);
}
static void type_text(const char *t)
{
    /* drive the multi-tap editor like a user would */
    static const char *const km[10] = { " 0", "1.,-?!/@#*", "ABC2", "DEF3", "GHI4", "JKL5", "MNO6", "PQRS7", "TUV8", "WXYZ9" };
    static const uint8_t keys[10] = { KEY_0, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9 };
    for (; *t; t++) {
        for (int d = 0; d < 10; d++) {
            const char *pos = strchr(km[d], *t);
            if (!pos) continue;
            for (int k = 0; k <= pos - km[d]; k++) { aprs_msg_ui_handle_key(keys[d]); sim_ms += 100; }
            sim_ms += 1000;            /* multi-tap timeout commits */
            aprs_msg_ui_draw();
            break;
        }
    }
}

static int run_aprs(const char *prefix)
{
    sim_realtime = 0; sim_ms = 5000;
    memset(flash, 0xFF, sizeof flash);
    memcpy(acfg.callsign, "EA7ABC", 7);
    acfg.ssid = 7;
    acfg.path_mode = APRS_PATH_WIDE1_1;
    aprs_msg_init();
    printf("cfg rx=%u ack=%u retries=%u\n", aprs_msg_cfg()->rx_enable, aprs_msg_cfg()->auto_ack, aprs_msg_cfg()->retries);
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        if (!strncmp(line, "SEND ", 5)) {               /* SEND <to> <text> */
            char to[16]; char *sp = strchr(line + 5, ' ');
            if (!sp) continue;
            *sp = 0; strncpy(to, line + 5, sizeof to - 1); to[15] = 0;
            char *t = sp + 1; t[strcspn(t, "\r\n")] = 0;
            printf("RET %d\n", aprs_msg_send(to, t));
        } else if (!strncmp(line, "RX ", 3)) {          /* RX <src> <info...> */
            char *sp = strchr(line + 3, ' ');
            if (!sp) continue;
            *sp = 0;
            char *info = sp + 1; info[strcspn(info, "\r\n")] = 0;
            aprs_msg_on_rx(line + 3, (const uint8_t *)info, (uint16_t)strlen(info));
        } else if (!strncmp(line, "WAIT ", 5)) {         /* WAIT <ms> */
            uint32_t ms = (uint32_t)strtoul(line + 5, 0, 10);
            for (uint32_t t = 0; t < ms; t += 100) { sim_ms += 100; aprs_msg_poll(); }
        } else if (!strncmp(line, "DUMP", 4)) {
            for (uint8_t i = 0; i < aprs_msg_count(); i++) {
                const aprs_msg_t *m = aprs_msg_get(i);
                printf("MSG %u peer=%s id=%s flags=%02X tries=%u text=%s\n", i, m->peer, m->id, m->flags, m->tries, m->text);
            }
            printf("UNREAD %u\n", aprs_msg_unread());
        } else if (!strncmp(line, "UI", 2)) {
            aprs_msg_ui_enter();
            ui_draw(prefix, "1_inbox");
            aprs_msg_ui_handle_key(KEY_C_MENU);
            ui_draw(prefix, "2_view");
            aprs_msg_ui_handle_key(KEY_C_MENU);      /* reply */
            type_text("QSL 73 DE EA7ABC");
            ui_draw(prefix, "3_compose");
            ui_key(KEY_C_MENU);                       /* send */
            ui_draw(prefix, "4_inbox_after");
            aprs_msg_ui_exit();
        }
        fflush(stdout);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "cps")) return run_cps(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "aprs")) return run_aprs(argv[2]);
    if (argc >= 5 && !strcmp(argv[1], "render"))
        return run_render(argv[2], (uint32_t)strtoul(argv[3], 0, 10), argv[4], argc > 5 ? argv[5] : 0);
    fprintf(stderr, "usage: sim cps <out.bin> | sim render <flash.bin> <unix> <prefix> [sat]\n");
    return 1;
}
