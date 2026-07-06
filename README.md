
# NES BOX — ESP32-S3 掌上 NES 游戏机

基于 Waveshare ESP32-S3-Touch-AMOLED-2.41 开发板的 NES 模拟器掌机，支持 600×450 AMOLED 全屏显示、LVGL 游戏启动器 UI、I2S 音频输出、ESP-NOW 双人联机、**TCA9554 震动马达反馈**。

<img src="./3D/Designer.png" width=500>

## BOM（物料清单）

| 物料 | 型号/规格 | 数量 | 用途 |
|------|----------|------|------|
| 主控板 | Waveshare ESP32-S3-Touch-AMOLED-2.41 | 1 | 主控 + 屏幕 + 触摸 + PMU |
| I2C GPIO 扩展 | TCA9554 模块 | 1 | 扩展 GPIO（驱动马达） |
| 震动马达 | 1027 扁平马达 / 1020 纽扣马达 | 1 | 游戏震动反馈 |
| NPN 三极管 | S8050 / 2N2222 | 1 | 驱动马达（GPIO 无法直驱） |
| 续流二极管 | 1N4148 / 1N4007 | 1 | 保护电路（反接马达两端） |
| 电阻 | 1kΩ（基极限流） | 1 | 三极管基极电阻 |
| 电阻 | 10kΩ（下拉） | 1 | 确保关断时基极为低 |
| SD 卡 | MicroSD，FAT32，≥1GB | 1 | 存储游戏 ROM 和音效 |
| 扬声器 | 8Ω 1-3W | 1 | I2S 功放输出 |
| 物理按键 | 6×6mm 轻触开关 | 9 | UP/DOWN/LEFT/RIGHT/A/B/SELECT/START/QUIT |
| I2S 功放 | MAX98357A 模块 | 1 | 音频放大 |
| 锂电池 | 3.7V，≥400mAh | 1 | 供电 |
| 杜邦线 / 排针 | — | 若干 | 连接外围模块 |

### 马达驱动电路

```
ESP32/TCA9554 GPIO ──[1kΩ]──┬─── B (S8050)
                             │
GND ──────────────────────── E
                             │
                     ┌─ 马达 ─┐
                     │        │
                     │   ┌─── C
                     │   │
           VCC ──────┴───┤◁ 1N4148 (续流)
                         └─── GND
```

## 硬件

- **主控**：ESP32-S3（240MHz，8MB Flash，PSRAM）
- **屏幕**：2.41" AMOLED，600×450 触摸屏
- **音频**：MAX98357A I2S 功放
- **存储**：MicroSD 卡（FAT32，SPI 40MHz）
- **电源**：SY6970 PMU + 锂电池
- **震动**：TCA9554 I2C GPIO 扩展 + 马达驱动电路

## 功能

### 游戏
- 34 个 Mapper 的 NES 模拟（arduino-nofrendo 核心）
- 全屏缩放（256×240 → 600×450，最近邻算法）
- 完整 APU 音频（I2S PCM 输出，22050Hz 16-bit）
- 物理按键 + 触摸屏虚拟手柄（8 键 + 退出键）
- 游戏内暂停菜单（下滑手势，调节亮度和音量）
- 电池电量显示（菜单栏 + 游戏内覆盖）
- **震动反馈：玩家受伤/掉血时马达振动 180ms，连击延长**
- **30+ 款游戏预设健康值地址，自动匹配 ROM 文件名**
- **HP 扫描调试工具（`-DHP_SCAN_ENABLE`），支持自定义新游戏，支持自动扫描HP的RAM地址**
- 长按电源键开机
- 按reset键关机

### UI
- LVGL 游戏选择器（卡片轮播 / 列表双模式）
- 启动画面（实时扫描游戏名显示）
- 游戏列表缓存（`.cache` 文件，再次启动秒加载）
- 导航音效（`/game/music/select.pcm` + `start.pcm`）

### 设置
- 屏幕亮度调节（5-255）
- 音量调节（0-21，同时控制 UI 音效和游戏音频）
- 屏幕旋转 180°
- 在线联机开关
- 清除游戏缓存（重新扫描游戏）
- 设置持久化（`/game/settings.cfg`）
- 游戏内设置，下滑屏幕弹出设置框

