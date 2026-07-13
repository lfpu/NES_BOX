#include <Arduino.h>
#include "Ws_AMOLED.h"
#include <lvgl.h>
#include <SD.h>
#include <SPI.h>
#include "game_ui.h"
#include "motor_controller.h"
#include "nes_launcher.h"
#include "audio_player.h"
#include "config.h"
#include "netplay.h"

// ===================== 联机模式开关 =====================
volatile bool g_netplayWanted = false; // 用户在设置面板里勾选的状态
int g_rotation180 = 0;                 // 0 or 180

// ===================== 全局对象 =====================
Ws_AMOLED amoled;
GameUI gameUI;
NESLauncher nesLauncher;
AudioPlayer audioPlayer;

// Preloaded PCM sound effects (zero-latency)
uint8_t *g_selectPCM = nullptr;
size_t g_selectSize = 0;
uint8_t *g_startPCM = nullptr;
size_t g_startSize = 0;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

uint16_t screenW, screenH;
lv_obj_t *loading = nullptr;

// 触摸坐标旋转变换（180度时 x=W-x, y=H-y）
inline void rotateTouch(int16_t &x, int16_t &y)
{
    if (g_rotation180)
    {
        x = screenW - x;
        y = screenH - y;
    }
}

int bright[6] = {40, 60, 100, 126, 180, 220};
int b = 3;

// 应用状态
enum AppState
{
    APP_MENU,
    APP_LAUNCHING,
    APP_PLAYING
};

volatile AppState appState = APP_MENU;

// 是否有物理按键
bool hasPhysicalButtons = true;

// 启动计时
static unsigned long launchStartTime = 0;
static String pendingRomPath = "";

// ===================== LVGL 显示回调 =====================
void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t w = area->x2 - area->x1 + 1;
    uint16_t h = area->y2 - area->y1 + 1;

    amoled.setAddrWindow(area->x1, area->y1, area->x2, area->y2);
    amoled.pushColors((uint16_t *)color_p, w * h);

    lv_disp_flush_ready(drv);
}
// ===================== LVGL 触摸回调 =====================
void my_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    int16_t x = 0, y = 0;
    uint8_t touched = amoled.getPoint(&x, &y);

    if (touched > 0)
    {
        rotateTouch(x, y);
        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    }
    else
    {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ===================== 按键初始化 =====================
void initButtons()
{
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_LEFT, INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_A, INPUT_PULLUP);
    pinMode(BTN_B, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_START, INPUT_PULLUP);
    pinMode(BTN_QUIT, INPUT_PULLUP);
    Serial.println("[BTN] Buttons initialized");
}

// ===================== 检测是否有物理按键 =====================
bool detectPhysicalButtons()
{
    // 简单检测：读取所有按键引脚
    // 如果全部为HIGH（上拉），可能没接按键
    // 这里默认返回true，用户可根据实际情况修改
    delay(50);

    // 检查引脚是否能正常读取
    int highCount = 0;
    if (digitalRead(BTN_UP) == HIGH)
        highCount++;
    if (digitalRead(BTN_DOWN) == HIGH)
        highCount++;
    if (digitalRead(BTN_LEFT) == HIGH)
        highCount++;
    if (digitalRead(BTN_RIGHT) == HIGH)
        highCount++;
    if (digitalRead(BTN_A) == HIGH)
        highCount++;
    if (digitalRead(BTN_B) == HIGH)
        highCount++;
    if (digitalRead(BTN_SELECT) == HIGH)
        highCount++;
    if (digitalRead(BTN_START) == HIGH)
        highCount++;
    if (digitalRead(BTN_QUIT) == HIGH)
        highCount++;

    // 如果全部为HIGH，假设有物理按键（上拉状态正常）
    bool detected = (highCount >= 6);
    Serial.printf("[BTN] Physical buttons detected: %s (%d/9 HIGH)\n",
                  detected ? "YES" : "NO", highCount);
    return detected;
}

