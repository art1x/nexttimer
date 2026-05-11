// ================================================================
// main.c  –  Next Timer for TrimUI Brick / NextUI
//
// Controls:
//   ↑ / ↓      → ±1 minute
//   ← / →      → ±10 seconds
//   A           → Start / Pause / Resume
//   B           → Reset / Exit (when idle)
//   MENU        → Settings
// ================================================================

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include "timer.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef PLATFORM_TG5040
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#endif


#define SETTINGS_FILE "./settings.txt"

static ap_status_bar_opts g_status_bar = {
    .show_clock   = AP_CLOCK_AUTO,
    .show_battery = true,
    .show_wifi    = true,
};

/* Forward declaration */
static void screen_settings(AppSettings *s);

// ----------------------------------------------------------------
// Alarm-Pattern: 3 × 300 ms an / 200 ms aus  (= 1500 ms Zyklus)
// ----------------------------------------------------------------
#define ALERT_BEEP_MS  300
#define ALERT_GAP_MS   200
#define ALERT_PULSES   3

// ----------------------------------------------------------------
// Screen-Off: rein per Software (schwarze SDL-Fläche).
// Kein Hardware-Eingriff – sicher auf allen Geräten.
// ----------------------------------------------------------------
#define SCREEN_ON   0   /* normaler Betrieb            */
#define SCREEN_OFF  1   /* schwarzer Bildschirm        */
#define SCREEN_HINT 2   /* Hinweis "Select+A" für 1 s */


// ----------------------------------------------------------------
// Backlight-Kontrolle (nur tg5040)
// Verwendet denselben Ansatz wie NextUI: O_RDWR + mmap auf
// SharedSettings POSIX-SHM, dann /dev/disp ioctl.
// ----------------------------------------------------------------
#ifdef PLATFORM_TG5040
#define DISP_LCD_SET_BRIGHTNESS 0x102
#define SHM_KEY "/SharedSettings"

/* Minimales Settings-Layout: version (int) + brightness (int) */
typedef struct { int version; int brightness; } MinSettings;

static void backlight_raw(int val) {
    int fd = open("/dev/disp", O_RDWR);
    if (fd >= 0) {
        unsigned long param[4] = {0, (unsigned long)val, 0, 0};
        ioctl(fd, DISP_LCD_SET_BRIGHTNESS, &param);
        close(fd);
    }
}

