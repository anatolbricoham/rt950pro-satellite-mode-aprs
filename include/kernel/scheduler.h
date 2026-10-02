/*
 * scheduler.h - Cooperative task scheduler for the RT-950 Pro
 *
 * Table-driven cooperative multitasking. Each module registers a poll
 * function with an interval. The scheduler dispatches tasks in
 * registration order when their interval elapses.
 *
 * No preemption, no per-task stacks, no RTOS overhead.
 * IWDG is fed automatically every iteration.
 *
 * Usage:
 *   sched_register("keypad", keypad_poll, 20);   // 50 Hz
 *   sched_register("encoder", encoder_poll, 5);   // 200 Hz
 *   sched_register("display", display_update, 33); // ~30 fps
 *   sched_run();  // never returns
 */

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <stdint.h>

#define SCHED_MAX_TASKS  24

/* Register a periodic task. Returns 0 on success, -1 if table full.
 * name: human-readable label (for debug output, not copied - must be static)
 * poll_fn: function called when interval elapses (must not block)
 * interval_ms: minimum period between calls (0 = every iteration) */
int sched_register(const char *name, void (*poll_fn)(void),
                   uint16_t interval_ms);

/* Enable or disable a task by name. Disabled tasks are skipped.
 * Returns 0 on success, -1 if not found. */
int sched_enable(const char *name, uint8_t enabled);

/* Main scheduler loop. Feeds IWDG, asserts power latch, dispatches
 * tasks, prints heartbeat. Never returns. */
void sched_run(void);

#endif /* KERNEL_SCHEDULER_H */
