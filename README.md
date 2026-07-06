# NES BOX — ESP32-S3 Handheld NES Game Console

[中文](README_CN.md) | [English](README_EN.md)

<img src="./3D/Designer.png" width=500>

A NES emulator handheld based on the **Waveshare ESP32-S3-Touch-AMOLED-2.41** dev board.

### Highlights

- 🕹️ 34-mapper NES emulation (arduino-nofrendo core), 600×450 full-screen scaling
- 🖥️ LVGL game launcher UI with card carousel and list view
- 🔊 Full APU audio via I2S (MAX98357A, 22050Hz 16-bit)
- 📳 **Vibration feedback** on player damage (TCA9554 I2C motor driver)
- 🩸 Auto HP address detection for 30+ games, SD-based learning for new games
- 🎮 Physical buttons + touch screen virtual gamepad
- 🌐 ESP-NOW 2-player netplay
- ⚙️ In-game pause menu, brightness/volume controls, persistent settings
- 🔋 Battery level display

### Quick Start

```bash
pio run -t upload
```

Place `.nes` ROMs on a FAT32 MicroSD card under `/game/nes/`.

### License

[MIT](LICENSE) — free for everyone, forever.
