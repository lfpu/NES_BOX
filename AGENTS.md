# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Project Overview

ESP32-S3 NES emulator handheld with an LVGL-based game launcher UI, running on a Waveshare Touch-AMOLED-2.41 display (600×450). Games are loaded from a FAT32 SD card from `/game/nes/`. The emulator core is **arduino-nofrendo** (34 mappers, full APU audio).

## Build System

PlatformIO project targeting ESP32-S3 with Arduino framework.

| Command | Purpose |
|---------|---------|
| `pio run` | Build |
| `pio run -t upload` | Build & flash |
| `pio device monitor -b 115200` | Serial monitor |
| `pio run -t clean` | Clean build |

Single environment: `waveshare_s3_amoled`. Board: `esp32-s3-devkitc-1`, CPU 240 MHz, QIO/OPI flash, `huge_app.csv` partitions. PSRAM required.

### Build Flags (platformio.ini)

- `-DBOARD_HAS_PSRAM` — PSRAM available (required)
- `-DNOFRENDO_DEBUG_DISABLE` — suppresses nofrendo debug output (**remove** this flag to re-enable debug)
- `-DHP_SCAN_ENABLE` — enables the HP address auto-scanner (writes learned addresses to SD card). **When enabled, damage detection and motor vibration are disabled** — hp_scan is for discovering addresses; damage detection is for using them in gameplay. Remove this flag for normal gameplay with vibration feedback.
- `-DNES_EMULATOR_ENABLED` — enables nofrendo compilation
- `-DLV_CONF_INCLUDE_SIMPLE` / `-DLV_LVGL_H_INCLUDE_SIMPLE` — LVGL include mode
- `-DARDUINO_USB_CDC_ON_BOOT=1` — serial output over USB
- `-O2` — optimization level
- Nofrendo include paths: `-I lib/arduino-nofrendo/src`, `-I lib/arduino-nofrendo/src/nes`, `-I lib/arduino-nofrendo/src/cpu`

### Dependencies

| Library | Purpose |
|---------|---------|
| lvgl/lvgl @ 8.4.0 | UI framework |
| esphome/ESP32-audioI2S @ ^2.3.0 | MP3 decode & I2S playback |
| arduino-nofrendo (lib/) | NES emulator core (34 mappers) |
| AMOLED (lib/) | Waveshare 2.41" RM690B0 display driver |
| TCA9554EXIO (lib/) | TCA9554 I2C GPIO expander (motor control) |
| MultiplePlayer (lib/) | ESP-NOW netplay (2-player wireless) |
| XPowersLib (lib/) | PMU driver (SY6970/AXP2101) |
| SensorLib (lib/) | Touch/IMU/light/RTC drivers |

## Architecture

### Core Split

```
Core 0 (UI & control):   LVGL menu + button input + settings + audioPlayer.loop()
Core 1 (emulation):      nofrendo blocking loop → video scaling + APU audio
                          + damage detection + motor control (same core, no IPC needed)
```

### Source Files