// ===================== 读取NES手柄状态 =====================
uint8_t readNESPadState()
{
    uint8_t state = 0;
    if (digitalRead(BTN_A) == LOW)
        state |= NES_PAD_A;
    if (digitalRead(BTN_B) == LOW)
        state |= NES_PAD_B;
    if (digitalRead(BTN_SELECT) == LOW)
        state |= NES_PAD_SELECT;
    if (digitalRead(BTN_START) == LOW)
        state |= NES_PAD_START;
    if (digitalRead(BTN_UP) == LOW)
        state |= NES_PAD_UP;
    if (digitalRead(BTN_DOWN) == LOW)
        state |= NES_PAD_DOWN;
    if (digitalRead(BTN_LEFT) == LOW)
        state |= NES_PAD_LEFT;
    if (digitalRead(BTN_RIGHT) == LOW)
        state |= NES_PAD_RIGHT;
    return state;
}

// ===================== 触摸屏模拟手柄 =====================
uint8_t readTouchPadState()
{
    uint8_t state = 0;
    int16_t x = 0, y = 0;
    uint8_t touched = amoled.getPoint(&x, &y);

    if (touched == 0)
        return 0;
    rotateTouch(x, y);

    // 屏幕分区映射：
    //
    // ┌─────────┬─────────┬─────────┐
    // │         │         │         │
    // │  方向键  │  显示区  │  AB键   │
    // │  区域    │         │  区域   │
    // │         │         │         │
    // ├─────────┴────┬────┴─────────┤
    // │   SELECT     │    START     │
    // └──────────────┴──────────────┘

    int thirdW = screenW / 3;
    int bottomZone = screenH - 60;

    // 底部区域：SELECT / START
    if (y > bottomZone)
    {
        if (x < screenW / 2)
        {
            state |= NES_PAD_SELECT;
        }
        else
        {
            state |= NES_PAD_START;
        }
        return state;
    }

    // 左侧1/3：方向键
    if (x < thirdW)
    {
        int centerX = thirdW / 2;
        int centerY = screenH / 2;
        int dx = x - centerX;
        int dy = y - centerY;
        int deadZone = 25;

        if (abs(dx) > deadZone || abs(dy) > deadZone)
        {
            // 8方向支持
            if (dy < -deadZone)
                state |= NES_PAD_UP;
            if (dy > deadZone)
                state |= NES_PAD_DOWN;
            if (dx < -deadZone)
                state |= NES_PAD_LEFT;
            if (dx > deadZone)
                state |= NES_PAD_RIGHT;
        }
    }
    // 右侧1/3：AB键
    else if (x > thirdW * 2)
    {
        int localX = x - thirdW * 2;
        int localCenterY = screenH / 2;

        // 上半部分 = A，下半部分 = B
        if (y < localCenterY)
        {
            // A键区域（也可以按X位置细分）
            state |= NES_PAD_A;
        }
        else
        {
            state |= NES_PAD_B;
        }

        // 如果按在中间区域，同时按AB
        if (abs(y - localCenterY) < 30)
        {
            state |= NES_PAD_A | NES_PAD_B;
        }
    }

    return state;
}
// =============== 电池供电保持（Waveshare 2.41" GPIO16=BAT_Control）===============
void PowerOn()
{
    digitalWrite(POWER_KEY_GPIO, HIGH); // 先拉高防抖动
    pinMode(POWER_KEY_GPIO, OUTPUT);
    digitalWrite(POWER_KEY_GPIO, HIGH); // 保持高电平 → PMU 维持电池供电
    delay(100);
}
// ===================== SD卡初始化 =====================
bool initSD()
{
    Serial.println("[SD] Initializing SD card...");
    Serial.printf("[SD] Pins: MISO=%d, MOSI=%d, CLK=%d, CS=%d\n",
                  SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN, SD_CS_PIN);

    // 使用自定义SPI引脚初始化SD卡
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    if (!SD.begin(SD_CS_PIN, SPI, 40000000, "/sd"))
    { // 40MHz SPI, VFS mount
        Serial.println("[SD] SD card init FAILED!");
        Serial.println("[SD] Check:");
        Serial.println("[SD]   1. SD card inserted?");
        Serial.println("[SD]   2. FAT32 formatted?");
        Serial.println("[SD]   3. Wiring correct?");
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE)
    {
        Serial.println("[SD] No SD card detected!");
        return false;
    }

    const char *typeStr = "UNKNOWN";
    switch (cardType)
    {
    case CARD_MMC:
        typeStr = "MMC";
        break;
    case CARD_SD:
        typeStr = "SD";
        break;
    case CARD_SDHC:
        typeStr = "SDHC";
        break;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
    uint64_t totalSpace = SD.totalBytes() / (1024 * 1024);
    Serial.printf("[SD] Type: %s, Size: %lluMB, Used: %lluMB / %lluMB\n",
                  typeStr, cardSize, usedSpace, totalSpace);

    // 创建游戏目录
    if (!SD.exists("/game"))
    {
        SD.mkdir("/game");
        Serial.println("[SD] Created /game/");
    }
    if (!SD.exists("/game/nes"))
    {
        SD.mkdir("/game/nes");
        Serial.println("[SD] Created /game/nes/");
    }

    return true;
}

// ===================== LVGL初始化 =====================
void initLVGL()
{
    lv_init(); // FS driver auto-registers via LV_USE_FS_STDIO in lv_conf.h

    uint32_t bufSize = (uint32_t)screenW * screenH;
    buf1 = (lv_color_t *)ps_malloc(bufSize * sizeof(lv_color_t));

    if (!buf1)
    {
        Serial.println("[LVGL] Buffer alloc FAILED!");
        while (1)
            delay(100);
    }
    Serial.printf("[LVGL] Framebuffer: %u bytes in PSRAM\n", bufSize * 2);

    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, bufSize);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenW;
    disp_drv.ver_res = screenH;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("[LVGL] Initialized OK");
}

