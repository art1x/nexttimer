# Next Timer

A countdown timer and stopwatch pak for TrimUI Brick/Hammer running [NextUI](https://github.com/LoveRetro/NextUI).

## Features

- Countdown timer adjustable in 1-minute and 10-second steps
- Stopwatch with tenths-of-second display
- Audio alert (3 × beep pattern) with adjustable volume
- Vibration alert with adjustable intensity
- Visual alarm (red background when timer expires)
- Timer continues running after app is closed — background daemon plays alert when it expires
- Power button toggles screen on/off while timer keeps running
- All settings persist between sessions

## Controls

### Timer Screen

| Button | Action |
|--------|--------|
| ↑ / ↓ | ±1 minute |
| ← / → | ±10 seconds |
| A | Start / Pause / Resume |
| X | Reset |
| B | Exit (timer keeps running in background) |
| Menu | Settings |
| L1 / R1 | Switch to Stopwatch |
| Power | Toggle screen on/off |

### Stopwatch Screen

| Button | Action |
|--------|--------|
| A | Start / Pause |
| X | Reset |
| B | Exit |
| Menu | Settings |
| L1 / R1 | Switch to Timer |
| Power | Toggle screen on/off |

## Settings

| Setting | Description |
|---------|-------------|
| Sound | On / Off |
| Volume | 1–10 |
| Vibration | On / Off |
| Vib. Intensity | 1–10 |
| Visual Alarm | Red background when timer expires (On / Off) |

## Installation

Download `Next Timer.zip` from the [latest release](../../releases/latest), extract it and copy `Next Timer.pak` into the `Tools` folder on your SD card.

## Credits

Built with [Apostrophe](https://github.com/Helaas/apostrophe) — a C UI toolkit for NextUI paks by Kevin Vranken (MIT License).

This project was developed primarily with the help of [Claude Code](https://claude.ai/code) by Anthropic.

## License

MIT — see [LICENSE](LICENSE)