| File | Role |
|------|------|
| [mian.cpp](src/mian.cpp) | Main entry point (`setup`/`loop`). Three-state FSM: APP_MENU → APP_LAUNCHING → APP_PLAYING. Initializes AMOLED, SD (VFS at `/sd`), LVGL, AudioPlayer, buttons, motor, netplay. Loads/saves settings from `/game/settings.cfg` (1-second debounce auto-save). Handles button input for both menu and game states. Touch fallback when no physical buttons detected. |
| [nes_launcher.cpp](src/nes_launcher.cpp) | `NESLauncher` class — thin wrapper around arduino-nofrendo. `init()` sets OSD globals, `loadAndRun()` extracts ROM base name, calls `damageDetector_setRom()`, creates FreeRTOS task on core 1 (32KB stack, priority 5). `stop()` sets `g_quitRequested`, waits up to 3s for graceful task exit. |
| [nofrendo_osd.cpp](src/nofrendo_osd.cpp) | **OSD layer** — implements ALL callbacks required by arduino-nofrendo: video driver (palette→RGB565, nearest-neighbor scale 256×240→600×450, `pushColors()`), NES audio (APU→mono PCM→stereo duplicate→`i2s_write()` at 22050Hz via 120Hz esp_timer, independent of video rate), input (Core 1 direct GPIO read via `g_padState`), FreeRTOS timer at 60Hz, file I/O via VFS, config stubs. **Per-frame in `scaleAndPush()`**: calls `damageDetector_update()`, `motorController_update()`, and `hp_scan_update()`. |
| [game_ui.cpp](src/game_ui.cpp) | `GameUI` class: LVGL game browser with carousel and list view modes. Settings overlay (brightness, volume, rotation, netplay toggle, cache clear). Search overlay with keyboard. Plays preloaded PCM sound effects (`select.pcm`/`start.pcm`) on navigate/launch via `audioPlayer.playPCM()`. |
| [audio_player.cpp](src/audio_player.cpp) | `AudioPlayer` class — wraps ESP32-audioI2S for MP3 playback. Also supports **PCM preloading**: loads `.pcm` files into PSRAM at boot for zero-latency SFX playback via a dedicated FreeRTOS task. Must be reinitialized after game exit (`reinit()`) since I2S is taken over by NES during gameplay. |
| [damage_detector.cpp](src/damage_detector.cpp) | **Damage detection** — runs on Core 1 each frame (inside `scaleAndPush()`). Matches ROM filename against SD-based profile store first, then built-in `gameProfiles[]` table. Monitors the game-specific NES RAM health address. When value drops by 1–8 (not a death/respawn spike), sets `g_damageDetected = true`. **Disabled when `HP_SCAN_ENABLE` is defined** — returns immediately. |
| [motor_controller.cpp](src/motor_controller.cpp) | **Vibration motor driver** — runs on Core 1 each frame. On `g_damageDetected`, turns on TCA9554 bit 7 for 11 frames (~180ms). Rapid hits reset the frame counter (motor stays on continuously). Uses I2C via TCA9554EXIO library on I2C_NUM_0 (shared with touch/PMU, no contention on Core 1). |
| [hp_scan.c](src/hp_scan.c) | **HP address auto-scanner** — only active when `-DHP_SCAN_ENABLE` is set. Scans NES RAM $0000-$07FF each frame, scores each byte for health-like behavior (stable values, small decreases, death→zero correlation). Auto-commits learned addresses to SD via `gps_commit()`. Once committed, stops scanning for that ROM. |
| [game_profile_store.cpp](src/game_profile_store.cpp) | **SD-based HP profile store** — persists auto-learned health addresses to `/game/game_profile.cfg`. Format: `ROM_SUBSTR\|HP_ADDR_HEX\|HITS\|SCORE\|DEATH_HITS`. Cache of up to 128 entries. `gps_find()` matches by ROM filename substring. Higher scores override existing entries. Atomic writes via temp-file-then-copy (Arduino SD has no `rename`). |
| [touch_gamepad.cpp](src/touch_gamepad.cpp) | `TouchGamepad` class: on-screen virtual NES controller via LVGL buttons (used when no physical buttons detected). |

### Headers

| Header | Defines |
|--------|---------|
| [config.h](include/config.h) | All pin definitions: SD SPI, buttons (GPIO 1/7/8/18/38/39/40/41/43), I2S audio (BCLK=45, LRC=42, DOUT=46), I2C (47/48), battery ADC (17), motor (EXIO bit 7). Screen dimensions (600×450). |
| [nes_launcher.h](include/nes_launcher.h) | `NESLauncher` class, `NES_PAD_*` bitmasks, `NES_WIDTH`/`NES_HEIGHT` = 256×240. |
| [game_ui.h](include/game_ui.h) | `GameUI` class, `GameInfo` struct, `ViewMode`/`UIState` enums, color constants. |
| [audio_player.h](include/audio_player.h) | `AudioPlayer` class with PCM preload/playback and MP3 SFX. |
| [damage_detector.h](include/damage_detector.h) | `g_damageDetected` flag (inter-core volatile), `damageDetector_setRom()`, `damageDetector_update()`, `damageDetector_reset()`. |
| [motor_controller.h](include/motor_controller.h) | `motorController_init()`, `motorController_update()`, `MOTO_PULSE_FRAMES` = 11 (~180ms). |
| [game_profiles.h](include/game_profiles.h) | Built-in `gameProfiles[]` table (30+ games), `findGameProfile()` — checks SD store first, then falls back to built-in table. |
| [game_profile_store.h](include/game_profile_store.h) | `GpsEntry` struct, `gps_load()`, `gps_find()`, `gps_commit()`, `gps_dump()`. Path: `/game/game_profile.cfg`. |
| [hp_scan.h](include/hp_scan.h) | `hp_scan_update()`, `hp_scan_set_rom()`, `hp_scan_get_learned_addr()`. |
| [touch_gamepad.h](include/touch_gamepad.h) | `TouchGamepad` class for on-screen controls. |
| [lv_conf.h](include/lv_conf.h) | LVGL config: 16-bit color with byte swap, max 600×450, custom memory via `malloc`, `LV_TICK_CUSTOM` using `millis()`. |