// ===================== 释放LVGL资源 =====================
void deinitLVGL()
{
    // ★ 必须先把 display 从 LVGL 中移除，再释放 framebuffer。
    // 否则 initLVGL() 每次都会注册新的 display，旧的 display 仍然指向
    // 已释放的 framebuffer → lv_timer_handler() 刷新时 use-after-free → 堆损坏。
    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        lv_obj_clean(lv_scr_act());   // 先清空所有 UI 对象
        lv_disp_remove(disp);         // 再移除 display（自动清理 screen/indev）
    }

    if (buf1)
    {
        free(buf1);
        buf1 = nullptr;
        Serial.println("[LVGL] Framebuffer freed");
    }
}

// ===================== 显示启动画面 =====================
void showSplashScreen()
{

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    // NES Logo
    lv_obj_t *logo = lv_label_create(scr);
    lv_label_set_text(logo, "NES BOX");
    lv_obj_set_style_text_font(logo, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(logo, lv_color_hex(0x00D4FF), 0);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "Game Center");
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0xFF6B35), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 15);

    loading = lv_label_create(scr);
    lv_label_set_text(loading, "Scanning...");
    lv_obj_set_style_text_font(loading, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(loading, lv_color_hex(0x888888), 0);
    lv_obj_align(loading, LV_ALIGN_CENTER, 0, 70);

    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 300, 6);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 95);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00D4FF), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 2000);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t v)
                        { lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF); });
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);

    for (int i = 0; i < 120; i++)
    {
        lv_timer_handler();
        audioPlayer.loop(); // MP3 解码需要持续服务
        delay(2);
    }
}

// ===================== 错误画面 =====================
void showSDErrorScreen()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A2E), 0);

    lv_obj_t *icon = lv_label_create(scr);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFF4444), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *msg = lv_label_create(scr);
    char msgBuf[256];
    snprintf(msgBuf, sizeof(msgBuf),
             "SD Card Error!\n\n"
             "Pins: MISO=%d MOSI=%d CLK=%d CS=%d\n\n"
             "Check:\n"
             "1. SD card is inserted\n"
             "2. FAT32 formatted\n"
             "3. Place .nes in /game/nes/",
             SD_MISO_PIN, SD_MOSI_PIN, SD_CLK_PIN, SD_CS_PIN);
    lv_label_set_text(msg, msgBuf);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msg, screenW - 40);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 40);
}

void showNoGamesScreen()
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A2E), 0);

    lv_obj_t *icon = lv_label_create(scr);
    lv_label_set_text(icon, LV_SYMBOL_DRIVE);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFAA00), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -50);

    lv_obj_t *msg = lv_label_create(scr);
    lv_label_set_text(msg,
                      "No NES Games Found!\n\n"
                      "Copy .nes ROM files to:\n"
                      "SD:/game/nes/\n\n"
                      "Then restart the device.");
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(msg, screenW - 60);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 40);
}

