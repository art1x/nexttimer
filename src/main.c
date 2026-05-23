// ================================================================
// main.c  –  Next Timer for TrimUI Brick / NextUI
//
// Timer-Screen:
//   ↑ / ↓      → ±1 minute
//   ← / →      → ±10 seconds
//   A           → Start / Pause / Resume
//   B           → Reset / Exit (when idle)
//   MENU        → Settings
//   L1 / R1     → Switch to Stopwatch screen
//   Power       → Screen on/off (timer continues)
//
// Stopwatch-Screen:
//   A           → Start / Pause
//   B           → Reset
//   MENU        → Settings
//   L1 / R1     → Switch to Timer screen
//   Power       → Screen on/off
// ================================================================

#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"

#include "timer.h"
#include "settings.h"
#include "screen.h"
#include "stopwatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>


#define SETTINGS_FILE "./settings.txt"

/* ── Hintergrund-Daemon: State-Datei ─────────────────────────────────────
 *
 * Wenn der Nutzer die App schließt während der Timer läuft, startet main.c
 * "nexttimer-bg" als Hintergrundprozess.  Die State-Datei enthält den
 * Timer-Zustand zum Zeitpunkt des Schließens, damit beim nächsten Start
 * der Timer korrekt wiederhergestellt werden kann.
 */

/* Pfad zur State-Datei: PAK_DIR/nexttimer.state (oder ./nexttimer.state) */
static char g_state_path[512];

static void init_state_path(void) {
    const char *pak = getenv("PAK_DIR");
    if (pak && pak[0])
        snprintf(g_state_path, sizeof(g_state_path), "%s/nexttimer.state", pak);
    else
        snprintf(g_state_path, sizeof(g_state_path), "./nexttimer.state");
}

/* Liest die State-Datei und stellt Timer und Stoppuhr wieder her.
 * Falls der Daemon noch läuft: Timer wird fortgesetzt.
 * Falls der Daemon schon beendet ist: Timer zeigt EXPIRED an. */
static void restore_state(Timer *t, Stopwatch *sw) {
    FILE *f = fopen(g_state_path, "r");
    if (!f) return;

    int  remaining_ms = 0, total_ms = 0, set_min = 5, set_sec = 0, pid = 0;
    long sw_elapsed_ms = 0, sw_running = 0, exit_time = 0;
    char key[32]; long val;
    while (fscanf(f, " %31[^=]=%ld", key, &val) == 2) {
        if (strcmp(key, "remaining_ms")  == 0) remaining_ms  = (int)val;
        if (strcmp(key, "total_ms")      == 0) total_ms      = (int)val;
        if (strcmp(key, "set_minutes")   == 0) set_min       = (int)val;
        if (strcmp(key, "set_seconds")   == 0) set_sec       = (int)val;
        if (strcmp(key, "pid")           == 0) pid           = (int)val;
        if (strcmp(key, "exit_time")     == 0) exit_time     = val;
        if (strcmp(key, "sw_elapsed_ms") == 0) sw_elapsed_ms = val;
        if (strcmp(key, "sw_running")    == 0) sw_running    = val;
    }
    fclose(f);
    remove(g_state_path);

    /* Stoppuhr wiederherstellen — Zeit seit App-Ende addieren falls sie lief.
     * Falls sie lief: weiterlaufen lassen, sonst pausiert. */
    if (sw_elapsed_ms > 0 || sw_running) {
        long since_exit_ms = (long)(time(NULL) - (time_t)exit_time) * 1000L;
        sw->elapsed_ms = (uint32_t)(sw_elapsed_ms + (sw_running ? since_exit_ms : 0));
        sw->running    = sw_running ? true : false;
        sw->start_ms   = sw->running ? SDL_GetTicks() : 0;
    }

    /* Timer nur wiederherstellen wenn beim Beenden ein Daemon gestartet wurde
     * (pid > 0).  pid=0 bedeutet: nur Stoppuhr war aktiv, Timer-Zustand bleibt
     * unverändert (Default aus timer_init). */
    if (pid <= 0 || remaining_ms <= 0 || total_ms <= 0) return;

    /* Prüfen ob Daemon noch läuft (kill(pid, 0) liefert 0 wenn Prozess existiert). */
    int daemon_alive = (kill(pid, 0) == 0);

    if (daemon_alive) {
        /* Timer noch nicht abgelaufen — verbleibende Zeit berechnen. */
        long elapsed_ms = (long)(time(NULL) - (time_t)exit_time) * 1000L;
        int  corrected  = remaining_ms - (int)elapsed_ms;
        kill(pid, SIGTERM);   /* Daemon stoppen — wir übernehmen den Timer wieder */
        if (corrected > 0) {
            timer_restore(t, corrected, total_ms, set_min, set_sec, SDL_GetTicks());
        } else {
            t->state        = TIMER_EXPIRED;
            t->remaining_ms = 0;
        }
    } else {
        /* Daemon bereits beendet — Alert wurde gespielt. EXPIRED anzeigen. */
        t->state        = TIMER_EXPIRED;
        t->remaining_ms = 0;
    }
}