### Data Flow: Game Launch

```
User selects game → GameUI::_playLaunchAnimation() → _launchCb(romPath)
→ appState = APP_LAUNCHING → after 500ms delay + animation flush → startNESGame(romPath)
→ [optional: netplay handshake with 30s timeout, cancelable via QUIT button]
→ deinitLVGL() [frees PSRAM framebuffer]
→ nesLauncher.init() [sets OSD globals via nofrendo_osd_setup()]
→ damageDetector_setRom(base) [sets ROM name, loads SD profile store]
→ nesLauncher.loadAndRun("/sd/game/nes/rom.nes")
→ FreeRTOS task on core 1 → _emuLoop()
→ nofrendo_main() → osd_main() → main_loop() → nes_emulate()  [BLOCKING]
→ each frame in custom_blit/scaleAndPush():
    → video: palette→RGB565, nearest-neighbor scale 256×240→600×450, pushColors()
    → damageDetector_update() [reads NES RAM, detects HP drops, sets g_damageDetected]
    → motorController_update() [checks g_damageDetected, drives TCA9554 motor]
    → hp_scan_update() [if HP_SCAN_ENABLE; scans RAM for health address candidates]
→ input: osd_getinput() reads g_padState, fires event_joypad1_* events
→ exit: g_quitRequested → event_quit → main_quit() → nes_emulate() returns
→ nofrendo_osd_cleanup() → returnToMenu() → reinit LVGL + AudioPlayer → gameUI.show()
```

### Damage Detection & Motor Feedback Pipeline

```
Core 1, inside scaleAndPush() (~60fps):
  damageDetector_update()
    → matches ROM filename → findGameProfile() [SD store first, then built-in table]
    → reads nes_getcontextptr()->cpu->mem_page[0][healthAddr]
    → if value drops 1–8 (small enough to be damage, not death): g_damageDetected = true
    → ignores drops >8 (death/respawn — false positive)
  motorController_update()
    → if g_damageDetected: TCA9554 bit 7 = HIGH → transistor → motor ON
    → 11-frame (~180ms) countdown; resets on rapid hits (motor stays on for combos)
    → after countdown expires: TCA9554 bit 7 = LOW → motor OFF

I2C: TCA9554 shares I2C_NUM_0 (GPIO 47/48) with touch/PMU
→ serialized on Core 1, no contention with Core 0 LVGL
```

### HP Auto-Scan System (when -DHP_SCAN_ENABLE)

```
hp_scan_update() runs each frame on Core 1:
  1. Warmup (60 frames): capture baseline values
  2. Each frame: scan NES RAM $0000-$07FF
     - +15 score for legal HP decrease (1–8)
     - +1 score for stable value (capped at +200)
     - −30 score for bad jump (>8 decrease)
     - −50 + blacklist for out-of-range (>99)
     - +50 death bonus: byte that was >0 in rollback window → 0 now
  3. Death detection: ≥100 bytes transition non-zero→zero in one frame
     → Rollback 30-frame ring buffer, reward candidates that hit zero
  4. Every 300 frames: rank top 5 candidates
  5. Auto-commit when: hits≥5, deathHits≥1, top score ≥ 2× runner-up
     → gps_commit() writes to /game/game_profile.cfg
  6. Once committed: hp_scan stops scanning (s_committed = true)
```

### Settings System

Loaded from `/game/settings.cfg` at boot, saved with 1-second debounce after last slider change:

| Key | Range | Default |
|-----|-------|---------|
| `brightness` | 25–255 | 126 |
| `volume` | 0–21 | 12 |
| `online` | 0/1 | 0 |
| `rotate` | 0/1 (0° or 180°) | 0 |

`g_nesVolume` (extern in nofrendo_osd.cpp) is synced from the settings volume — controls NES PCM output level independently of UI audio player volume.

### Netplay (ESP-NOW, currently disabled)

