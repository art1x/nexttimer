#include "timer.h"

void timer_init(Timer *t) {
    t->set_minutes  = 5;
    t->set_seconds  = 0;
    t->total_ms     = 5 * 60 * 1000;
    t->remaining_ms = 5 * 60 * 1000;
    t->state        = TIMER_IDLE;
    t->last_tick_ms = 0;
}

static void sync_remaining(Timer *t) {
    if (t->set_minutes == 0 && t->set_seconds < 10) t->set_seconds = 10;
    t->remaining_ms = (t->set_minutes * 60 + t->set_seconds) * 1000;
}

void timer_add_minutes(Timer *t, int delta) {
    if (t->state != TIMER_IDLE) return;
    t->set_minutes += delta;
    if (t->set_minutes < 0)  t->set_minutes = 0;
    if (t->set_minutes > 99) t->set_minutes = 99;
    sync_remaining(t);
}

void timer_add_seconds(Timer *t, int delta) {
    if (t->state != TIMER_IDLE) return;
    t->set_seconds += delta;
    if (t->set_seconds < 0)  t->set_seconds = 0;
    if (t->set_seconds > 59) t->set_seconds = 59;
    sync_remaining(t);
}

void timer_start(Timer *t, uint32_t now_ms) {
    if (t->state != TIMER_IDLE) return;
    t->total_ms     = t->remaining_ms;
    t->state        = TIMER_RUNNING;
    t->last_tick_ms = now_ms;
}

void timer_pause(Timer *t, uint32_t now_ms) {
    if (t->state != TIMER_RUNNING) return;
    timer_tick(t, now_ms);
    t->state = TIMER_PAUSED;
}

void timer_resume(Timer *t, uint32_t now_ms) {
    if (t->state != TIMER_PAUSED) return;
    t->state        = TIMER_RUNNING;
    t->last_tick_ms = now_ms;
}

void timer_reset(Timer *t) {
    t->remaining_ms = (t->set_minutes * 60 + t->set_seconds) * 1000;
    t->state        = TIMER_IDLE;
    t->last_tick_ms = 0;
}

void timer_tick(Timer *t, uint32_t now_ms) {
    if (t->state != TIMER_RUNNING) return;
    uint32_t elapsed = now_ms - t->last_tick_ms;
    t->last_tick_ms  = now_ms;
    if (elapsed >= (uint32_t)t->remaining_ms) {
        t->remaining_ms = 0;
        t->state        = TIMER_EXPIRED;
    } else {
        t->remaining_ms -= (int)elapsed;
    }
}

int timer_display_minutes(const Timer *t) {
    return t->remaining_ms / 1000 / 60;
}

int timer_display_seconds(const Timer *t) {
    return (t->remaining_ms / 1000) % 60;
}