/* Schreibt State-Datei (Timer + Stoppuhr).  Startet nexttimer-bg nur wenn
 * der Timer noch läuft (für den Alert beim Ablauf).  Bei laufender Stoppuhr
 * ohne aktiven Timer wird nur der Stand gespeichert. */
static void save_state_and_launch_daemon(const Timer *t, const Stopwatch *sw,
                                         uint32_t now_ms) {
    bool timer_active = (t->state == TIMER_RUNNING || t->state == TIMER_PAUSED);

    /* Wichtig: SIGCHLD ignorieren damit der Kind-Prozess kein Zombie wird. */
    if (timer_active) signal(SIGCHLD, SIG_IGN);

    /* Stoppuhr: aktuellen Stand berechnen (falls sie läuft). */
    long sw_elapsed = (long)stopwatch_get_ms(sw, now_ms);
    int  sw_running = sw->running ? 1 : 0;
    long exit_ts    = (long)time(NULL);

    /* State speichern (vorerst pid=0, wird gleich aktualisiert wenn Daemon startet). */
    FILE *f = fopen(g_state_path, "w");
    if (f) {
        fprintf(f, "remaining_ms=%d\ntotal_ms=%d\nset_minutes=%d\nset_seconds=%d\n"
                   "sw_elapsed_ms=%ld\nsw_running=%d\npid=0\nexit_time=%ld\n",
                t->remaining_ms, t->total_ms, t->set_minutes, t->set_seconds,
                sw_elapsed, sw_running, exit_ts);
        fclose(f);
    }

    if (!timer_active) return;   /* Nur Stoppuhr: kein Daemon nötig */

    /* Daemon starten: schläft remaining_seconds, spielt dann Alert. */
    int  remaining_s = (t->remaining_ms + 999) / 1000;   /* aufrunden */
    char secs_str[32];
    snprintf(secs_str, sizeof(secs_str), "%d", remaining_s);

    pid_t pid = fork();
    if (pid == 0) {
        /* Kind-Prozess: Daemon starten. */
        execl("./nexttimer-bg", "nexttimer-bg",
              secs_str, SETTINGS_FILE, g_state_path, NULL);
        _exit(1);   /* exec fehlgeschlagen */
    }

    if (pid > 0) {
        /* Eltern-Prozess: State-Datei mit Daemon-PID aktualisieren. */
        f = fopen(g_state_path, "w");
        if (f) {
            fprintf(f, "remaining_ms=%d\ntotal_ms=%d\nset_minutes=%d\nset_seconds=%d\n"
                       "sw_elapsed_ms=%ld\nsw_running=%d\npid=%d\nexit_time=%ld\n",
                    t->remaining_ms, t->total_ms, t->set_minutes, t->set_seconds,
                    sw_elapsed, sw_running, (int)pid, exit_ts);
            fclose(f);
        }
    }
}

/* Flag das von SIGTERM/SIGINT gesetzt wird, um den Main-Loop zu beenden. */
static volatile sig_atomic_t g_should_exit = 0;

/* L3 / R3 (Analog-Stick-Klicks) werden von Apostrophe nicht abgebildet —
 * wir fangen sie über SDL_AddEventWatch ab und setzen Flags, die der
 * Main-Loop nach dem Input-Loop abfragt.  Wirken wie L1 / R1.
 *
 * Auf TG5040/NextUI können Stick-Klicks als SDL_CONTROLLERBUTTONDOWN oder
 * als SDL_JOYBUTTONDOWN ankommen — wir behandeln beides. */
static volatile int g_l3_pressed = 0;
static volatile int g_r3_pressed = 0;