// ===================== 启动NES游戏 =====================

void startNESGame(const String &romPath)
{
    String vfsPath = "/sd" + romPath;
    Serial.printf("[APP] Starting NES: %s\n", vfsPath.c_str());

    // ====== 联机握手（在释放 LVGL 前显示 UI）======
    bool netplayActive = false;
    //g_netplayWanted = false; //////////////////////////////////////////暂时关闭联机
    if (g_netplayWanted)
    {
        // 显示"等待 Player 2"提示
        lv_obj_clean(lv_scr_act());
        lv_obj_t *scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

        lv_obj_t *title = lv_label_create(scr);
        lv_label_set_text(title, LV_SYMBOL_WIFI "  Netplay");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0x00D4FF), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);

        lv_obj_t *msg = lv_label_create(scr);
        lv_label_set_text(msg, "Waiting for Player join...\n(30s timeout)");
        lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(msg, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, 30);

        // 后台异步握手，前台刷新 UI + 监听 QUIT 取消
        unsigned long start = millis();
        bool started = false;
        // 用单独任务跑 netplay_start，避免阻塞 LVGL
        TaskHandle_t hsTask;
        struct HsArg
        {
            volatile bool *done;
            volatile bool *ok;
        };
        static volatile bool hsDone = false, hsOk = false;
        hsDone = false;
        hsOk = false;
        xTaskCreatePinnedToCore([](void *)
                                {
            hsOk = netplay_start(30000);
            hsDone = true;
            vTaskDelete(NULL); }, "np_hs", 4096, NULL, 5, &hsTask, 0);

        while (!hsDone)
        {
            lv_timer_handler();
            delay(20);
            // 按 QUIT 取消
            if (digitalRead(BTN_QUIT) == LOW)
            {
                netplay_abort();
                Serial.println("[APP] Netplay handshake cancelled");
                delay(300);
                break;
            }
            if (millis() - start > 32000)
                break;
        }
        netplayActive = hsOk;

        if (netplayActive)
        {
            lv_label_set_text(msg,
                              netplay_role() == NP_ROLE_HOST
                                  ? "Connected as HOST (P1)\nStarting..."
                                  : "Connected as CLIENT (P2)\nStarting...");
            lv_timer_handler();
            delay(800);
        }
        else
        {
            lv_label_set_text(msg, "No peer found.\nStarting single player...");
            lv_timer_handler();
            delay(1000);
            g_netplayWanted = false; // 自动关掉
        }
    }

    // 释放 LVGL 帧缓冲
    deinitLVGL();

    // 清屏
    uint16_t *blackBuf = (uint16_t *)ps_malloc(screenW * screenH * sizeof(uint16_t));
    if (blackBuf)
    {
        memset(blackBuf, 0, screenW * screenH * sizeof(uint16_t));
        amoled.pushColors(0, 0, screenW, screenH, blackBuf);
        free(blackBuf);
    }
    delay(200);

    if (!nesLauncher.init(&amoled, screenW, screenH))
    {
        Serial.println("[APP] NES init failed!");
        appState = APP_MENU;
        initLVGL();
        gameUI.show();
        return;
    }

    if (!nesLauncher.loadAndRun(vfsPath))
    {
        Serial.println("[APP] ROM load failed!");
        nesLauncher.stop();
        appState = APP_MENU;
        initLVGL();
        gameUI.show();
        return;
    }

    appState = APP_PLAYING;
    Serial.printf("[APP] NES running, netplay=%s\n", netplayActive ? "ON" : "OFF");
}

// ===================== 返回菜单 =====================
void returnToMenu()
{
    Serial.println("[APP] Returning to menu...");

    nesLauncher.stop();
    delay(100);

    // 联机结束后，重置状态（保留 ESP-NOW 初始化，等下次再用）
    if (netplay_is_enabled() || netplay_state() != NP_IDLE)
    {
        Serial.println("[APP] Netplay session ended");
        // 保留 init，仅停止当前会话
    }

    // ★ 新增：把 AudioPlayer 重新拉起来
    audioPlayer.reinit();

    // 重新初始化LVGL和UI
    initLVGL();
    lv_obj_clean(lv_scr_act());
    gameUI.show();

    appState = APP_MENU;
    Serial.println("[APP] Back to menu");
}

