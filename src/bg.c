/*
 * bg.c — nexttimer-bg: Hintergrund-Daemon für ablaufenden Timer
 *
 * Wird von main.c gestartet wenn der Nutzer die App schließt während der
 * Timer noch läuft.  Schläft bis der Timer abläuft, spielt dann den Alert
 * ab (3× Beep + Vibration auf TG5040), und beendet sich.
 *
 * Argumente: <remaining_seconds> <settings_path> <state_file_path>
 */

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>

#define ALERT_BEEP_MS  300
#define ALERT_GAP_MS   200
#define ALERT_PULSES   3

#ifdef PLATFORM_TG5040
/* Schreibt gpio227 (Vibrations-Motor) auf 0.  Wird auch im Signal-Handler
 * benutzt — async-signal-safe ist `write(2)` auf einen offenen FD, aber
 * `fopen`/`fprintf` sind es nicht.  Für unseren Use-Case (Daemon-Cleanup
 * bei SIGTERM) ist das ein akzeptabler Kompromiss, da der Daemon ohnehin
 * gleich beendet wird. */
static void motor_off(void) {
    FILE *f = fopen("/sys/class/gpio/gpio227/value", "w");
    if (f) { fprintf(f, "0\n"); fclose(f); }
}

/* SIGTERM/SIGINT-Handler: Motor abschalten und sofort beenden.  Verhindert
 * dass die Vibration weiterläuft, wenn der Daemon mitten in einem Puls
 * gekillt wird (z.B. vom Haupt-Prozess beim Neustart). */
static void cleanup_and_exit(int sig) {
    (void)sig;
    motor_off();
    _exit(0);
}
#endif

/* Liest einen Integer-Wert aus einer "key=value"-Datei. */
static int read_setting(const char *path, const char *key, int fallback) {
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    char k[32];
    long v;
    while (fscanf(f, " %31[^=]=%ld", k, &v) == 2) {
        if (strcmp(k, key) == 0) { fclose(f); return (int)v; }
    }
    fclose(f);
    return fallback;
}

/* Spielt den 3-Puls-Beep-Alert über SDL Audio ab. */
static void play_alert(int volume) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) return;

    SDL_AudioSpec want = {0}, got;
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!dev) { SDL_QuitSubSystem(SDL_INIT_AUDIO); return; }

    float  amplitude = 2800.0f * (float)volume;
    int    beep_s    = got.freq * ALERT_BEEP_MS / 1000;
    int    gap_s     = got.freq * ALERT_GAP_MS  / 1000;
    int    total     = (beep_s + gap_s) * ALERT_PULSES;
    Sint16 *buf      = malloc(total * sizeof(Sint16));
    if (!buf) { SDL_CloseAudioDevice(dev); SDL_QuitSubSystem(SDL_INIT_AUDIO); return; }

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

    /* Warten bis Wiedergabe abgeschlossen (max 3 s). */
    for (int i = 0; i < 300; i++) {
        if (SDL_GetQueuedAudioSize(dev) == 0) break;
        SDL_Delay(10);
    }

    SDL_CloseAudioDevice(dev);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

#ifdef PLATFORM_TG5040
/* Ein Vibrations-Puls: Motor an für BEEP_MS, dann aus für GAP_MS. */
static void vib_pulse(int intensity) {
    uint32_t voltage = 1000000u + (uint32_t)(intensity - 1) * ((3300000u - 1000000u) / 9u);
    FILE *f;

    f = fopen("/sys/class/motor/voltage", "w");
    if (f) { fprintf(f, "%u\n", voltage); fclose(f); }
    f = fopen("/sys/class/gpio/gpio227/value", "w");
    if (f) { fprintf(f, "1\n"); fclose(f); }
    usleep(ALERT_BEEP_MS * 1000u);

    f = fopen("/sys/class/gpio/gpio227/value", "w");
    if (f) { fprintf(f, "0\n"); fclose(f); }
    usleep(ALERT_GAP_MS * 1000u);
}
#endif

int main(int argc, char *argv[]) {
    if (argc < 4) return 1;

    int         remaining_s   = atoi(argv[1]);
    const char *settings_path = argv[2];
    const char *state_path    = argv[3];

#ifdef PLATFORM_TG5040
    /* Signal-Handler installieren BEVOR Vibration starten kann.  Wenn der
     * Haupt-Prozess uns mitten in einem Vibrations-Puls killt, schaltet der
     * Handler den Motor sauber ab — sonst bliebe gpio227 auf 1 hängen. */
    struct sigaction sa = {0};
    sa.sa_handler = cleanup_and_exit;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* KEIN SA_RESTART — sleep()/usleep() sollen unterbrochen werden */
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
#endif

    /* Auf Timer-Ende warten.  SIGTERM vom Haupt-Prozess beim Neustart
     * unterbricht sleep() — Daemon beendet sich dann sofort. */
    if (remaining_s > 0)
        sleep((unsigned)remaining_s);

    /* Einstellungen lesen. */
    int sound_on = read_setting(settings_path, "sound",         1);
    int volume   = read_setting(settings_path, "volume",        7);
    int vib_on   = read_setting(settings_path, "vibration",     1);
    int vib_int  = read_setting(settings_path, "vib_intensity", 7);

    /* Alert 3 × wiederholen: 3 Wiederholungen × 3 Pulses = 9 Beeps insgesamt. */
    for (int rep = 0; rep < 3; rep++) {
        if (sound_on && volume > 0)
            play_alert(volume);

#ifdef PLATFORM_TG5040
        if (vib_on) {
            for (int i = 0; i < ALERT_PULSES; i++)
                vib_pulse(vib_int);
        }
#endif
    }

#ifndef PLATFORM_TG5040
    (void)vib_on;
    (void)vib_int;
#endif

    /* State-Datei entfernen — signalisiert dem Haupt-Prozess beim nächsten
     * Start dass der Timer bereits abgelaufen und der Alert gespielt wurde. */
    remove(state_path);

    return 0;
}
