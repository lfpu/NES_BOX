# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 NES emulator with an LVGL-based game launcher UI, running on a Waveshare Touch-AMOLED-2.41 display (600×450). Games are loaded from a FAT32 SD card from `/game/nes/`. The emulator core is **arduino-nofrendo** (a mature NES emulator supporting 34 mappers with full APU audio).

## Build System

PlatformIO project targeting ESP32-S3 with Arduino framework.

| Command | Purpose |
|---------|---------|
| `pio run` | Build |
| `pio run -t upload` | Build & flash |
| `pio device monitor -b 115200` | Serial monitor |
| `pio run -t clean` | Clean build |

Single environment: `waveshare_s3_amoled` (in [platformio.ini](platformio.ini)). Board: `esp32-s3-devkitc-1`, CPU 240 MHz, QIO/OPI flash, `huge_app.csv` partitions. PSRAM required (`-DBOARD_HAS_PSRAM`).

## Architecture

### Source Files (`src/`)

| File | Role |
|------|------|
| [mian.cpp](src/mian.cpp) | Main entry point (`setup`/`loop`). Three-state FSM: APP_MENU → APP_LAUNCHING → APP_PLAYING. Initializes AMOLED display, SD card (VFS mount at `/sd`), LVGL, AudioPlayer, buttons. `audioPlayer.loop()` only runs in menu/launching states. |
| [nes_launcher.cpp](src/nes_launcher.cpp) | `NESLauncher` class — thin wrapper around arduino-nofrendo. `init()` sets up OSD globals, `loadAndRun()` creates FreeRTOS task on core 1, `stop()` signals quit via `g_quitRequested` flag, `setButtonState()` syncs to `g_padState`. |
| [nofrendo_osd.cpp](src/nofrendo_osd.cpp) | **OSD layer** — implements ALL callbacks required by arduino-nofrendo: memory allocation, video driver (palette→RGB565, nearest-neighbor scale 256→600, pushColors), input (direct GPIO read on Core 1), FreeRTOS timer at 60Hz, file I/O, config stubs, log chaining, and **NES audio**: APU→mono PCM→stereo duplicate→`i2s_write()` via Audio library's pre-installed I2S driver. |
| [audio_player.cpp](src/audio_player.cpp) | `AudioPlayer` class — thin wrapper around ESP32-audioI2S for MP3 sound effects. `init()`, `playSFX()`, `loop()`, `end()`. |
| [game_ui.cpp](src/game_ui.cpp) | `GameUI` class: LVGL game browser. Plays `/game/music/select.mp3` on navigate, `/game/music/start.mp3` on launch. |
| [touch_gamepad.cpp](src/touch_gamepad.cpp) | `TouchGamepad` class: on-screen virtual NES controller via LVGL buttons. |

### Headers (`include/`)

| File | Key definitions |
|------|----------------|
| [nes_launcher.h](include/nes_launcher.h) | `NESLauncher` class (public interface only), `NES_PAD_*` bitmasks, `NES_WIDTH`/`NES_HEIGHT` = 256×240. |
| [game_ui.h](include/game_ui.h) | `GameUI` class, `GameInfo` struct, `ViewMode`/`UIState` enums, color constants. |
| [touch_gamepad.h](include/touch_gamepad.h) | `TouchGamepad` class for on-screen controls. |
| [lv_conf.h](include/lv_conf.h) | LVGL config: 16-bit color with byte swap, max 600×450, custom memory via `malloc`, `LV_TICK_CUSTOM` using `millis()`. |

### Libraries (`lib/`)