static int backlight_read_level(void) {
    /* SharedSettings mit O_RDWR öffnen wie InitSettings() es tut */
    int fd = shm_open(SHM_KEY, O_RDWR, 0644);
    if (fd < 0) return 7;
    MinSettings *s = mmap(NULL, sizeof(MinSettings),
                          PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (s == MAP_FAILED) return 7;
    int level = s->brightness;
    munmap(s, sizeof(MinSettings));
    return (level >= 0 && level <= 10) ? level : 7;
}

static void backlight_off(void) {
    backlight_raw(0);
}

static void backlight_on(void) {
    static const int tbl[11] = {1,8,16,32,48,72,96,128,160,192,255};
    int level = backlight_read_level();
    /* NextUI-Sequenz für Brick: erst Minimalwert 8, dann Zielwert */
    backlight_raw(8);
    backlight_raw(tbl[level]);
}
#endif

static void render_screen_off(int show_hint) {
    int       sw  = ap_get_screen_width();
    int       sh  = ap_get_screen_height();
    ap_color  blk = {0, 0, 0, 255};
    ap_draw_rect(0, 0, sw, sh, blk);

    if (show_hint) {
        ap_theme *thm  = ap_get_theme();
        TTF_Font *fmed = ap_get_font(AP_FONT_MEDIUM);
        TTF_Font *fsm  = ap_get_font(AP_FONT_SMALL);
        if (fmed) {
            const char *l1 = "Press Select + A";
            ap_draw_text(fmed, l1, (sw - ap_measure_text(fmed, l1)) / 2,
                         sh / 2 - TTF_FontHeight(fmed) - AP_S(4), thm->text);
        }
        if (fsm) {
            const char *l2 = "to wake the screen";
            ap_draw_text(fsm, l2, (sw - ap_measure_text(fsm, l2)) / 2,
                         sh / 2 + AP_S(4), thm->hint);
        }
    }
    ap_present();
}

// ----------------------------------------------------------------
// Audio-Alert: volume 1-10 → Amplitude 2800-28000
// ----------------------------------------------------------------
static SDL_AudioDeviceID start_alert_audio(int volume) {
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_AudioSpec want = {0}, got;
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!dev) return 0;

    const float  amplitude = 2800.0f * (float)volume;
    const int    beep_s    = got.freq * ALERT_BEEP_MS / 1000;
    const int    gap_s     = got.freq * ALERT_GAP_MS  / 1000;
    const int    total     = (beep_s + gap_s) * ALERT_PULSES;

    Sint16 *buf = malloc(total * sizeof(Sint16));
    if (!buf) { SDL_CloseAudioDevice(dev); return 0; }

    int pos = 0;
    for (int r = 0; r < ALERT_PULSES; r++) {
        for (int i = 0; i < beep_s; i++) {
            float t   = (float)i / (float)got.freq;
            float env = (i < beep_s / 8)
                            ? (8.0f * i / beep_s)
                            : (i > beep_s * 7 / 8)
                                ? (8.0f * (beep_s - i) / beep_s)
                                : 1.0f;
            buf[pos++] = (Sint16)(amplitude * env * sinf(2.0f * (float)M_PI * 880.0f * t));
        }
        for (int i = 0; i < gap_s; i++) buf[pos++] = 0;
    }
    SDL_QueueAudio(dev, buf, total * sizeof(Sint16));
    SDL_PauseAudioDevice(dev, 0);
    free(buf);
    return dev;
}

// ----------------------------------------------------------------
// Vibrations-Pattern (gleiche Timings wie Audio)
// ----------------------------------------------------------------
static const struct { int on; uint32_t ms; } VIB_PAT[] = {
    {1, ALERT_BEEP_MS}, {0, ALERT_GAP_MS},
    {1, ALERT_BEEP_MS}, {0, ALERT_GAP_MS},
    {1, ALERT_BEEP_MS}, {0, ALERT_GAP_MS},
};
#define VIB_PAT_LEN ((int)(sizeof(VIB_PAT) / sizeof(VIB_PAT[0])))

#ifdef PLATFORM_TG5040
static void vib_set(int on, int intensity) {
    if (on) {
        /* Minimum auf 1.0 V angehoben – Level 1 war vorher zu schwach */
        uint32_t voltage = 1000000u
            + (uint32_t)(intensity - 1) * ((3300000u - 1000000u) / 9u);
        FILE *f = fopen("/sys/class/motor/voltage", "w");
        if (f) { fprintf(f, "%u\n", voltage); fclose(f); }
    }
    FILE *f = fopen("/sys/class/gpio/gpio227/value", "w");
    if (f) { fprintf(f, "%d\n", on); fclose(f); }
}
#endif

// ----------------------------------------------------------------
// Screen-Timeout-Hilfsfunktionen
// ----------------------------------------------------------------
static const int TIMEOUT_VALS[] = {0, 10, 30, 60, 120, 300};
#define TIMEOUT_COUNT ((int)(sizeof(TIMEOUT_VALS)/sizeof(TIMEOUT_VALS[0])))

static const char *timeout_label(int secs) {
    switch (secs) {
        case   0: return "Never";
        case  10: return "10 sec";
        case  30: return "30 sec";
        case  60: return "1 min";
        case 120: return "2 min";
        case 300: return "5 min";
        default:  return "?";
    }
}

static int timeout_next(int cur) {
    for (int i = 0; i < TIMEOUT_COUNT; i++)
        if (TIMEOUT_VALS[i] == cur)
            return TIMEOUT_VALS[(i + 1) % TIMEOUT_COUNT];
    return 0;
}

