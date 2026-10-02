/*
 * event.c - Lightweight event queue for the RT-950 Pro
 *
 * Fixed ring buffer. On overflow, the oldest event is dropped and a
 * warning is printed to debug UART.
 */

#include "kernel/event.h"
#include "debug_uart.h"

#define EVENT_QUEUE_SIZE  64  /* must be power of 2 */
#define EVENT_QUEUE_MASK  (EVENT_QUEUE_SIZE - 1)

static event_t  queue[EVENT_QUEUE_SIZE];
static volatile uint8_t head;  /* next write position */
static volatile uint8_t tail;  /* next read position */
static uint16_t overflow_count;

void event_init(void)
{
    head = 0;
    tail = 0;
    overflow_count = 0;
}

int event_post(event_type_t type, uint16_t param)
{
    uint8_t next = (head + 1) & EVENT_QUEUE_MASK;

    if (next == tail) {
        /* Queue full - drop oldest */
        tail = (tail + 1) & EVENT_QUEUE_MASK;
        overflow_count++;
        if (overflow_count == 1)
            dbg_puts("[EVT] overflow!\n");
    }

    queue[head].type  = (uint8_t)type;
    queue[head].param = param;
    head = next;
    return 0;
}

int event_poll(event_t *out)
{
    if (head == tail)
        return 0;

    *out = queue[tail];
    tail = (tail + 1) & EVENT_QUEUE_MASK;
    return 1;
}

int event_pending(void)
{
    return head != tail;
}

void event_flush(void)
{
    tail = head;
}
