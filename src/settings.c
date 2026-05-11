#include "settings.h"
#include <stdio.h>
#include <string.h>

void settings_init(AppSettings *s) {
    s->sound_enabled        = 1;
    s->volume               = 7;
    s->vibration_enabled    = 1;
    s->vibration_intensity  = 7;
    s->screen_timeout       = 60;
    s->visual_alert         = 1;
}

void settings_load(AppSettings *s, const char *path) {
    settings_init(s);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char key[32];
    int  val;
    while (fscanf(f, " %31[^=]=%d", key, &val) == 2) {
        if (strcmp(key, "sound")          == 0) s->sound_enabled        = val;
        if (strcmp(key, "volume")         == 0) s->volume               = val;
        if (strcmp(key, "vibration")      == 0) s->vibration_enabled    = val;
        if (strcmp(key, "vib_intensity")  == 0) s->vibration_intensity  = val;
        if (strcmp(key, "screen_timeout") == 0) s->screen_timeout       = val;
        if (strcmp(key, "visual_alert")   == 0) s->visual_alert         = val;
    }
    fclose(f);
    if (s->volume               < 1)  s->volume               = 1;
    if (s->volume               > 10) s->volume               = 10;
    if (s->vibration_intensity  < 1)  s->vibration_intensity  = 1;
    if (s->vibration_intensity  > 10) s->vibration_intensity  = 10;
}

void settings_save(const AppSettings *s, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "sound=%d\nvolume=%d\nvibration=%d\nvib_intensity=%d\nscreen_timeout=%d\nvisual_alert=%d\n",
            s->sound_enabled, s->volume,
            s->vibration_enabled, s->vibration_intensity,
            s->screen_timeout, s->visual_alert);
    fclose(f);
}