static int timeout_prev(int cur) {
    for (int i = 0; i < TIMEOUT_COUNT; i++)
        if (TIMEOUT_VALS[i] == cur)
            return TIMEOUT_VALS[(i + TIMEOUT_COUNT - 1) % TIMEOUT_COUNT];
    return 0;
}

// ----------------------------------------------------------------
// Timer-Bildschirm rendern
// ----------------------------------------------------------------
static void render_timer(const Timer *t, int alert_active, const AppSettings *s) {
    ap_draw_background();
    if (alert_active && s->visual_alert) {
        ap_color red = {200, 30, 30, 255};
        ap_draw_rect(0, 0, ap_get_screen_width(), ap_get_screen_height(), red);
    }
    ap_draw_status_bar(&g_status_bar);

    int      sw   = ap_get_screen_width();
    int      sh   = ap_get_screen_height();
    ap_theme *thm = ap_get_theme();

    TTF_Font *fxl  = ap_get_font(AP_FONT_EXTRA_LARGE);
    TTF_Font *fmed = ap_get_font(AP_FONT_MEDIUM);
    TTF_Font *fsm  = ap_get_font(AP_FONT_SMALL);

    ap_draw_text(fsm, "Next Timer", 16, 12, thm->hint);

    /* Doppelt so große Schrift – einmalig laden und cachen */
    static TTF_Font *s_big = NULL;
    if (!s_big && fxl && thm->font_path[0]) {
        int pt = TTF_FontHeight(fxl) * 2;
        s_big = TTF_OpenFont(thm->font_path, pt);
    }
    TTF_Font *fnum = s_big ? s_big : fxl;
    if (fnum) {
        char ms[4], ss[4];
        snprintf(ms, sizeof(ms), "%02d", timer_display_minutes(t));
        snprintf(ss, sizeof(ss), "%02d", timer_display_seconds(t));

        int mw      = ap_measure_text(fnum, ms);
        int cw      = ap_measure_text(fnum, ":");
        int sw2     = ap_measure_text(fnum, ss);
        int fh      = TTF_FontHeight(fnum);
        int total_w = mw + cw + sw2;
        int ty      = sh / 2 - fh / 2 - AP_S(10);
        int mx      = (sw - total_w) / 2;

        ap_draw_text(fnum, ms, mx,          ty, thm->text);
        ap_draw_text(fnum, ":", mx + mw,     ty, thm->text);
        ap_draw_text(fnum, ss, mx + mw + cw, ty, thm->text);

        if (t->total_ms > 0 && t->state != TIMER_IDLE) {
            float progress = 1.0f - (float)t->remaining_ms / (float)t->total_ms;
            int bm = sw / 8;
            ap_draw_progress_bar(bm, ty + fh + AP_S(8), sw - 2 * bm, AP_S(6),
                                 progress, thm->accent, thm->highlight);
        }

        const char *status = NULL;
        ap_color    sc     = thm->hint;
        if (t->state == TIMER_RUNNING) { status = "Running"; }
        if (t->state == TIMER_PAUSED)  { status = "Paused";  }
        if (t->state == TIMER_EXPIRED) { status = "TIME UP!"; sc = thm->accent; }

        if (status && fmed) {
            int stw = ap_measure_text(fmed, status);
            ap_draw_text(fmed, status, (sw - stw) / 2, ty + fh + AP_S(18), sc);
        }
    }


    const char *hint = "";
    switch (t->state) {
        case TIMER_IDLE:
            hint = "↑↓:±1Min  ←→:±10Sec  A:Start  Menu:Settings  B:Exit";
            break;
        case TIMER_RUNNING:
            hint = "A:Pause  B:Cancel  Menu:Settings";
            break;
        case TIMER_PAUSED:
            hint = "A:Resume  B:Cancel  Menu:Settings";
            break;
        case TIMER_EXPIRED:
            hint = alert_active
                   ? "Any button: stop alarm"
                   : "Any button: restart  B:Exit";
            break;
    }
    if (fsm && hint[0]) {
        int hw = ap_measure_text(fsm, hint);
        ap_draw_text(fsm, hint, (sw - hw) / 2,
                     sh - TTF_FontHeight(fsm) - 10, thm->hint);
    }

    ap_present();
}

