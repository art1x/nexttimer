#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t elapsed_ms;   /* akkumulierte Zeit (ohne aktuellen Lauf) */
    uint32_t start_ms;     /* SDL_GetTicks() beim letzten Start */
    bool     running;
} Stopwatch;

/* Initialisiert die Stoppuhr auf 0. */
void stopwatch_init(Stopwatch *sw);

/* Startet die Stoppuhr (hat keine Wirkung wenn sie schon läuft). */
void stopwatch_start(Stopwatch *sw, uint32_t now_ms);

/* Pausiert die Stoppuhr und speichert die gelaufene Zeit. */
void stopwatch_pause(Stopwatch *sw, uint32_t now_ms);

/* Setzt die Stoppuhr auf 0 zurück und stoppt sie. */
void stopwatch_reset(Stopwatch *sw);

/* Gibt die aktuell angezeigte Zeit in Millisekunden zurück. */
uint32_t stopwatch_get_ms(const Stopwatch *sw, uint32_t now_ms);
