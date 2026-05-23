#include "stopwatch.h"

void stopwatch_init(Stopwatch *sw) {
    sw->elapsed_ms = 0;
    sw->start_ms   = 0;
    sw->running    = false;
}

void stopwatch_start(Stopwatch *sw, uint32_t now_ms) {
    if (sw->running) return;
    sw->start_ms = now_ms;
    sw->running  = true;
}

void stopwatch_pause(Stopwatch *sw, uint32_t now_ms) {
    if (!sw->running) return;
    /* Vergangene Zeit zum Akkumulator addieren bevor wir stoppen. */
    sw->elapsed_ms += now_ms - sw->start_ms;
    sw->running     = false;
}

void stopwatch_reset(Stopwatch *sw) {
    sw->elapsed_ms = 0;
    sw->start_ms   = 0;
    sw->running    = false;
}

uint32_t stopwatch_get_ms(const Stopwatch *sw, uint32_t now_ms) {
    if (sw->running)
        return sw->elapsed_ms + (now_ms - sw->start_ms);
    return sw->elapsed_ms;
}
