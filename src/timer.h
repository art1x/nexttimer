#pragma once
#include <stdint.h>

typedef enum {
    TIMER_IDLE,
    TIMER_RUNNING,
    TIMER_PAUSED,
    TIMER_EXPIRED,
} TimerState;

typedef struct {
    int        set_minutes;
    int        set_seconds;
    int        total_ms;
    int        remaining_ms;
    TimerState state;
    uint32_t   last_tick_ms;
} Timer;

void timer_init(Timer *t);
void timer_add_minutes(Timer *t, int delta);
void timer_add_seconds(Timer *t, int delta);
void timer_start(Timer *t, uint32_t now_ms);
void timer_pause(Timer *t, uint32_t now_ms);
void timer_resume(Timer *t, uint32_t now_ms);
void timer_reset(Timer *t);
void timer_tick(Timer *t, uint32_t now_ms);
int  timer_display_minutes(const Timer *t);
int  timer_display_seconds(const Timer *t);

/* Stellt den Timer mit gespeichertem Zustand wieder her (nach App-Neustart). */
void timer_restore(Timer *t, int remaining_ms, int total_ms,
                   int set_minutes, int set_seconds, uint32_t now_ms);
