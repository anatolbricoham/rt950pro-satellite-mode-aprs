/*
 * scheduler.c - Cooperative task scheduler for the RT-950 Pro
 *
 * Dispatches registered poll functions at their configured intervals.
 * Feeds IWDG and asserts power latch (PB9) every iteration to prevent
 * watchdog reset and power loss.
 */

#include "kernel/scheduler.h"
#include "rt950_pinmap.h"
#include "drivers/gpio.h"
#include "debug_uart.h"
#include <stddef.h>

extern uint32_t get_tick(void);

/* Feed IWDG - bootloader enables watchdog before jumping to us */
#define IWDG_FEED()  (*(volatile uint32_t *)0x40003000UL = 0x0000AAAAUL)

/* Heartbeat interval */
#define HEARTBEAT_MS  2000

typedef struct {
    const char *name;
    void       (*poll_fn)(void);
    uint16_t   interval_ms;
    uint32_t   last_tick;
    uint8_t    enabled;
} task_entry_t;

static task_entry_t tasks[SCHED_MAX_TASKS];
static uint8_t task_count;

int sched_register(const char *name, void (*poll_fn)(void),
                   uint16_t interval_ms)
{
    if (task_count >= SCHED_MAX_TASKS || !poll_fn)
        return -1;

    task_entry_t *t = &tasks[task_count++];
    t->name        = name;
    t->poll_fn     = poll_fn;
    t->interval_ms = interval_ms;
    t->last_tick   = 0;
    t->enabled     = 1;

    dbg_puts("[SCHED] +task: ");
    dbg_puts(name);
    dbg_puts("\n");
    return 0;
}

int sched_enable(const char *name, uint8_t enabled)
{
    for (uint8_t i = 0; i < task_count; i++) {
        /* Compare pointers first (fast path for string literals),
         * fall back to byte-by-byte compare */
        const char *a = tasks[i].name;
        const char *b = name;
        if (a == b) goto found;
        if (!a || !b) continue;
        while (*a && *a == *b) { a++; b++; }
        if (*a == *b) goto found;
        continue;
found:
        tasks[i].enabled = enabled;
        return 0;
    }
    return -1;
}

void sched_run(void)
{
    uint32_t last_heartbeat = get_tick();
    uint32_t now;

    /* Snapshot initial tick for all tasks */
    now = get_tick();
    for (uint8_t i = 0; i < task_count; i++)
        tasks[i].last_tick = now;

    dbg_puts("[SCHED] run: ");
    dbg_reg("tasks=", task_count);

    while (1) {
        IWDG_FEED();

        /* Power latch - must stay HIGH or radio powers off */
        gpio_set_pin(GPIO_PB9_PWREN_PORT, GPIO_PB9_PWREN_PIN);

        now = get_tick();

        /* Dispatch tasks */
        for (uint8_t i = 0; i < task_count; i++) {
            task_entry_t *t = &tasks[i];
            if (!t->enabled)
                continue;
            if ((now - t->last_tick) >= t->interval_ms) {
                t->last_tick = now;
                t->poll_fn();
            }
        }

        /* Heartbeat */
        if ((now - last_heartbeat) >= HEARTBEAT_MS) {
            last_heartbeat = now;
            dbg_reg("[SCHED] hb t=", now);
        }
    }
}