| Library | Purpose |
|---------|---------|
| **AMOLED/** | `Ws_AMOLED` display driver (2.41" RM690B0 via `beginAMOLED_241()`). Provides `pushColors()`, `setAddrWindow()`, `getPoint()`, `setBrightness()`. |
| **arduino-nofrendo/** | **The NES emulator core.** Supports 34 mappers (0-5, 7-9, 11, 15-16, 18-19, 21-25, 32-34, 40, 64-66, 70, 75, 78-79, 85, 94, 99, 231). Full 6502 CPU, 2C02 PPU, APU audio. |
| **SensorLib/** | Touch/IMU/light/RTC drivers. |
| **XPowersLib/** | PMU drivers (AXP2101, SY6970). |

### Data Flow: Game Launch

```
User selects game → GameUI::_playLaunchAnimation() → _launchCb(romPath)
→ startNESGame(romPath)  [prepends "/sd" to path]
→ nesLauncher.init()     [sets OSD globals via nofrendo_osd_setup()]
→ nesLauncher.loadAndRun("/sd/game/nes/rom.nes")
→ FreeRTOS task on core 1 → _emuLoop()
→ nofrendo_main() → osd_main() → main_loop() → nes_emulate()  [BLOCKING]
→ each frame: custom_blit() converts palette→RGB565, scales 256×240→600×450, pushColors()
→ input: osd_getinput() reads g_padState, fires event_joypad1_* events
→ exit: g_quitRequested → event_quit → main_quit() → nes_emulate() returns
```

### OSD Layer Details

The OSD implementation in [nofrendo_osd.cpp](src/nofrendo_osd.cpp) provides:

- **Video driver**: `viddriver_t` with `custom_blit` that converts indexed bitmap→RGB565, nearest-neighbor scales to 600×450, and pushes to display
- **Input**: Polls `g_padState` (set by `NESLauncher::setButtonState`), fires nofrendo events using `INP_STATE_MAKE`/`INP_STATE_BREAK`
- **Timer**: FreeRTOS auto-reload timer at 60Hz, triggers `timer_isr` → `nofrendo_ticks++`
- **Memory**: `mem_alloc(prefer_fast_memory)` → DRAM for fast, PSRAM for large
- **File I/O**: `fopen`/`fread` via VFS (SD mounted at `/sd`)
- **Config**: Stub that returns success

### Key Behaviors

- **Physical buttons**: 9 GPIOs (UP=1, DOWN=7, LEFT=8, RIGHT=18, A=38, B=39, SELECT=40, START=41, QUIT=20), internal pull-ups, active LOW.
- **Touch fallback**: Screen divided into zones — left third = D-pad, right third = A/B, bottom strip = SELECT/START.
- **Game exit**: Press QUIT (GPIO 20) for 300ms, or hold SELECT+START for 2 seconds. Sets `g_quitRequested`, `osd_getinput()` fires `event_quit`, nofrendo exits cleanly. When returning to menu, the game list stays at the last selected game.
- **Emulation on core 1**: FreeRTOS task with 32KB stack, priority 5. Main loop on core 0 handles LVGL and input.
- **SD card**: Custom SPI pins at 40 MHz, VFS mounted at `/sd`. ROMs must be `.nes` files in `/game/nes/`. VFS mount enables `fopen("/sd/game/nes/rom.nes")` from nofrendo.
- **LVGL**: Single buffer in PSRAM, `full_refresh=1`. LVGL is deinitialized before emulation starts (to free PSRAM) and reinitialized on return to menu.

## Build Flags

See [platformio.ini](platformio.ini). Key flags:
- `-DBOARD_HAS_PSRAM` — PSRAM available (required)
- `-DLV_CONF_INCLUDE_SIMPLE` / `-DLV_LVGL_H_INCLUDE_SIMPLE` — LVGL include mode
- `-DARDUINO_USB_CDC_ON_BOOT=1` — serial output over USB
- `-I lib/arduino-nofrendo/src` / `-I lib/arduino-nofrendo/src/nes` / `-I lib/arduino-nofrendo/src/cpu` — nofrendo include paths
- To disable nofrendo debug output: add `-DNOFRENDO_DEBUG_DISABLE` to build_flags

## Important Notes

- The main source file is **`src/mian.cpp`** (misspelled — "mian" not "main"). PlatformIO finds it by scanning for `setup()`/`loop()`.
- PSRAM allocations use `ps_malloc()` for large buffers. Standard `malloc()` returns DRAM.
- The arduino-nofrendo library uses C stdio (`fopen`/`fread`) for ROM loading — requires VFS-mounted SD card.
- `lib/arduino-nofrendo/src/noftypes.h` has been modified: `NOFRENDO_DEBUG` is now conditional on `#ifndef NOFRENDO_DEBUG_DISABLE`.
- No test framework or test files exist.