// ----------------------------------------------------------------
// Einstellungen rendern
// ----------------------------------------------------------------
static void render_settings(int cursor, const AppSettings *s) {
    static const char *labels[6] = {
        "Sound", "Volume", "Vibration", "Vib. Intensity",
        "Screen Timeout", "Visual Alarm"
    };
    static const int is_numeric[6] = {1, 1, 1, 1, 1, 1};

    char vals[6][32];
    snprintf(vals[0], sizeof(vals[0]), "%s",      s->sound_enabled     ? "On" : "Off");
    snprintf(vals[1], sizeof(vals[1]), "%d / 10", s->volume);
    snprintf(vals[2], sizeof(vals[2]), "%s",      s->vibration_enabled ? "On" : "Off");
    snprintf(vals[3], sizeof(vals[3]), "%d / 10", s->vibration_intensity);
    snprintf(vals[4], sizeof(vals[4]), "%s",      timeout_label(s->screen_timeout));
    snprintf(vals[5], sizeof(vals[5]), "%s",      s->visual_alert      ? "On" : "Off");

    ap_draw_background();
    ap_draw_status_bar(&g_status_bar);
    ap_draw_screen_title("Settings", &g_status_bar);

    int       sw  = ap_get_screen_width();
    int       sh  = ap_get_screen_height();
    ap_theme *thm = ap_get_theme();
    TTF_Font *fxl = ap_get_font(AP_FONT_EXTRA_LARGE);
    TTF_Font *fm  = ap_get_font(AP_FONT_MEDIUM);
    TTF_Font *fsm = ap_get_font(AP_FONT_SMALL);
    if (!fm || !fsm) { ap_present(); return; }

    int title_h = fxl ? TTF_FontHeight(fxl) : AP_S(48);
    int fh      = TTF_FontHeight(fm);
    int item_h  = fh + AP_S(14);
    int pad_x   = AP_S(30);
    int start_y = title_h + AP_S(18);

    for (int i = 0; i < 6; i++) {
        int  iy  = start_y + i * item_h;
        bool sel = (i == cursor);

        if (sel)
            ap_draw_rect(0, iy - AP_S(5), sw, item_h, thm->highlight);

        ap_color tc = sel ? thm->highlighted_text : thm->text;
        ap_draw_text(fm, labels[i], pad_x, iy, tc);

        char vbuf[48];
        if (is_numeric[i] && sel)
            snprintf(vbuf, sizeof(vbuf), "← %s →", vals[i]);
        else
            snprintf(vbuf, sizeof(vbuf), "%s", vals[i]);

        int vw = ap_measure_text(fm, vbuf);
        ap_draw_text(fm, vbuf, sw - pad_x - vw, iy, tc);
    }

    const char *hint = "↑↓:Select   ←→:Change value   B:Back";
    int hw = ap_measure_text(fsm, hint);
    ap_draw_text(fsm, hint, (sw - hw) / 2,
                 sh - TTF_FontHeight(fsm) - AP_S(10), thm->hint);

    ap_present();
}

