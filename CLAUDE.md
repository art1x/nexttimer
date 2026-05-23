# Next Timer

Minimalist countdown timer + stopwatch for TrimUI Brick/Hammer running NextUI OS.

---

## For Claude

> **Maintenance:** This file is actively maintained by Claude throughout development.
> Architecture decisions, format/API choices, and significant implementation progress are documented here.
> Update this file whenever a decision is made or a module reaches a stable state.

> **Code style:** User is learning to program. All code must be thoroughly commented —
> explain what each block does, why design decisions were made, and how non-obvious APIs work.
> Prefer clarity over brevity in comments.

> **Version control:** Claude handles git commits after each completed module/feature.
> Always ask the user for confirmation before committing.

> **Context management:** Remind the user to `/clear` after each completed module/feature
> so the next step starts with a clean context window, using only CLAUDE.md as foundation.

---

## Project

- **Platform:** TrimUI Brick/Hammer (Build target: TG5040 only)
- **OS:** NextUI
- **UI language:** German/English mixed (comments German, UI English)
- **Submodules:** `NextUI/` (primary reference), `Apostrophe/` (UI framework)

---

## Architecture

- **UI:** Apostrophe (header-only SDL2 toolkit)
  - Theme: `ap_theme_load_nextui()` + `ap_get_theme()` — follows active NextUI theme
  - Resolution: 1024×768
- **Build:** Docker cross-compile via `ghcr.io/loveretro/tg5040-toolchain`
- **Two binaries:** `nexttimer` (main app) + `nexttimer-bg` (background daemon)

---

## Development

### NextUI Environment Variables

Available in `launch.sh` and passed to the binary:

| Variable | Inhalt |
|---|---|
| `$PAK_DIR` | Pfad zum `.pak` Ordner selbst |
| `$PAK_NAME` | Name des Paks (aus Ordnername) |
| `$SDCARD_PATH` | Root der SD-Karte (`/mnt/SDCARD`) |
| `$USERDATA_PATH` | Persistenter Speicher: `/mnt/SDCARD/.userdata/tg5040` |
| `$SHARED_USERDATA_PATH` | Geteilter Speicher zwischen Paks |
| `$LOGS_PATH` | Log-Verzeichnis |
| `$PLATFORM` | Gerät: `tg5040` |

`launch.sh` muss `#!/bin/sh` verwenden (kein bash auf dem Gerät).

### Build Notes

- `make linux` — lokaler Build zum schnellen Kompilier-Check (wird **nicht** deployed).
- `make tg5040` — cross-compiliert für TG5040 via Docker, kann direkt aus VS Code gestartet werden.
  Output: `build/tg5040/Next Timer.pak/` — enthält `nexttimer`, `nexttimer-bg`, `launch.sh`.
- **Deploy via ADB:** TrimUI Brick per USB verbunden — ADB in NextUI standardmäßig aktiv:
  ```sh
  adb push "build/tg5040/Next Timer.pak/nexttimer" "/mnt/SDCARD/Tools/tg5040/Next Timer.pak/nexttimer"
  adb push "build/tg5040/Next Timer.pak/nexttimer-bg" "/mnt/SDCARD/Tools/tg5040/Next Timer.pak/nexttimer-bg"
  ```
  Vorher `adb devices` zum Verbindungstest (`android-tools` Paket nötig).

### Screen On/Off

**Do NOT use** `DISP_LCD_BACKLIGHT_ENABLE/DISABLE` (0x104/0x105) — they corrupt the
backlight driver state and invert the hardware brightness keys.

Use `DISP_LCD_SET_BRIGHTNESS` (0x102) with raw value 0–255:

```c
#define DISP_LCD_SET_BRIGHTNESS 0x102

// Screen off — set raw brightness to 0:
int fd = open("/dev/disp", O_RDWR);
unsigned long param[4] = {0, 0, 0, 0};
ioctl(fd, DISP_LCD_SET_BRIGHTNESS, &param);
close(fd);

// Screen on — kick to 8 first, then restore saved level:
unsigned long p1[4] = {0, 8,   0, 0};  ioctl(fd, DISP_LCD_SET_BRIGHTNESS, &p1);
unsigned long p2[4] = {0, raw, 0, 0};  ioctl(fd, DISP_LCD_SET_BRIGHTNESS, &p2);
```

Saved brightness (0–10) is read from NextUI's POSIX shared memory `/SharedSettings`.
Map level→raw with Brick table `{1,8,16,32,48,72,96,128,160,192,255}`.

**Implementation:** `src/screen.c` — Power-button thread reads `/dev/input/event*` for `KEY_POWER`; short press toggles screen. Apostrophe's built-in power handler disabled with `ap_set_power_handler(false)`.

**Idle timeout** is handled by NextUI itself — no auto-off logic in the app.

**Suspend-Lock:** While timer or stopwatch runs, `screen_set_suspend_lock(true)` writes `nexttimer` to `/sys/power/wake_lock` to prevent device suspend. Released on pause/stop.

**Two screen states:**
- **ON** — normal, backlight at user level
- **OFF** — `set_raw_brightness(0)`, render solid black frame

### Background Daemon

When NextUI closes the app while the timer is running (SIGTERM), `main.c`:
1. Writes `$PAK_DIR/nexttimer.state` with remaining_ms, total_ms, set_minutes, set_seconds, exit_time
2. Forks and execs `nexttimer-bg <remaining_seconds> <settings_path> <state_path>`
3. The daemon sleeps, then plays the alert (3× beep + vibration), then removes the state file

On next app start:
- If state file exists and daemon PID alive: kill daemon, restore timer with corrected remaining time
- If state file exists but daemon dead: show EXPIRED state (alert already played)

---

## Features

- Countdown timer: set minutes (±1 via Up/Down) and seconds (±10 via Left/Right)
- Start / Pause / Resume / Reset
- Audio alert when timer expires (configurable volume); repeats until dismissed
- Vibration alert on TrimUI Brick (configurable intensity)
- Timer continues running after app is closed (background daemon plays alert)
- Stopwatch screen: count up with tenths-of-second display
- L1/R1 switches between Timer and Stopwatch with swipe animation
- Suspend-lock: prevents device sleep while timer/stopwatch is running
- Settings: sound on/off, volume, vibration on/off, intensity, visual alarm

---

## UI

### Button Mapping — Timer Screen

| Button | Action |
|---|---|
| A | Start / Pause / Resume |
| X | Reset timer |
| B | Exit app (timer running → daemon keeps it alive in background) |
| D-Pad Up/Down | ±1 minute |
| D-Pad Left/Right | ±10 seconds |
| Menu | Settings |
| L1 / R1 | Switch to Stopwatch screen |
| Power (short) | Toggle screen on/off (timer continues) |

### Button Mapping — Stopwatch Screen

| Button | Action |
|---|---|
| A | Start / Pause |
| X | Reset |
| B | Exit app |
| Menu | Settings |
| L1 / R1 | Switch to Timer screen |
| Power (short) | Toggle screen on/off |

---

## Project Structure

```
nexttimer/
├── Makefile              # Linux + Docker cross-compile targets
├── ports/tg5040/Makefile # TG5040 cross-compile (used by Docker)
├── launch.sh             # NextUI pak entry point
└── src/
    ├── main.c            # Entry, timer+stopwatch screens, swipe animation, render loop
    ├── timer.c/h         # Countdown timer state machine
    ├── stopwatch.c/h     # Stopwatch state machine (count-up)
    ├── settings.c/h      # Persistent settings (sound/vibration/visual alarm)
    ├── screen.c/h        # Backlight/sleep — Power button toggle, suspend lock
    └── bg.c              # nexttimer-bg: background daemon for alert after app close
```