// ===================== 菜单按键处理 =====================
void handleMenuButtons()
{
    if (!hasPhysicalButtons)
        return;

    static unsigned long lastPress = 0;
    unsigned long now = millis();
    if (now - lastPress < 180)
        return;

    bool pressed = false;

    if (digitalRead(BTN_UP) == LOW)
    {
        gameUI.navigateUp();
        pressed = true;
    }
    else if (digitalRead(BTN_DOWN) == LOW)
    {
        gameUI.navigateDown();
        pressed = true;
    }
    else if (digitalRead(BTN_LEFT) == LOW)
    {
        gameUI.navigateLeft();
        pressed = true;
    }
    else if (digitalRead(BTN_RIGHT) == LOW)
    {
        gameUI.navigateRight();
        pressed = true;
    }
    else if (digitalRead(BTN_SELECT) == LOW)
    {
        gameUI.selectCurrentGame();
        pressed = true;
    }
    else if (digitalRead(BTN_START) == LOW)
    {
        gameUI.switchViewMode();
        pressed = true;
    }

    if (pressed)
    {
        lastPress = now;
    }
}

// ===================== 游戏按键处理 =====================
void handleGameButtons()
{
    // 专用退出键 — 按下300ms后返回菜单（Core 1 osd_getinput 也会检测）
    static unsigned long quitPressStart = 0;
    if (digitalRead(BTN_QUIT) == LOW)
    {
        if (quitPressStart == 0)
        {
            quitPressStart = millis();
        }
        else if (millis() - quitPressStart >= 300)
        {
            Serial.println("[APP] QUIT button pressed!");
            quitPressStart = 0;
            returnToMenu();
            return;
        }
    }
    else
    {
        quitPressStart = 0;
    }

    uint8_t padState;
    if (hasPhysicalButtons)
    {
        padState = readNESPadState();
    }
    else
    {
        padState = readTouchPadState();
    }

    nesLauncher.setButtonState(padState);

    // 退出组合键 SELECT+START 保持2秒
    static unsigned long exitComboStart = 0;
    static bool exitComboActive = false;
    if ((padState & NES_PAD_SELECT) && (padState & NES_PAD_START))
    {
        if (!exitComboActive)
        {
            exitComboActive = true;
            exitComboStart = millis();
        }
        else if (millis() - exitComboStart >= 2000)
        {
            Serial.println("[APP] Exit combo triggered!");
            exitComboActive = false;
            returnToMenu();
            return;
        }
    }
    else
    {
        exitComboActive = false;
    }
}

// ===================== 设置持久化 =====================
int g_settingBrightness = 126; // default
int g_settingVolume = 12;      // default
static unsigned long g_lastSliderChange = 0;

void loadSettings()
{
    File f = SD.open("/game/settings.cfg", FILE_READ);
    if (!f)
    {
        Serial.println("[CFG] No settings file, using defaults");
        return;
    }
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.startsWith("brightness="))
        {
            line.replace("brightness=", "");
            g_settingBrightness = line.toInt();
        }
        else if (line.startsWith("volume="))
        {
            line.replace("volume=", "");
            g_settingVolume = line.toInt();
        }
        else if (line.startsWith("online="))
        {
            line.replace("online=", "");
            g_netplayWanted = (line == "1");
        }
        else if (line.startsWith("rotate="))
        {
            line.replace("rotate=", "");
            g_rotation180 = (line == "1") ? 180 : 0;
        }
    }
    f.close();
    // Clamp values
    if (g_settingBrightness < 25)
        g_settingBrightness = 25;
    if (g_settingBrightness > 255)
        g_settingBrightness = 255;
    if (g_settingVolume < 0)
        g_settingVolume = 0;
    if (g_settingVolume > 21)
        g_settingVolume = 21;
    // Apply
    amoled.setBrightness(g_settingBrightness);
    audioPlayer.setVolume((uint8_t)g_settingVolume);
    Serial.printf("[CFG] Loaded: brightness=%d volume=%d\n",
                  g_settingBrightness, g_settingVolume);
}