- Auto HOST/CLIENT negotiation with 30s timeout
- 6-frame input delay buffer with CRC desync detection
- Disconnection triggers auto-exit to menu
- Currently gated behind `g_netplayWanted = false` in mian.cpp
- Toggle via settings panel → persisted to `settings.cfg`
- Handshake runs on a separate FreeRTOS task; cancelable via QUIT button

## Key Pin Assignments (from config.h)

| Function | GPIO | Notes |
|----------|------|-------|
| BTN UP/DOWN/LEFT/RIGHT | 1/7/8/18 | Internal pull-up, active LOW |
| BTN A/B | 38/39 | Internal pull-up, active LOW |
| BTN SELECT/START | 40/41 | Internal pull-up, active LOW |
| BTN QUIT | 43 | Internal pull-up, active LOW; 300ms hold to exit game |
| SD CS/CLK/MOSI/MISO | 2/4/5/6 | 40 MHz SPI |
| I2S BCLK/LRC/DOUT | 45/42/46 | MAX98357A |
| I2C SDA/SCL | 47/48 | TCA9554 + touch + PMU |
| MOTOR | EXIO bit 7 | TCA9554 P7 → transistor → vibration motor |
| BAT Control/ADC/Key | 16/17/15 | Power hold / voltage sense / power button |

## SD Card Filesystem Layout

```
/game/
├── settings.cfg               # brightness, volume, online, rotate
├── game_profile.cfg           # auto-learned HP addresses (ROM_SUBSTR|ADDR|HITS|SCORE|DEATH_HITS)
├── nes/
│   ├── *.nes                  # NES ROM files
│   └── .cache                 # game list cache (for fast startup)
├── music/
│   ├── power_up.mp3           # boot sound (MP3)
│   ├── select.pcm / start.pcm # navigation/launch SFX (preloaded raw PCM, zero-latency)
│   └── select.mp3 / start.mp3 # source files (not used at runtime)
├── img/
│   └── *.png                  # game cover art
└── nofrendo.cfg               # emulator config (stub file)
```

Convert MP3 to PCM for zero-latency SFX playback:
```bash
ffmpeg -i select.mp3 -f s16le -acodec pcm_s16le -ac 1 -ar 22050 select.pcm
```

## Touch Fallback

When no physical buttons detected: left third = D-pad (8-directional, 25px dead zone), right third = A (top half) / B (bottom half), bottom 60px strip = SELECT (left) / START (right). Touch coordinates rotate 180° when `g_rotation180` is set.

## Game Exit Methods

1. **QUIT button** (GPIO 43): hold 300ms — detected by both Core 0 (`handleGameButtons`) and Core 1 (`osd_getinput`)
2. **SELECT + START combo**: hold 2 seconds — Core 0 only

## Important Notes

- The main source file is **`src/mian.cpp`** (misspelled — "mian" not "main"). PlatformIO finds it by scanning for `setup()`/`loop()`.
- PSRAM allocations use `ps_malloc()` for large buffers. Standard `malloc()` returns DRAM.
- The arduino-nofrendo library uses C stdio (`fopen`/`fread`) for ROM loading — requires VFS-mounted SD card at `/sd`.
- `lib/arduino-nofrendo/src/noftypes.h` has been modified: `NOFRENDO_DEBUG` is conditional on `#ifndef NOFRENDO_DEBUG_DISABLE`.
- `arduino-nofrendo` is a git submodule — if issues arise, check `.gitmodules`.
- When `HP_SCAN_ENABLE` is defined, damage detection and motor vibration are **disabled** (mutually exclusive modes). For gameplay with vibration, remove `-DHP_SCAN_ENABLE` from build_flags.
- LVGL is fully deinitialized (framebuffer freed) before emulation starts, and reinitialized on return to menu — frees PSRAM for the emulator.
- `audioPlayer.loop()` only runs in `APP_MENU` and `APP_LAUNCHING`. During gameplay (`APP_PLAYING`), I2S is exclusively used by nofrendo's APU audio output at 22050Hz via a 120Hz esp_timer (not tied to video frame rate, reducing stutter).
- No test framework or test files exist.
- The `game_profile_store` uses a temp-file-then-copy pattern for atomic writes (Arduino SD library lacks `rename`).
- Game list is cached to `/game/nes/.cache` — use "Clear Cache" in settings to force rescan.