// ----------------------------------------------------------------
// Einstellungen-Bildschirm
// ----------------------------------------------------------------
static void screen_settings(AppSettings *s) {
    int cursor = 0;

    while (1) {
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            if (!ev.pressed) continue;
            switch (ev.button) {
                case AP_BTN_UP:
                    cursor = (cursor > 0) ? cursor - 1 : 5;
                    break;
                case AP_BTN_DOWN:
                    cursor = (cursor < 5) ? cursor + 1 : 0;
                    break;
                case AP_BTN_LEFT:
                    switch (cursor) {
                        case 0: s->sound_enabled       ^= 1; break;
                        case 1: s->volume               = (s->volume              > 1) ? s->volume              - 1 : 10; break;
                        case 2: s->vibration_enabled   ^= 1; break;
                        case 3: s->vibration_intensity  = (s->vibration_intensity > 1) ? s->vibration_intensity - 1 : 10; break;
                        case 4: s->screen_timeout       = timeout_prev(s->screen_timeout); break;
                        case 5: s->visual_alert        ^= 1; break;
                    }
                    break;
                case AP_BTN_RIGHT:
                    switch (cursor) {
                        case 0: s->sound_enabled       ^= 1; break;
                        case 1: s->volume               = (s->volume              < 10) ? s->volume              + 1 : 1; break;
                        case 2: s->vibration_enabled   ^= 1; break;
                        case 3: s->vibration_intensity  = (s->vibration_intensity < 10) ? s->vibration_intensity + 1 : 1; break;
                        case 4: s->screen_timeout       = timeout_next(s->screen_timeout); break;
                        case 5: s->visual_alert        ^= 1; break;
                    }
                    break;
                case AP_BTN_B:
                case AP_BTN_MENU:
                    settings_save(s, SETTINGS_FILE);
                    return;
                default: break;
            }
        }

        render_settings(cursor, s);
        SDL_Delay(16);
    }
}

