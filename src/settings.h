#pragma once

typedef struct {
    int sound_enabled;
    int volume;              /* 1-10 */
    int vibration_enabled;
    int vibration_intensity; /* 1-10 */
    int screen_timeout;      /* Sekunden bis Display aus, 0 = nie */
    int visual_alert;        /* 0 = aus, 1 = rotes Blinken bei Ablauf */
} AppSettings;

void settings_init(AppSettings *s);
void settings_load(AppSettings *s, const char *path);
void settings_save(const AppSettings *s, const char *path);