### HP RAM 地址扫描
- plateformio.ini 打开-DHP_SCAN_ENABLE 进入游戏正常玩后会自动采集玩家受伤害和死亡的数据进行HP地址跟踪
- 最终写入 (`/game/game_profile.cfg`)

### 联机
- ESP-NOW 无线通信
- 自动 HOST/CLIENT 协商
- 6 帧输入延迟缓冲
- CRC 失步检测
- 断线自动退出

## 构建

```bash
pio run              # 编译
pio run -t upload    # 烧录
pio device monitor -b 115200  # 串口监视
```

### 调试选项

```ini
; platformio.ini build_flags 中添加：
-DHP_SCAN_ENABLE     # 启用 HP 地址扫描器（串口输出候选地址）
```

## 目录结构

```
AMOLedNES/
├── src/
│   ├── mian.cpp              # 主入口，setup/loop，LVGL，按键，设置
│   ├── game_ui.cpp           # 游戏选择 UI（卡片/列表）
│   ├── nes_launcher.cpp      # NES 模拟器 FreeRTOS 任务封装
│   ├── nofrendo_osd.cpp      # OSD 层：视频驱动、音频、输入、暂停菜单
│   ├── audio_player.cpp      # MP3 音效 + PCM 播放封装
│   ├── touch_gamepad.cpp     # 触摸屏虚拟手柄
│   ├── damage_detector.cpp   # 伤害检测（NES RAM 健康值监控）
│   ├── motor_controller.cpp  # 震动马达驱动（TCA9554 I2C）
│   └── hp_scan.c             # HP 地址扫描工具（调试用）
├── include/
│   ├── game_ui.h             #LVGL UI
│   ├── nes_launcher.h        # NES 启动
|   ├── game_profile_store.H  #HP RAM 扫描
│   ├── touch_gamepad.h
│   ├── damage_detector.h     # 伤害检测接口
│   ├── motor_controller.h    # 马达控制接口
│   ├── game_profiles.h       # 游戏健康值地址数据库（30+ 游戏）
│   ├── hp_scan.h             # HP 扫描接口
│   ├── lv_conf.h             # LVGL 配置
│   └── config.h              # 引脚定义
├── lib/
│   ├── AMOLED/               # Ws_AMOLED 显示驱动
│   ├── arduino-nofrendo/     # NES 模拟器核心（34 mapper）
│   ├── TCA9554EXIO/          # TCA9554 I2C GPIO 扩展驱动
│   ├── XPowersLib/           # PMU 驱动（SY6970）
│   ├── SensorLib/            # 传感器驱动
│   └── MultiplePlayer/       # ESP-NOW 联机
├── platformio.ini
├── CLAUDE.md
└── README.md
```

## 引脚

| 功能 | GPIO | 说明 |
|------|------|------|
| SD MISO | 6 | |
| SD MOSI | 5 | |
| SD CLK | 4 | |
| SD CS | 2 | |
| BTN UP | 1 | |
| BTN DOWN | 7 | |
| BTN LEFT | 8 | |
| BTN RIGHT | 18 | |
| BTN A | 38 | |
| BTN B | 39 | |
| BTN SELECT | 40 | |
| BTN START | 41 | |
| BTN QUIT | 43 | 游戏中退回到菜单 |
| I2S BCLK | 45 | MAX98357A |
| I2S LRC | 42 | MAX98357A |
| I2S DOUT | 46 | MAX98357A |
| I2C SDA | 47 | TCA9554 + PMU / Touch |
| I2C SCL | 48 | TCA9554 + PMU / Touch |
| MOTOR (TCA9554 P7) | EXIO bit 7 | 震动马达（通过 TCA9554 I2C 扩展） |
| BAT Control | 16 | 电池供电保持 |
| BAT ADC | 17 | 电池电压检测 |
| Key BAT | 15 | 电源按键 |

## 震动系统架构

