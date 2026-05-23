#pragma once
/*
 * screen.h — Backlight / screen sleep management
 *
 * Screen off:  set raw brightness to 0 via DISP_LCD_SET_BRIGHTNESS (0x102)
 * Screen on:   restore saved brightness level from NextUI shared settings
 *
 * Do NOT use DISP_LCD_BACKLIGHT_ENABLE/DISABLE (0x104/0x105) — those ioctls
 * corrupt the backlight driver state and invert the hardware brightness keys.
 *
 * Idle timeout is handled by NextUI itself.  This module only manages the
 * manual Power-button toggle and the suspend wake-lock.
 */

#include <stdbool.h>
#include <stdint.h>

/* Initialise screen state.  Call once at startup. */
void screen_init(void);

/* Turn the screen off immediately (e.g. user pressed Power). */
void screen_off(void);

/* Turn the screen back on (e.g. Power pressed again, or timer expired). */
void screen_on(void);

/* Returns true if the screen is currently off. */
bool screen_is_off(void);

/* Prevent (locked=true) or allow (locked=false) device suspend.
 * Call when the timer or stopwatch transitions to/from RUNNING. */
void screen_set_suspend_lock(bool locked);

/* Start power-button thread: short press → screen_off()/screen_on() toggle.
 * Call after screen_init().  Disable Apostrophe's handler first with
 * ap_set_power_handler(false) so it doesn't race and trigger suspend. */
void screen_init_power(void);

/* Stop the power-button thread.  Call before ap_quit(). */
void screen_quit_power(void);
