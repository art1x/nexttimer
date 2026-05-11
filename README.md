# Next Timer

A simple countdown timer pak for TrimUI Brick running [NextUI](https://github.com/LoveRetro/NextUI).

## Features

- Countdown timer adjustable in 1-minute and 10-second steps
- Audio alert (3 × beep pattern) with adjustable volume
- Vibration alert with adjustable intensity
- Visual alert (red background when timer expires)
- Screen timeout with software dimming (Select + A to wake)
- All settings persist between sessions

## Controls

| Button | Action |
|--------|--------|
| ↑ / ↓ | ±1 minute |
| ← / → | ±10 seconds |
| A | Start / Pause / Resume |
| B | Reset / Exit (when idle) |
| Menu | Settings |

## Settings

| Setting | Description |
|---------|-------------|
| Ton | Sound on/off |
| Lautstärke | Volume 1–10 |
| Vibration | Vibration on/off |
| Vibr.-Stärke | Vibration intensity 1–10 |
| Bildschirm-Timeout | Screen off after: never / 10s / 30s / 1min / 2min / 5min |
| Visueller Alarm | Red background when timer expires |

## Installation

Copy `Next Timer.pak` into the `Tools` folder on your SD card.

## Building

Requires Docker and the [tg5040-toolchain](https://github.com/LoveRetro/NextUI).

```
make tg5040
```

## Credits

Built with [Apostrophe](https://github.com/Helaas/apostrophe) — a C UI toolkit for NextUI paks by Kevin Vranken (MIT License).

## License

MIT — see [LICENSE](LICENSE)