```
Core 1（模拟器，~60fps）:
  scaleAndPush()
    ├── damageDetector_update()
    │     ├── 首帧：匹配 ROM 文件名 → game_profiles.h或SD：/game/game_profile.cfg → 确定健康值地址
    │     ├── 每帧：读取 nes_getcontextptr()->cpu->mem_page[0][addr]
    │     └── 值下降 → g_damageDetected = true
    └── motorController_update()
          ├── g_damageDetected → exio_set_bit(7) → 马达 ON
          ├── 帧计数 11 帧（≈180ms）→ exio_clear_bit(7) → 马达 OFF
          └── 连击：帧计数器重置（延长振动）

I2C: TCA9554 独占 I2C_NUM_0 (GPIO 47/48)，与触摸 I2C 同核串行，无竞争
```

### 支持的游戏（game_profiles.h）

| 类型 | 游戏 |
|------|------|
| 平台跳跃 | Super Mario, Mega Man 1-6, Castlevania, Ninja Gaiden, Duck Tales, Battletoads, Ghosts 'n Goblins, Double Dragon III, Bubble Bobble, Batman, Chip 'n Dale 1-2, Adventure Island 1-3, Little Nemo |
| 动作冒险 | Zelda II, Metroid, Kirby, TMNT III, Punch-Out, Shadow of the Ninja, Kid Icarus, Ice Climber, Toki |
| 射击 | Contra, Gradius, Life Force, Star Wars |
| RPG | Final Fantasy |

未匹配的游戏不触发震动。使用 `-DHP_SCAN_ENABLE` 扫描新游戏地址，添加到 `game_profiles.h` 即可支持。

## SD 卡文件结构

```
/game/
├── settings.cfg               # 设置文件（亮度、音量、联机、旋转）
├── nes/
│   ├── game1.nes
│   ├── game2.nes
│   └── .cache                 # 游戏列表缓存
├── music/
|   ├── power_up.mp3           # 开机音效
│   ├── select.mp3             # 导航音效（源文件）
│   ├── select.pcm             # 导航音效（预加载 PCM）
│   ├── start.mp3              # 启动音效（源文件）
│   └── start.pcm              # 启动音效（预加载 PCM）
|── img/
│   ├── game1.png
│   ├── game2.png
└── nofrendo.cfg               # 模拟器配置
```

## 音效预处理

```bash
# 把 MP3 转换为原始 PCM（零延迟播放）
ffmpeg -i select.mp3 -f s16le -acodec pcm_s16le -ac 1 -ar 22050 select.pcm
ffmpeg -i start.mp3  -f s16le -acodec pcm_s16le -ac 1 -ar 22050 start.pcm
```

## 依赖库

| 库 | 用途 |
|----|------|
| lvgl @ 8.4.0 | UI 框架 |
| esphome/ESP32-audioI2S @ ^2.3.0 | MP3 解码播放 |
| arduino-nofrendo | NES 模拟器核心 |
| TCA9554EXIO | TCA9554 I2C GPIO 扩展驱动 |
| Ws_AMOLED | AMOLED 显示驱动 |
| XPowersLib | PMU 电源管理 |
| SensorLib | 传感器驱动 |
| Arduino SD / SPI / FS | SD 卡文件系统 |

## 项目架构

```
Core 0 (UI):      LVGL 菜单 + 输入处理 + 音效播放
Core 1 (模拟器):   nofrendo 阻塞循环 → 视频缩放输出 + APU 音频
                   + 伤害检测 + 马达控制（同核 I2C）

视频:   nofrendo PPU → 256×240 调色板索引 → RGB565 → 600×450 缩放 → pushColors()
音频:   nofrendo APU → 单声道 PCM → 立体声复制 → i2s_write() → MAX98357A
输入:   osd_getinput() 在 Core 1 直接 digitalRead() GPIO → event 系统
震动:   damageDetector → nes_getcontextptr() RAM 监控 → TCA9554 I2C → 马达驱动
联机:   ESP-NOW → netplay_sync_frame() → 双人输入合并
```

## 贡献者

 感谢军火提供友人：<a href="https://github.com/Fineman007">Fineman007</a>