/* Joystick-Button-IDs für L3/R3 auf typischen Controllern.
 * SDL2 Standard: L3=9, R3=10 (variiert je nach Mapping). */
#define JOY_BTN_L3  9
#define JOY_BTN_R3  10

static int SDLCALL stick_click_watch(void *userdata, SDL_Event *ev) {
    (void)userdata;
    if (ev->type == SDL_CONTROLLERBUTTONDOWN) {
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK)  g_l3_pressed = 1;
        if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) g_r3_pressed = 1;
        /* Debug: alle Controller-Button-Events ins Log */
        FILE *lg = fopen("./input_debug.log", "a");
        if (lg) { fprintf(lg, "CTRL btn=%d\n", ev->cbutton.button); fclose(lg); }
    } else if (ev->type == SDL_JOYBUTTONDOWN) {
        if (ev->jbutton.button == JOY_BTN_L3) g_l3_pressed = 1;
        if (ev->jbutton.button == JOY_BTN_R3) g_r3_pressed = 1;
        FILE *lg = fopen("./input_debug.log", "a");
        if (lg) { fprintf(lg, "JOY btn=%d\n", ev->jbutton.button); fclose(lg); }
    } else if (ev->type == SDL_KEYDOWN) {
        FILE *lg = fopen("./input_debug.log", "a");
        if (lg) { fprintf(lg, "KEY sc=%d sym=%d\n",
                          ev->key.keysym.scancode, ev->key.keysym.sym); fclose(lg); }
    }
    return 1;   /* Event im Queue belassen */
}

static void sigterm_handler(int sig) {
    (void)sig;
    g_should_exit = 1;
}

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

static void render_screen_off(void) {
    ap_color blk = {0, 0, 0, 255};
    ap_draw_rect(0, 0, ap_get_screen_width(), ap_get_screen_height(), blk);
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
// L1/R1 Navigationspfeile — auf beiden Screens links und rechts
// ----------------------------------------------------------------
static void draw_nav_arrows(TTF_Font *fsm, int sw, int sh, ap_color col) {
    if (!fsm) return;
    int fh = TTF_FontHeight(fsm);
    int y  = sh / 2 - fh / 2;
    /* Linker Pfeil ◄ L1 */
    ap_draw_text(fsm, "< L1", AP_S(8), y, col);
    /* Rechter Pfeil R1 ► */
    int rw = ap_measure_text(fsm, "R1 >");
    ap_draw_text(fsm, "R1 >", sw - rw - AP_S(8), y, col);
}

// ----------------------------------------------------------------
// Timer-Bildschirm zeichnen (ohne ap_present — für Swipe-Textur)
// ----------------------------------------------------------------
static void draw_timer_frame(const Timer *t, int alert_active, const AppSettings *s) {
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
            hint = "A:Pause  X:Reset  B:Exit (background)  Menu:Settings";
            break;
        case TIMER_PAUSED:
            hint = "A:Resume  X:Reset  B:Exit (background)  Menu:Settings";
            break;
        case TIMER_EXPIRED:
            hint = alert_active
                   ? "Any button: stop alarm  B:Exit"
                   : "A:Restart  B:Exit";
            break;
    }
    if (fsm && hint[0]) {
        int hw = ap_measure_text(fsm, hint);
        ap_draw_text(fsm, hint, (sw - hw) / 2,
                     sh - TTF_FontHeight(fsm) - AP_S(10), thm->hint);
    }

    draw_nav_arrows(fsm, sw, sh, thm->hint);
}

static void render_timer(const Timer *t, int alert_active, const AppSettings *s) {
    draw_timer_frame(t, alert_active, s);
    ap_present();
}