void saveSettings()
{
    File f = SD.open("/game/settings.cfg", FILE_WRITE);
    if (!f)
    {
        Serial.println("[CFG] Failed to save settings!");
        return;
    }
    f.printf("brightness=%d\nvolume=%d\nonline=%d\nrotate=%d\n",
             g_settingBrightness, g_settingVolume, g_netplayWanted,
             g_rotation180 ? 1 : 0);
    f.close();
    Serial.printf("[CFG] Saved: brightness=%d volume=%d online=%d\n",
                  g_settingBrightness, g_settingVolume, g_netplayWanted ? 1 : 0);
}

// Called from game_ui slider callbacks (with debounce)
extern int g_nesVolume; // in nofrendo_osd.cpp

extern "C" void settings_changed(int brightness, int volume)
{
    g_settingBrightness = brightness;
    g_settingVolume = volume;
    g_nesVolume = volume; // sync NES PCM volume
    amoled.setBrightness(brightness);
    audioPlayer.setVolume((uint8_t)volume);
    g_lastSliderChange = millis();
}

// Called from main loop to save after slider inactivity
void settings_update()
{
    if (g_lastSliderChange && millis() - g_lastSliderChange >= 1000)
    {
        g_lastSliderChange = 0;
        saveSettings();
    }
}