// ----------------------------------------------------------------
// Haupt-Timer-Schleife
// ----------------------------------------------------------------
static void screen_timer(Timer *t, AppSettings *s) {
    int               alert_active = 0;
    SDL_AudioDeviceID alert_dev    = 0;
    int               vib_on       = 0;
    int               vib_step     = 0;
    uint32_t          vib_next_ms  = 0;
    int               scr          = SCREEN_ON;
    uint32_t          last_input   = SDL_GetTicks();
    uint32_t          hint_start   = 0;
    int               select_held  = 0;

    while (1) {
        // ── Eingabe ──────────────────────────────────────────────
        ap_input_event ev;
        while (ap_poll_input(&ev)) {
            // Select-Held-Status immer tracken (für Combo-Erkennung)
            if (ev.button == AP_BTN_SELECT)
                select_held = ev.pressed;

            if (!ev.pressed) continue;

            // ── Bildschirm ist gedimmt ───────────────────────────
            if (scr != SCREEN_ON) {
                if (ev.button == AP_BTN_A && select_held) {
                    scr        = SCREEN_ON;
                    last_input = SDL_GetTicks();
                    ap_set_power_handler(true);
#ifdef PLATFORM_TG5040
                    backlight_on();
#endif
                } else if (scr == SCREEN_OFF) {
                    scr        = SCREEN_HINT;
                    hint_start = SDL_GetTicks();
#ifdef PLATFORM_TG5040
                    backlight_on();
#endif
                }
                continue; // Taste nicht an Timer weitergeben
            }

            // ── Normaler Betrieb ─────────────────────────────────
            last_input = SDL_GetTicks();

            if (t->state == TIMER_EXPIRED) {
                if (alert_active) {
                    alert_active = 0;
                    if (alert_dev) { SDL_CloseAudioDevice(alert_dev); alert_dev = 0; }
#ifdef PLATFORM_TG5040
                    if (vib_on) { vib_set(0, s->vibration_intensity); vib_on = 0; }
#endif
                    vib_step = 0;
                }
                if (ev.button == AP_BTN_B) return;
                timer_reset(t);
                continue;
            }

            switch (ev.button) {
                case AP_BTN_A:
                    if      (t->state == TIMER_IDLE)    timer_start(t,  SDL_GetTicks());
                    else if (t->state == TIMER_RUNNING) timer_pause(t,  SDL_GetTicks());
                    else if (t->state == TIMER_PAUSED)  timer_resume(t, SDL_GetTicks());
                    break;
                case AP_BTN_B:
                    if (t->state == TIMER_IDLE) return;
                    timer_reset(t);
                    break;
                case AP_BTN_UP:    timer_add_minutes(t,  1);  break;
                case AP_BTN_DOWN:  timer_add_minutes(t, -1);  break;
                case AP_BTN_RIGHT: timer_add_seconds(t,  10); break;
                case AP_BTN_LEFT:  timer_add_seconds(t, -10); break;
                case AP_BTN_MENU: {
                    int was_running = (t->state == TIMER_RUNNING);
                    if (was_running) timer_pause(t, SDL_GetTicks());
                    screen_settings(s);
                    if (was_running) timer_resume(t, SDL_GetTicks());
                    last_input = SDL_GetTicks();
                    break;
                }
                default: break;
            }
        }

        // ── Timer-Tick ───────────────────────────────────────────
        TimerState prev_state = t->state;
        timer_tick(t, SDL_GetTicks());

        // ── Alarm starten ────────────────────────────────────────
        if (t->state == TIMER_EXPIRED && prev_state == TIMER_RUNNING) {
            alert_active = 1;
            vib_step     = 0;
            vib_next_ms  = 0;
            if (scr != SCREEN_ON) {
                scr = SCREEN_ON;
                ap_set_power_handler(true);
#ifdef PLATFORM_TG5040
                backlight_on();
#endif
            }
            last_input = SDL_GetTicks();
        }

        // ── Alarm-Dauerschleife ──────────────────────────────────
        if (t->state == TIMER_EXPIRED && alert_active) {
            if (s->sound_enabled) {
                if (!alert_dev) {
                    alert_dev = start_alert_audio(s->volume);
                } else if (SDL_GetQueuedAudioSize(alert_dev) == 0) {
                    SDL_CloseAudioDevice(alert_dev);
                    alert_dev = start_alert_audio(s->volume);
                }
            }
#ifdef PLATFORM_TG5040
            if (s->vibration_enabled) {
                uint32_t now = SDL_GetTicks();
                if (now >= vib_next_ms) {
                    vib_on   = VIB_PAT[vib_step].on;
                    vib_set(vib_on, s->vibration_intensity);
                    vib_next_ms = now + VIB_PAT[vib_step].ms;
                    vib_step    = (vib_step + 1) % VIB_PAT_LEN;
                }
            }
#endif
        }

        // ── Screen-Timeout ───────────────────────────────────────
        uint32_t now    = SDL_GetTicks();
        int      was_on = (scr == SCREEN_ON);
        if (scr == SCREEN_ON && s->screen_timeout > 0
                && (now - last_input) >= (uint32_t)(s->screen_timeout * 1000)) {
            scr = SCREEN_OFF;
        }
        if (scr == SCREEN_HINT && (now - hint_start) >= 1000u) {
            scr = SCREEN_OFF;
#ifdef PLATFORM_TG5040
            backlight_off();
#endif
        }
        // Backlight + Power-Taste beim Zustandswechsel
        if (was_on && scr != SCREEN_ON) {
            ap_set_power_handler(false);
#ifdef PLATFORM_TG5040
            backlight_off();
#endif
        } else if (!was_on && scr == SCREEN_ON) {
            ap_set_power_handler(true);
#ifdef PLATFORM_TG5040
            backlight_on();
#endif
        }

        // ── Rendern ──────────────────────────────────────────────
        if (scr != SCREEN_ON) {
            render_screen_off(scr == SCREEN_HINT);
            SDL_Delay(50);
        } else {
            render_timer(t, alert_active, s);
            SDL_Delay(16);
        }
    }
}

// ================================================================
// Programmstart
// ================================================================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    ap_config cfg = {
        .window_title = "Next Timer",
        .font_path    = AP_PLATFORM_IS_DEVICE ? NULL : "apostrophe/res/font.ttf",
        .is_nextui    = AP_PLATFORM_IS_DEVICE,
        .cpu_speed    = AP_CPU_SPEED_MENU,
    };
    ap_init(&cfg);

    AppSettings settings;
    settings_load(&settings, SETTINGS_FILE);

    Timer timer;
    timer_init(&timer);

    screen_timer(&timer, &settings);

    ap_quit();
    return 0;
}