// ----------------------------------------------------------------
// Stoppuhr-Bildschirm zeichnen (ohne ap_present — für Swipe-Textur)
// ----------------------------------------------------------------
static void draw_stopwatch_frame(const Stopwatch *sw_state) {
    ap_draw_background();
    ap_draw_status_bar(&g_status_bar);

    int      sw   = ap_get_screen_width();
    int      sh   = ap_get_screen_height();
    ap_theme *thm = ap_get_theme();

    TTF_Font *fxl  = ap_get_font(AP_FONT_EXTRA_LARGE);
    TTF_Font *fmed = ap_get_font(AP_FONT_MEDIUM);
    TTF_Font *fsm  = ap_get_font(AP_FONT_SMALL);

    ap_draw_text(fsm, "Stopwatch", 16, 12, thm->hint);

    /* Selbe doppelt-große Schrift wie Timer-Screen */
    static TTF_Font *s_big_sw = NULL;
    if (!s_big_sw && fxl && thm->font_path[0]) {
        int pt = TTF_FontHeight(fxl) * 2;
        s_big_sw = TTF_OpenFont(thm->font_path, pt);
    }
    TTF_Font *fnum = s_big_sw ? s_big_sw : fxl;

    if (fnum) {
        /* HH:MM:SS.x — Zehntelsekunden */
        uint32_t total_ms  = stopwatch_get_ms(sw_state, SDL_GetTicks());
        uint32_t tenths    = (total_ms / 100) % 10;
        uint32_t secs      = (total_ms / 1000) % 60;
        uint32_t mins      = (total_ms / 60000) % 60;
        uint32_t hours     = total_ms / 3600000u;

        char time_str[16];
        if (hours > 0)
            snprintf(time_str, sizeof(time_str), "%u:%02u:%02u.%u", hours, mins, secs, tenths);
        else
            snprintf(time_str, sizeof(time_str), "%02u:%02u.%u", mins, secs, tenths);

        int tw = ap_measure_text(fnum, time_str);
        int fh = TTF_FontHeight(fnum);
        int ty = sh / 2 - fh / 2 - AP_S(10);
        ap_draw_text(fnum, time_str, (sw - tw) / 2, ty, thm->text);

        /* Status-Text */
        const char *status = sw_state->running ? "Running" : (sw_state->elapsed_ms > 0 ? "Paused" : "Ready");
        if (fmed) {
            int stw = ap_measure_text(fmed, status);
            ap_draw_text(fmed, status, (sw - stw) / 2, ty + fh + AP_S(18), thm->hint);
        }
    }

    /* Tasten-Hinweise unten */
    const char *hint = sw_state->running
                       ? "A:Pause  X:Reset  B:Exit"
                       : "A:Start  X:Reset  B:Exit";
    if (fsm && hint[0]) {
        int hw = ap_measure_text(fsm, hint);
        ap_draw_text(fsm, hint, (sw - hw) / 2,
                     sh - TTF_FontHeight(fsm) - AP_S(10), thm->hint);
    }

    draw_nav_arrows(fsm, sw, sh, thm->hint);
}

static void render_stopwatch(const Stopwatch *sw_state) {
    draw_stopwatch_frame(sw_state);
    ap_present();
}

// ----------------------------------------------------------------
// Swipe-Animation zwischen zwei Screens
// ----------------------------------------------------------------

/* Erstellt eine Render-Textur in Bildschirmgröße.
 * Gibt NULL zurück wenn der Renderer TARGETTEXTURE nicht unterstützt. */
static SDL_Texture *create_screen_texture(void) {
    SDL_Renderer *r = ap_get_renderer();
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(r, &info) < 0) return NULL;
    if (!(info.flags & SDL_RENDERER_TARGETTEXTURE)) return NULL;
    return SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                             SDL_TEXTUREACCESS_TARGET,
                             ap_get_screen_width(), ap_get_screen_height());
}

/* Rendert einen Swipe-Frame: from-Textur fährt in Richtung dir raus,
 * to-Textur kommt von der anderen Seite herein.
 * dir = -1: Bewegung nach links (L1 gedrückt)
 * dir = +1: Bewegung nach rechts (R1 gedrückt)
 * progress: 0.0 = Anfang, 1.0 = Ende */
static void render_swipe_frame(SDL_Texture *from, SDL_Texture *to,
                                float progress, int dir) {
    SDL_Renderer *r  = ap_get_renderer();
    int           sw = ap_get_screen_width();
    int           sh = ap_get_screen_height();
    int           offset = (int)(progress * (float)sw);

    SDL_Rect src = {0, 0, sw, sh};
    SDL_Rect dst_from, dst_to;

    if (dir < 0) {
        /* L1: alles nach links schieben — from geht nach links, to kommt von rechts */
        dst_from = (SDL_Rect){-offset, 0, sw, sh};
        dst_to   = (SDL_Rect){sw - offset, 0, sw, sh};
    } else {
        /* R1: alles nach rechts schieben — from geht nach rechts, to kommt von links */
        dst_from = (SDL_Rect){offset, 0, sw, sh};
        dst_to   = (SDL_Rect){offset - sw, 0, sw, sh};
    }

    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderCopy(r, from, &src, &dst_from);
    SDL_RenderCopy(r, to,   &src, &dst_to);
    SDL_RenderPresent(r);
}