// ===================== SETUP =====================
void setup()
{
    PowerOn();
    Serial.begin(115200);
    delay(2000);
    Serial.println("==========================================");
    Serial.println("  ESP32-S3 NES Game Center");
    Serial.println("  Board: Touch-AMOLED-2.41");
    Serial.println("==========================================");

    // ===== 1. 初始化AMOLED =====
    bool ok = amoled.beginAMOLED_241();
    Serial.printf("[AMOLED] Init: %s\n", ok ? "OK" : "FAIL");
    if (!ok)
    {
        Serial.println("[AMOLED] FATAL: Display init failed!");
        while (1)
            delay(100);
    }
    screenW = amoled.width();
    screenH = amoled.height();
    // ===== 5. 初始化LVGL =====
    initLVGL();
    // ===== 2. 初始化SD卡（在启动画面期间） =====
    bool sdOK = initSD();
    if (!sdOK)
    {
        lv_obj_clean(lv_scr_act()); // 清除启动画面，避免重影
        showSDErrorScreen();
        Serial.println("[APP] SD error - waiting...");
        while (1)
        {
            lv_timer_handler();
            delay(5);
        }
    }
    // ===== 7. 初始化音频 =====
    audioPlayer.init(DI2S_BCLK_PIN, DI2S_LRC_PIN, DI2S_DIN_PIN);
    // ===== 3. 加载设置文件 =====
    loadSettings();
    if (g_rotation180)
    {
        amoled.setRotation(180);
        screenW = amoled.width();
        screenH = amoled.height();
    }

    Serial.printf("[AMOLED] Screen: %dx%d\n", screenW, screenH);
    amoled.setBrightness(bright[b]);

    // 再次确认电池供电保持（PMU 初始化后可能被重置）
    digitalWrite(POWER_KEY_GPIO, HIGH);

    // ===== 4a. 初始化震动马达 =====
    motorController_init();

    // ===== 4b. 初始化按键 =====
    initButtons();
    hasPhysicalButtons = detectPhysicalButtons();
    if (!hasPhysicalButtons)
    {
        Serial.println("[APP] No physical buttons, using touchscreen controls");
    }

    audioPlayer.playSFX(SD, "/game/music/power_up.mp3");
    // ===== 6. 启动画面 =====
    showSplashScreen();

    // 预加载 PCM 音效到 PSRAM（零延迟播放）
    audioPlayer.loadPCM("/sd/game/music/select.pcm", g_selectPCM, g_selectSize);
    audioPlayer.loadPCM("/sd/game/music/start.pcm", g_startPCM, g_startSize);

    // ===== 8. 初始化 ESP-NOW 联机（仅初始化，不连接）=====
    netplay_init();

    g_nesVolume = g_settingVolume; // sync NES PCM volume

    // ===== 9. 初始化游戏UI =====
    gameUI.init(screenW, screenH);
    gameUI.setLaunchCallback([](const String &romPath)
                             {
        Serial.printf("[APP] Launch: %s\n", romPath.c_str());
        pendingRomPath = romPath;
        launchStartTime = millis();
        appState = APP_LAUNCHING; });

    // ===== 10. 加载游戏列表 =====
    bool hasGames = gameUI.loadGameList("/game/nes");

    // 扫描完成，清除启动画面
    lv_obj_clean(lv_scr_act());
    loading = nullptr;

    if (!hasGames)
    {
        lv_obj_clean(lv_scr_act()); // 清除启动画面残留
        showNoGamesScreen();
        Serial.println("[APP] No games found");
        while (1)
        {
            lv_timer_handler();
            delay(5);
        }
    }

    // ===== 11. 显示游戏选择UI =====
    gameUI.show();

    // ===== 12. 打印系统信息 =====
    Serial.println("==========================================");
    Serial.println("  System Ready!");
    Serial.printf("  Screen: %dx%d\n", screenW, screenH);
    Serial.printf("  PSRAM Free: %d bytes\n", ESP.getFreePsram());
    Serial.printf("  Heap Free: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("  Physical Buttons: %s\n", hasPhysicalButtons ? "YES" : "NO (touch mode)");
    Serial.println("  ─────────────────────────────────────");
    Serial.println("  Controls (Physical Buttons):");
    Serial.printf("    UP=%d DOWN=%d LEFT=%d RIGHT=%d\n", BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT);
    Serial.printf("    A=%d B=%d SELECT=%d START=%d QUIT=%d\n", BTN_A, BTN_B, BTN_SELECT, BTN_START, BTN_QUIT);
    Serial.println("  ─────────────────────────────────────");
    Serial.println("  Controls (Touch Screen):");
    Serial.println("    Left 1/3  = D-Pad");
    Serial.println("    Right 1/3 = A/B buttons");
    Serial.println("    Bottom    = SELECT/START");
    Serial.println("  ─────────────────────────────────────");
    Serial.println("  In Menu:");
    Serial.println("    SELECT = Launch game");
    Serial.println("    START  = Switch view mode");
    Serial.println("  In Game:");
    Serial.println("    QUIT = Exit to menu");
    Serial.println("    SELECT+START (2s) = Exit to menu (alternative)");
    Serial.println("  ─────────────────────────────────────");
    Serial.printf("  SD Card Pins: CS=%d CLK=%d MOSI=%d MISO=%d\n",
                  SD_CS_PIN, SD_CLK_PIN, SD_MOSI_PIN, SD_MISO_PIN);
    Serial.println("==========================================");

    analogSetPinAttenuation(ADC_11DB_PIN, ADC_11db);
    analogReadResolution(12);

    // audioPlayer.playSFX(SD, "/game/music/See You Again 320kb.mp3");
}

// ===================== LOOP =====================
void loop()
{
    // Audio only in menu/launching (I2S is taken over by NES during gameplay)
    if (appState != APP_PLAYING)
    {
        audioPlayer.loop();
    }
    switch (appState)
    {

    // ========== 菜单模式 ==========
    case APP_MENU:
        settings_update();
        lv_timer_handler();
        handleMenuButtons();
        gameUI.update();
        delay(5);
        break;

    // ========== 启动过渡 ==========
    case APP_LAUNCHING:
        lv_timer_handler();

        // 等待启动动画完成（约1.8秒）
        if (pendingRomPath.length() > 0 && millis() - launchStartTime >= 500)
        {
            String romPath = pendingRomPath;
            pendingRomPath = "";

            // 多渲染几帧确保动画结束
            for (int i = 0; i < 30; i++)
            {
                lv_timer_handler();
                delay(2);
            }

            startNESGame(romPath);
        }
        else
        {
            delay(5);
        }
        break;

    // ========== 游戏运行模式 ==========
    case APP_PLAYING:
        // 模拟器退出后自动返回菜单（Core 0 在 SPI 释放后恢复）
        if (!nesLauncher.isRunning())
        {
            returnToMenu();
            break;
        }
        handleGameButtons();
        nesLauncher.frameUpdate();
        delay(1);
        break;
    }
}
