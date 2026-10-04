# Snake 2 Watchface

A Nokia Snake 2-inspired watchface for Pebble, built primarily for **Pebble Time 2 (Emery)** (200 × 228 colour), also compatible with Pebble Time (Basalt, 144 × 168).

## Layout

```
┌─ INFO BAR ──────────────────────────────────────────────────┐
│  [☀ 22°C]    [30.06.25]                           [85%]    │
├─────────────────────────────────────────────────────────────┤
│  🐍 Snake animates in the background across the full screen │
│                                                             │
│             ┌──────────────────────┐                        │
│             │       14:30          │  ← LECO pixel font     │
│             └──────────────────────┘                        │
│                                                             │
│  🍎 🍒 💧 🍄  Food items in valid zones (not on clock/UI)  │
├─────────────────────────────────────────────────────────────┤
│  Mo  Di  [Mi]  Do  Fr  Sa  So   ← today highlighted        │
└─────────────────────────────────────────────────────────────┘
```

## Features

- **Nokia Snake 2 style snake** – scale-textured body, directional head with slit pupils and forked tongue, tapering tail
- **8 pixel-art food items** – red/green/yellow apples, blue water drop, cherry, mushroom, star, gem
- **BFS pathfinding** – snake intelligently navigates toward food with 30 % random variation for organic movement
- **Minute restart** – snake respawns above the current weekday at each new minute
- **Weather display** – current conditions icon + temperature (OpenWeatherMap or wttr.in)
- **Battery indicator** – percentage in top-right corner
- **5 languages** – weekday names in German, English, French, Spanish, Italian
- **4 colour schemes** – Classic, Matrix, Ocean, Sunset
- **4 snake skins** – Nokia Classic (green), Viper (dark emerald), Python (golden brown), Ice (teal)
- **3 food item sets** – Fruits, Nature, Mixed
- **3 animation speeds** – Slow, Normal, Fast
- **12/24 h clock toggle**

## Building

```bash
# Install dependencies (Clay settings framework)
npm install

# Build with Pebble SDK 4.x
pebble build

# Install to watch / emulator
pebble install --emulator emery
pebble install --phone <IP>
```

## Configuration

Open the watchface settings in the Pebble smartphone app to configure:

- **Color Scheme** – overall UI colours
- **Language** – weekday language
- **Snake Skin** – snake body colours
- **Snake Speed** – animation speed
- **Food Item Set** – which pixel-art items appear
- **Weather Provider** – OpenWeatherMap (free API key required) or wttr.in (no key)
- **OpenWeatherMap API Key** – get a free key at [openweathermap.org](https://openweathermap.org/api)
- **Celsius / Fahrenheit** – temperature unit
- **24 h / 12 h** – clock format
- **Show Weather** – toggle weather display

## Project Structure

```
src/
  c/
    Snake2.c       Main watchface (C, ~1200 lines)
  pkjs/
    index.js       Phone-side JS: weather + settings
    config.js      Clay settings UI definition
package.json       Project metadata & message keys
wscript            Build configuration
```

A Pebble watchapp/watchface written in C using the Pebble SDK.

## Building & running

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. The
modern Pebble hardware is **emery** (Pebble Time 2), **gabbro** (Pebble Round
2), and **flint** (Pebble 2 Duo); the original Pebble platforms (aplite,
basalt, chalk, diorite) are included by default for backwards compatibility.

## Project layout

```
src/c/           C source for the watchapp
src/pkjs/        PebbleKit JS (phone-side) source, if any
worker_src/c/    Background worker source, if any
resources/       Images, fonts, and other bundled resources
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

By default this project is configured as a watchapp. To make it a watchface,
set `pebble.watchapp.watchface` to `true` in `package.json`.

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