// ----------------------------------------------------------------
// Einstellungen rendern
// ----------------------------------------------------------------
static void render_settings(int cursor, const AppSettings *s) {
    static const char *labels[5] = {
        "Sound", "Volume", "Vibration", "Vib. Intensity", "Visual Alarm"
    };
    static const int is_numeric[5] = {1, 1, 1, 1, 1};

    char vals[5][32];
    snprintf(vals[0], sizeof(vals[0]), "%s",      s->sound_enabled     ? "On" : "Off");
    snprintf(vals[1], sizeof(vals[1]), "%d / 10", s->volume);
    snprintf(vals[2], sizeof(vals[2]), "%s",      s->vibration_enabled ? "On" : "Off");
    snprintf(vals[3], sizeof(vals[3]), "%d / 10", s->vibration_intensity);
    snprintf(vals[4], sizeof(vals[4]), "%s",      s->visual_alert      ? "On" : "Off");

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

    for (int i = 0; i < 5; i++) {
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
                    cursor = (cursor > 0) ? cursor - 1 : 4;
                    break;
                case AP_BTN_DOWN:
                    cursor = (cursor < 4) ? cursor + 1 : 0;
                    break;
                case AP_BTN_LEFT:
                    switch (cursor) {
                        case 0: s->sound_enabled       ^= 1; break;
                        case 1: s->volume               = (s->volume              > 1) ? s->volume              - 1 : 10; break;
                        case 2: s->vibration_enabled   ^= 1; break;
                        case 3: s->vibration_intensity  = (s->vibration_intensity > 1) ? s->vibration_intensity - 1 : 10; break;
                        case 4: s->visual_alert        ^= 1; break;
                    }
                    break;
                case AP_BTN_RIGHT:
                    switch (cursor) {
                        case 0: s->sound_enabled       ^= 1; break;
                        case 1: s->volume               = (s->volume              < 10) ? s->volume              + 1 : 1; break;
                        case 2: s->vibration_enabled   ^= 1; break;
                        case 3: s->vibration_intensity  = (s->vibration_intensity < 10) ? s->vibration_intensity + 1 : 1; break;
                        case 4: s->visual_alert        ^= 1; break;
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
// Swipe-Trigger: Snapshots beider Screens in Texturen rendern + Animation starten.
// Wird von L1/R1 (Input-Events) und L3/R3 (EventWatch-Flags) aufgerufen.
// ----------------------------------------------------------------
typedef enum { SCREEN_TIMER = 0, SCREEN_STOPWATCH = 1 } ScreenID;

static void start_screen_swipe(int dir,
                               ScreenID  *current_screen,
                               ScreenID  *swipe_to,
                               bool      *swipe_active,
                               float     *swipe_progress,
                               int       *swipe_dir,
                               SDL_Texture **swipe_tex_from,
                               SDL_Texture **swipe_tex_to,
                               uint32_t  *swipe_start_ms,
                               const Timer     *t,
                               int              alert_active,
                               const AppSettings *s,
                               const Stopwatch *sw_state) {
    *swipe_dir = dir;
    *swipe_to  = (*current_screen == SCREEN_TIMER) ? SCREEN_STOPWATCH : SCREEN_TIMER;

    SDL_Texture *ta = create_screen_texture();
    SDL_Texture *tb = create_screen_texture();

    if (ta && tb) {
        SDL_Renderer *r = ap_get_renderer();
        SDL_SetRenderTarget(r, ta);
        if (*current_screen == SCREEN_TIMER) draw_timer_frame(t, alert_active, s);
        else                                  draw_stopwatch_frame(sw_state);

        SDL_SetRenderTarget(r, tb);
        if (*swipe_to == SCREEN_TIMER) draw_timer_frame(t, alert_active, s);
        else                            draw_stopwatch_frame(sw_state);

        SDL_SetRenderTarget(r, NULL);

        if (*swipe_tex_from) SDL_DestroyTexture(*swipe_tex_from);
        if (*swipe_tex_to)   SDL_DestroyTexture(*swipe_tex_to);
        *swipe_tex_from  = ta;
        *swipe_tex_to    = tb;
        *swipe_active    = true;
        *swipe_progress  = 0.0f;
        *swipe_start_ms  = SDL_GetTicks();
    } else {
        if (ta) SDL_DestroyTexture(ta);
        if (tb) SDL_DestroyTexture(tb);
        *current_screen = *swipe_to;
    }
}

// ----------------------------------------------------------------
// Haupt-Schleife: Timer + Stoppuhr, Screen-Wechsel mit Swipe
// ----------------------------------------------------------------

#define SWIPE_DURATION_MS 200.0f

static void screen_timer(Timer *t, Stopwatch *sw_init, AppSettings *s) {
    Stopwatch         sw_state = *sw_init;   /* wiederhergestellten Zustand übernehmen */

    ScreenID          current_screen = SCREEN_TIMER;

    /* Swipe-Zustand */
    bool          swipe_active   = false;
    float         swipe_progress = 0.0f;
    int           swipe_dir      = 0;          /* -1 = links (L1), +1 = rechts (R1) */
    ScreenID      swipe_to       = SCREEN_STOPWATCH;
    SDL_Texture  *swipe_tex_from = NULL;
    SDL_Texture  *swipe_tex_to   = NULL;
    uint32_t      swipe_start_ms = 0;

    /* Timer-Alert-Zustand */
    int               alert_active  = 0;
    SDL_AudioDeviceID alert_dev     = 0;
    int               vib_on        = 0;
    int               vib_step      = 0;
    uint32_t          vib_next_ms   = 0;

    /* Falls Timer schon bei EXPIRED wiederhergestellt wurde: Alert starten */
    if (t->state == TIMER_EXPIRED) {
        alert_active = 1;
        screen_on();
    }

    while (1) {
        /* Apostrophe rendert standardmäßig nur on-demand (Idle-Mode mit
         * SDL_WaitEventTimeout).  Wenn etwas animiert oder läuft, müssen wir
         * aktives 60fps-Pacing anfordern — sonst wartet ap_present() auf
         * Input-Events und Timer/Stoppuhr ruckeln. */
        if (t->state == TIMER_RUNNING || sw_state.running
            || swipe_active || alert_active) {
            ap_request_frame();
        }

        // ── Eingabe (nur wenn kein Swipe läuft) ──────────────────
        if (!swipe_active) {
            ap_input_event ev;
            while (ap_poll_input(&ev)) {
                if (!ev.pressed) continue;

                /* Screen aus: Eingabe ignorieren (Power-Thread behandelt Power) */
                if (screen_is_off()) continue;

                /* L1 / R1: Screen wechseln mit Swipe-Animation.
                 * L3 / R3 werden weiter unten via Flag-Check verarbeitet (gleicher Effekt). */
                if (ev.button == AP_BTN_L1 || ev.button == AP_BTN_R1) {
                    int dir = (ev.button == AP_BTN_L1) ? 1 : -1;
                    start_screen_swipe(dir, &current_screen, &swipe_to,
                                       &swipe_active, &swipe_progress, &swipe_dir,
                                       &swipe_tex_from, &swipe_tex_to, &swipe_start_ms,
                                       t, alert_active, s, &sw_state);
                    continue;
                }

                /* MENU: Einstellungen (von beiden Screens aus) */
                if (ev.button == AP_BTN_MENU) {
                    int was_running = (t->state == TIMER_RUNNING);
                    if (was_running) timer_pause(t, SDL_GetTicks());
                    screen_settings(s);
                    if (was_running) timer_resume(t, SDL_GetTicks());
                    continue;
                }

                /* B: App beenden — Timer/Stoppuhr laufen im Hintergrund weiter falls aktiv */
                if (ev.button == AP_BTN_B) {
                    if (t->state == TIMER_RUNNING || t->state == TIMER_PAUSED || sw_state.running)
                        save_state_and_launch_daemon(t, &sw_state, SDL_GetTicks());
                    return;
                }

                /* Screen-spezifische Eingabe */
                if (current_screen == SCREEN_STOPWATCH) {
                    switch (ev.button) {
                        case AP_BTN_A:
                            if (sw_state.running) {
                                stopwatch_pause(&sw_state, SDL_GetTicks());
                                if (t->state != TIMER_RUNNING)
                                    screen_set_suspend_lock(false);
                            } else {
                                stopwatch_start(&sw_state, SDL_GetTicks());
                                screen_set_suspend_lock(true);
                            }
                            break;
                        case AP_BTN_X:
                            stopwatch_reset(&sw_state);
                            if (t->state != TIMER_RUNNING)
                                screen_set_suspend_lock(false);
                            break;
                        default: break;
                    }
                } else {
                    /* Timer-Screen-Eingabe */

                    /* Timer abgelaufen: jede Taste stoppt den Alarm */
                    if (t->state == TIMER_EXPIRED) {
                        if (alert_active) {
                            alert_active = 0;
                            if (alert_dev) { SDL_CloseAudioDevice(alert_dev); alert_dev = 0; }
#ifdef PLATFORM_TG5040
                            if (vib_on) { vib_set(0, s->vibration_intensity); vib_on = 0; }
#endif
                            vib_step = 0;
                        }
                        timer_reset(t);
                        continue;
                    }

                    switch (ev.button) {
                        case AP_BTN_A:
                            if (t->state == TIMER_IDLE) {
                                timer_start(t, SDL_GetTicks());
                                screen_set_suspend_lock(true);
                            } else if (t->state == TIMER_RUNNING) {
                                timer_pause(t, SDL_GetTicks());
                                if (!sw_state.running) screen_set_suspend_lock(false);
                            } else if (t->state == TIMER_PAUSED) {
                                timer_resume(t, SDL_GetTicks());
                                screen_set_suspend_lock(true);
                            }
                            break;
                        case AP_BTN_X:
                            timer_reset(t);
                            if (!sw_state.running) screen_set_suspend_lock(false);
                            break;
                        case AP_BTN_UP:    timer_add_minutes(t,  1);  break;
                        case AP_BTN_DOWN:  timer_add_minutes(t, -1);  break;
                        case AP_BTN_RIGHT: timer_add_seconds(t,  10); break;
                        case AP_BTN_LEFT:  timer_add_seconds(t, -10); break;
                        default: break;
                    }
                }
            }
        }

        // ── L3 / R3 (Stick-Klicks) verhalten sich wie L1 / R1 ────
        if (!swipe_active && !screen_is_off()) {
            if (g_l3_pressed) {
                g_l3_pressed = 0;
                start_screen_swipe(1, &current_screen, &swipe_to,
                                   &swipe_active, &swipe_progress, &swipe_dir,
                                   &swipe_tex_from, &swipe_tex_to, &swipe_start_ms,
                                   t, alert_active, s, &sw_state);
            } else if (g_r3_pressed) {
                g_r3_pressed = 0;
                start_screen_swipe(-1, &current_screen, &swipe_to,
                                   &swipe_active, &swipe_progress, &swipe_dir,
                                   &swipe_tex_from, &swipe_tex_to, &swipe_start_ms,
                                   t, alert_active, s, &sw_state);
            }
        } else {
            /* Flags zurücksetzen wenn Eingabe ignoriert wird, damit sie nicht aufstauen. */
            g_l3_pressed = 0;
            g_r3_pressed = 0;
        }

        // ── Timer-Tick ───────────────────────────────────────────
        TimerState prev_state = t->state;
        timer_tick(t, SDL_GetTicks());

        // ── Alarm starten ────────────────────────────────────────
        if (t->state == TIMER_EXPIRED && prev_state == TIMER_RUNNING) {
            alert_active   = 1;
            vib_step       = 0;
            vib_next_ms    = 0;
            current_screen = SCREEN_TIMER;
            swipe_active   = false;
            screen_set_suspend_lock(false);
            screen_on();
        }

        // ── Alarm: Pattern wiederholen bis Taste gedrückt ────────
        if (t->state == TIMER_EXPIRED && alert_active) {
            if (s->sound_enabled) {
                if (!alert_dev) {
                    alert_dev = start_alert_audio(s->volume);
                } else if (SDL_GetQueuedAudioSize(alert_dev) == 0) {
                    /* Durchlauf fertig — sofort neu starten. */
                    SDL_CloseAudioDevice(alert_dev);
                    alert_dev = 0;
                    alert_dev = start_alert_audio(s->volume);
                }
            }
#ifdef PLATFORM_TG5040
            if (s->vibration_enabled) {
                uint32_t now = SDL_GetTicks();
                if (now >= vib_next_ms) {
                    vib_on = VIB_PAT[vib_step].on;
                    vib_set(vib_on, s->vibration_intensity);
                    vib_next_ms = now + VIB_PAT[vib_step].ms;
                    vib_step++;
                    if (vib_step >= VIB_PAT_LEN)
                        vib_step = 0;   /* Nächste Runde */
                }
            }
#endif
        }

        // ── Rendern ──────────────────────────────────────────────
        if (screen_is_off()) {
            render_screen_off();
            SDL_Delay(50);
        } else if (swipe_active) {
            /* Swipe-Fortschritt berechnen */
            float elapsed = (float)(SDL_GetTicks() - swipe_start_ms);
            swipe_progress = elapsed / SWIPE_DURATION_MS;
            if (swipe_progress >= 1.0f) {
                /* Swipe abgeschlossen */
                swipe_progress = 1.0f;
                swipe_active   = false;
                current_screen = swipe_to;
                if (swipe_tex_from) { SDL_DestroyTexture(swipe_tex_from); swipe_tex_from = NULL; }
                if (swipe_tex_to)   { SDL_DestroyTexture(swipe_tex_to);   swipe_tex_to   = NULL; }
                /* Ziel-Screen normal rendern */
                if (current_screen == SCREEN_TIMER)
                    render_timer(t, alert_active, s);
                else
                    render_stopwatch(&sw_state);
            } else {
                render_swipe_frame(swipe_tex_from, swipe_tex_to, swipe_progress, swipe_dir);
            }
            SDL_Delay(16);
        } else {
            if (current_screen == SCREEN_TIMER)
                render_timer(t, alert_active, s);
            else
                render_stopwatch(&sw_state);
            SDL_Delay(16);
        }

        // ── SIGTERM/SIGINT: App sauber beenden ───────────────────
        if (g_should_exit) {
            if (t->state == TIMER_RUNNING || t->state == TIMER_PAUSED || sw_state.running)
                save_state_and_launch_daemon(t, &sw_state, SDL_GetTicks());
            if (alert_dev) { SDL_CloseAudioDevice(alert_dev); alert_dev = 0; }
#ifdef PLATFORM_TG5040
            if (vib_on) { vib_set(0, s->vibration_intensity); vib_on = 0; }
#endif
            break;
        }
    }

    /* Swipe-Texturen freigeben */
    if (swipe_tex_from) SDL_DestroyTexture(swipe_tex_from);
    if (swipe_tex_to)   SDL_DestroyTexture(swipe_tex_to);
}

// ================================================================
// Programmstart
// ================================================================
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* Signal-Handler für sauberes Beenden (NextUI sendet SIGTERM beim Wechsel). */
    signal(SIGTERM, sigterm_handler);
    signal(SIGINT,  sigterm_handler);

    /* State-Datei-Pfad initialisieren (abhängig von PAK_DIR-Umgebungsvariable). */
    init_state_path();

    ap_config cfg = {
        .window_title = "Next Timer",
        .font_path    = AP_PLATFORM_IS_DEVICE ? NULL : "apostrophe/res/font.ttf",
        .is_nextui    = AP_PLATFORM_IS_DEVICE,
        .cpu_speed    = AP_CPU_SPEED_MENU,
    };
    ap_init(&cfg);
    ap_set_power_handler(false);   /* Power-Thread in screen.c übernimmt */
    screen_init();
    screen_init_power();

    /* L3 / R3 abfangen — Apostrophe ignoriert Stick-Klicks. */
    SDL_AddEventWatch(stick_click_watch, NULL);

    AppSettings settings;
    settings_load(&settings, SETTINGS_FILE);

    Timer     timer;
    Stopwatch stopwatch;
    timer_init(&timer);
    stopwatch_init(&stopwatch);

    /* Gespeicherten Zustand wiederherstellen (falls App vorher mit laufendem
     * Timer beendet wurde). */
    restore_state(&timer, &stopwatch);

    /* Falls Timer oder Stoppuhr nach Restore aktiv ist: Suspend-Lock setzen. */
    if (timer.state == TIMER_RUNNING || stopwatch.running)
        screen_set_suspend_lock(true);

    screen_timer(&timer, &stopwatch, &settings);

    screen_set_suspend_lock(false);
    screen_quit_power();
    ap_quit();
    return 0;
}
