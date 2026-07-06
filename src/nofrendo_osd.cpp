/*
 * nofrendo_osd.cpp — OSD layer for arduino-nofrendo on ESP32-S3
 *
 * Fix for 600x450 fullscreen audio stutter:
 * - Audio production driven by esp_timer (independent of video frame rate)
 * - Lower sample rate (22050) to reduce CPU/DMA pressure
 * - Optimized scaling loop
 * - Smaller ring buffer to reduce latency
 */

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/ringbuf.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <driver/i2s.h>
#include <string.h>
#include <stdio.h>

extern "C"
{
#include <noftypes.h>
#include <osd.h>
#include <event.h>
#include <gui.h>
#include <log.h>
#include <nofconfig.h>
#include <nes/nes.h>
#include <nes/nesinput.h>
#include <bitmap.h>
}

#include "Ws_AMOLED.h"
#include "audio_player.h"
#include "config.h"
#include "damage_detector.h"
#include "motor_controller.h"
#include "hp_scan.h"
#include "nes_launcher.h" // NES_PAD_* defines
#include "netplay.h"
#include "netplay.h"

extern AudioPlayer audioPlayer;
extern int g_settingBrightness;
extern int g_rotation180;
extern "C" void settings_changed(int brightness, int volume);

#define SW16(c) (((c) >> 8) | ((c) << 8))

// ===================== Display Constants =====================
#define NES_W 256
#define NES_H 240

// ===================== NES Audio Constants =====================
// 降低采样率：NES 音质足够，大幅减轻 I2S + CPU 压力
static const int NES_AUDIO_RATE = 22050;

// 音频定时器频率：每秒调用多少次音频生产
// 120Hz = 每 8.3ms 生产一小批，比 60Hz 更平滑
#define AUDIO_TIMER_HZ 120

// 每次定时器回调生产的采样数：22050/120 ≈ 184
#define AUDIO_SAMPLES_PER_TICK 184
#define AUDIO_MAX_SAMPLES 256

// Ring buffer：小一些控制延迟
// 每次 stereo bytes = 184*4 = 736, 缓存约 10 次 = 7360
#define AUDIO_RINGBUFFER_BYTES (16 * 1024)

#define AUDIO_TASK_STACK 4096
#define AUDIO_TASK_PRIORITY 5

// ===================== Global State =====================
static Ws_AMOLED *g_amoled = nullptr;
static uint16_t g_screenW = SCREEN_W;
static uint16_t g_screenH = SCREEN_H;
volatile uint8_t g_padState = 0;
volatile bool g_quitRequested = false;
static bool g_paused = false;
static int g_touchStartY = -1, g_touchStartX = 0;

// ===================== NES Audio State =====================
static void (*g_apuProcess)(void *, int) = nullptr;

static int16_t *g_audioMonoBuf = nullptr;
static int16_t *g_audioStereoBuf = nullptr;

static RingbufHandle_t g_audioRing = nullptr;
static TaskHandle_t g_audioTask = nullptr;
static volatile bool g_audioTaskRun = false;

#ifdef HP_SCAN_ENABLE
static TaskHandle_t g_hpScanTask = nullptr;
static volatile bool g_hpScanTaskRun = false;
#endif

static esp_timer_handle_t g_audioTimer = nullptr;
static volatile bool g_osdCleanupInProgress = false;
static volatile bool g_osdCleanupDone = false;

// 分数累加器
static double g_audioFracAccum = 0.0;
static portMUX_TYPE g_audioMux = portMUX_INITIALIZER_UNLOCKED;

int g_nesVolume = 12; // 0-21

static uint32_t g_audioDropCount = 0;
static uint32_t g_audioWriteFailCount = 0;

// ===================== Forward Declarations =====================
static void scaleAndPush(bitmap_t *bmp);
static void drawBatteryIcon();
static void drawPauseOverlay();
static void nes_audio_init();
static void nes_audio_deinit();
static void nes_audio_task(void *arg);
static void nes_audio_timer_cb(void *arg);
static void nes_i2s_reconfig();

// ==================================================================
//  Memory Allocation
// ==================================================================
extern "C" void *mem_alloc(int size, bool prefer_fast_memory)
{
    if (prefer_fast_memory)
    {
        if (size > 32768) {
            void *p = ps_malloc(size);
            if (p)
                return p;
        }

        void *p = heap_caps_malloc(size, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (p)
            return p;

        p = ps_malloc(size);
        if (p)
            return p;

        return heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }

    void *p = ps_malloc(size);
    if (!p)
    {
        p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return p;
}

// ==================================================================
//  Video Driver
// ==================================================================
static uint8_t *g_fbData = nullptr;
static uint16_t *g_rgb565Buf = nullptr;
static uint16_t g_nesPal565[256];

static uint16_t g_srcX[SCREEN_W];
static uint16_t g_srcY[SCREEN_H];

static void buildScaleTables()
{
    for (int dx = 0; dx < SCREEN_W; dx++)
    {
        g_srcX[dx] = (uint16_t)((uint32_t)dx * NES_W / SCREEN_W);
    }
    for (int dy = 0; dy < SCREEN_H; dy++)
    {
        g_srcY[dy] = (uint16_t)((uint32_t)dy * NES_H / SCREEN_H);
    }
}

static int osd_vid_init(int width, int height)
{
    (void)width;
    (void)height;
#ifdef HP_SCAN_ENABLE
    Serial.println("[HP_SCAN] runtime disabled during gameplay to preserve video");
#endif
    return 0;
}

static void osd_vid_shutdown(void)
{
}

static int osd_vid_set_mode(int width, int height)
{
    (void)width;
    (void)height;
    return 0;
}

static void osd_vid_set_palette(rgb_t *pal)
{
    for (int i = 0; i < 256; i++)
    {
        uint16_t r = (pal[i].r >> 3) & 0x1F;
        uint16_t g = (pal[i].g >> 2) & 0x3F;
        uint16_t b = (pal[i].b >> 3) & 0x1F;
        uint16_t rgb565 = (r << 11) | (g << 5) | b;
        g_nesPal565[i] = (rgb565 >> 8) | (rgb565 << 8);
    }
}

static void osd_vid_clear(uint8 color)
{
    (void)color;
}

static bitmap_t *osd_vid_lock_write(void)
{
    if (!g_fbData)
    {
        g_fbData = (uint8_t *)ps_malloc(NES_W * NES_H);
        if (!g_fbData)
        {
            Serial.println("[OSD] FATAL: fb alloc failed!");
            return nullptr;
        }
    }
    return bmp_createhw(g_fbData, NES_W, NES_H, NES_W);
}

static void osd_vid_free_write(int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;
}

static void osd_vid_custom_blit(bitmap_t *bmp, int num_dirties, rect_t *dirty_rects)
{
    (void)num_dirties;
    (void)dirty_rects;

    // ★ 音频现在由独立定时器驱动，这里不再调用 nes_audio_frame()
    // 只做视频推送
    scaleAndPush(bmp);
}

static viddriver_t nes_vid_driver = {
    "ESP32-AMOLED",
    osd_vid_init,
    osd_vid_shutdown,
    osd_vid_set_mode,
    osd_vid_set_palette,
    osd_vid_clear,
    osd_vid_lock_write,
    osd_vid_free_write,
    osd_vid_custom_blit,
    false};

// ==================================================================
//  Scale & Push — 优化：逐行处理减少 cache miss
// ==================================================================
// ★ 改回整屏缓冲（PSRAM），不再是单行
// static uint16_t *g_rgb565Buf = nullptr;

#ifdef HP_SCAN_ENABLE
static void hp_scan_task(void *arg)
{
    (void)arg;
    while (g_hpScanTaskRun)
    {
        hp_scan_update();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    vTaskDelete(NULL);
}
#endif

static void scaleAndPush(bitmap_t *bmp)
{
    if (!bmp || !g_amoled)
        return;

    // 首次分配整屏 RGB565 缓冲到 PSRAM
    if (!g_rgb565Buf)
    {
        g_rgb565Buf = (uint16_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t));
        if (!g_rgb565Buf)
        {
            Serial.println("[OSD] FATAL: RGB565 full buffer alloc failed!");
            return;
        }
        buildScaleTables();
        Serial.printf("[OSD] RGB565 buffer: %d bytes in PSRAM\n",
                      SCREEN_W * SCREEN_H * (int)sizeof(uint16_t));
    }

    // 缩放：索引色 → RGB565
    for (int dy = 0; dy < SCREEN_H; dy++)
    {
        uint8_t *srcLine = bmp->line[g_srcY[dy]];
        uint16_t *dstLine = g_rgb565Buf + dy * SCREEN_W;

        int dx = 0;
        // 4 像素展开
        for (; dx <= SCREEN_W - 4; dx += 4)
        {
            dstLine[dx] = g_nesPal565[srcLine[g_srcX[dx]]];
            dstLine[dx + 1] = g_nesPal565[srcLine[g_srcX[dx + 1]]];
            dstLine[dx + 2] = g_nesPal565[srcLine[g_srcX[dx + 2]]];
            dstLine[dx + 3] = g_nesPal565[srcLine[g_srcX[dx + 3]]];
        }
        for (; dx < SCREEN_W; dx++)
        {
            dstLine[dx] = g_nesPal565[srcLine[g_srcX[dx]]];
        }
    }

    // ★ 一次性推送整屏（和原来 256x240 时一样的调用方式）
    // HP scan runtime is intentionally disabled during gameplay to keep the display path stable.
#ifdef HP_SCAN_ENABLE
    // Skip damage/motor updates while HP scan is enabled to avoid interfering with the emulator frame path.
#else
    damageDetector_update();
    motorController_update();
#endif

    g_amoled->setAddrWindow(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    drawBatteryIcon();
    if (g_paused)
        drawPauseOverlay();
    g_amoled->pushColors(0, 0, SCREEN_W, SCREEN_H, g_rgb565Buf);
}

// ==================================================================
//  Battery Icon — 30×16 at (4,4), updated every 60 frames
// ==================================================================
static void drawBatteryIcon()
{
    static int battPercent = -1;
    static bool charging = false;
    static int frameCount = 0;

    if (++frameCount % 60 == 0)
    {
        // Waveshare BAT_ADC = GPIO 17, voltage divider ~1:2
        int raw = analogRead(ADC_11DB_PIN);  // 0-4095 (12-bit ADC)
        uint16_t mv = raw * 10000 / 4095;  // 3.3V ref * ~3 divider (100K:200K)
        if (mv > 1000) {
            battPercent = constrain(map(mv, 3000, 4200, 0, 100), 0, 100);
        }
        charging = g_amoled->isVbusIn();  // USB connected = charging
        //ets_printf("[BAT] Battery : %u\n", battPercent);
    }
    if (battPercent < 0)
        return;

    int x0 = 4, y0 = 4;
    int bw = 22, bh = 12, termW = 3, termH = 5;
    int termY = y0 + (bh - termH) / 2;

    uint16_t fillColor;
    if (charging || battPercent > 60)
        fillColor = SW16(0x07E0); // green
    else if (battPercent > 20)
        fillColor = SW16(0xFFE0); // yellow
    else
        fillColor = SW16(0xF800); // red
    uint16_t white = SW16(0xFFFF);

    // Body outline
    for (int dy = 0; dy < bh; dy++)
        for (int dx = 0; dx < bw; dx++)
            if (dy == 0 || dy == bh - 1 || dx == 0 || dx == bw - 1)
                g_rgb565Buf[(y0 + dy) * SCREEN_W + x0 + dx] = white;
    // Terminal
    for (int dy = 0; dy < termH; dy++)
        for (int dx = 0; dx < termW; dx++)
            if (dy == 0 || dy == termH - 1 || dx == termW - 1)
                g_rgb565Buf[(termY + dy) * SCREEN_W + x0 + bw + dx] = white;
            else
                g_rgb565Buf[(termY + dy) * SCREEN_W + x0 + bw + dx] = fillColor;
    // Fill level
    int fillW = (bw - 2) * battPercent / 100;
    for (int dy = 1; dy < bh - 1; dy++)
        for (int dx = 0; dx < fillW; dx++)
            g_rgb565Buf[(y0 + dy) * SCREEN_W + x0 + 1 + dx] = fillColor;
    // Charging "+"
    if (charging)
    {
        int cx = x0 + bw / 2, cy = y0 + bh / 2;
        for (int d = -2; d <= 2; d++)
        {
            g_rgb565Buf[cy * SCREEN_W + cx + d] = white;
            g_rgb565Buf[(cy + d) * SCREEN_W + cx] = white;
        }
    }
}

// ==================================================================
//  Pause Overlay — drawn on g_rgb565Buf when game is paused
// ==================================================================
// Simple 5x7 pixel font for overlay text (only needed glyphs)

static const uint8_t FONT5x7[][7] = {
    // ' ' = 0
    // A=1  B=2  D=3  E=4  G=5  H=6  I=7  L=8  M=9  N=10  O=11  P=12  R=13  S=14  T=15  U=16  V=17  X=18
    [0] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // space
    [1] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // A
    [2] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
    [3] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},  // D
    [4] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},  // E
    [5] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E},  // G
    [6] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // H
    [7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},  // I
    [8] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // L
    [9] = {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11},  // M
    [10] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
    [11] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
    [12] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
    [13] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x11}, // R
    [14] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    [15] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    [16] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    [17] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04}, // V
    [18] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
};

static const char *FONT_CHARS = " ABDEGHILMNOPRSTUVX";

static int fontIdx(char c)
{
    const char *p = strchr(FONT_CHARS, c);
    return p ? (int)(p - FONT_CHARS) : 3; // default to 'E'
}

static void drawText(const char *s, int x0, int y0, uint16_t color, int scale = 2, int bold = 2)
{
    if (scale <= 0)
        scale = 1;
    if (bold <= 0)
        bold = 1;

    while (*s)
    {
        int idx = fontIdx(*s);

        for (int dy = 0; dy < 7; dy++)
        {
            uint8_t row = FONT5x7[idx][dy];

            for (int dx = 0; dx < 5; dx++)
            {
                if (row & (1 << (4 - dx)))
                {
                    // ===== 绘制 (带 scale + bold) =====
                    for (int sy = 0; sy < scale; sy++)
                    {
                        for (int sx = 0; sx < scale; sx++)
                        {
                            for (int bx = 0; bx < bold; bx++)
                            {
                                int px = x0 + dx * scale + sx + bx;
                                int py = y0 + dy * scale + sy;

                                if (px >= 0 && px < SCREEN_W &&
                                    py >= 0 && py < SCREEN_H)
                                {
                                    g_rgb565Buf[py * SCREEN_W + px] = color;
                                }
                            }
                        }
                    }
                }
            }
        }

        // 字符间距也放大 + 粗体补偿
        x0 += (5 * scale) + (1 * scale) + (bold - 1);

        s++;
    }
}

// Helper: fill a solid rectangle
static void fillRect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > SCREEN_W)
        w = SCREEN_W - x;
    if (y + h > SCREEN_H)
        h = SCREEN_H - y;
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            g_rgb565Buf[(y + dy) * SCREEN_W + x + dx] = color;
}

// Helper: rounded rectangle (filled + border, for solid shapes on overlay)
static void drawRoundedRect(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, int bw)
{
    // Border: draw as four edge strips + corner dots
    if (bw > 0)
    {
        // Top and bottom strips
        fillRect(x + r, y - bw, w - 2 * r, bw, border);
        fillRect(x + r, y + h, w - 2 * r, bw, border);
        // Left and right strips
        fillRect(x - bw, y + r, bw, h - 2 * r, border);
        fillRect(x + w, y + r, bw, h - 2 * r, border);
        // Simplified corners: just draw the strips extending, it's fine at small bw
        fillRect(x + r - bw, y - bw, bw, bw, border);
        fillRect(x + w - r, y - bw, bw, bw, border);
        fillRect(x + r - bw, y + h, bw, bw, border);
        fillRect(x + w - r, y + h, bw, bw, border);
    }
    // Fill interior (same logic as before)
    fillRect(x + r, y, w - 2 * r, h, fill);
    fillRect(x, y + r, w, h - 2 * r, fill);

    for (int dy = 0; dy < r + bw; dy++)
    {
        for (int dx = 0; dx < r + bw; dx++)
        {
            int dist2 = dx * dx + dy * dy;

            uint16_t color;
            if (dist2 <= (r + bw) * (r + bw) && dist2 > r * r)
                color = border;
            else if (dist2 <= r * r)
                color = fill;
            else
                continue;

            int px, py;

            // 左上
            px = x + r - 1 - dx;
            py = y + r - 1 - dy;
            g_rgb565Buf[py * SCREEN_W + px] = color;

            // 右上
            px = x + w - r + dx;
            g_rgb565Buf[py * SCREEN_W + px] = color;

            // 右下
            py = y + h - r + dy;
            g_rgb565Buf[py * SCREEN_W + px] = color;

            // 左下
            px = x + r - 1 - dx;
            g_rgb565Buf[py * SCREEN_W + px] = color;
        }
    }
}

// Helper: RGB565 alpha blend (values in buffer are byte-swapped; un-swap, blend, re-swap)
static inline uint16_t blend565(uint16_t bg, uint16_t fg, int alpha)
{
    // Un-swap both to raw RGB565
    uint16_t bgr = (bg << 8) | (bg >> 8);
    uint16_t fgr = (fg << 8) | (fg >> 8);
    int r = ((bgr >> 11) & 0x1F) + ((((fgr >> 11) & 0x1F) - ((bgr >> 11) & 0x1F)) * alpha / 16);
    int g = ((bgr >> 5) & 0x3F) + ((((fgr >> 5) & 0x3F) - ((bgr >> 5) & 0x3F)) * alpha / 16);
    int b = (bgr & 0x1F) + (((fgr & 0x1F) - (bgr & 0x1F)) * alpha / 16);
    uint16_t raw = (r << 11) | (g << 5) | b;
    return (raw << 8) | (raw >> 8); // re-swap to match buffer format
}

// Helper: convert 24-bit RGB to byte-swapped RGB565 for AMOLED buffer
static inline uint16_t RGB24to565(int r8, int g8, int b8)
{
    uint16_t raw = ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3);
    return (raw << 8) | (raw >> 8); // byte-swap for display
}

// Pre-computed swapped colors (match UI theme from game_ui.h)
#define C_DIM RGB24to565(4, 4, 16)        // dark navy dim
#define C_CARD RGB24to565(18, 18, 58)     // card background
#define C_ACCENT RGB24to565(0, 212, 255)  // cyan accent
#define C_TITLE RGB24to565(26, 26, 78)    // title bar
#define C_SEP RGB24to565(42, 42, 106)     // separator
#define C_LABEL RGB24to565(170, 170, 255) // label text
#define C_TRACK RGB24to565(34, 34, 68)    // slider track bg
#define C_VOL RGB24to565(255, 107, 53)    // orange (volume)
#define C_BRI RGB24to565(0, 212, 255)     // cyan (brightness)
#define C_KNOB_V RGB24to565(255, 170, 85) // light orange knob
#define C_KNOB_B RGB24to565(68, 221, 255) // light cyan knob
#define C_BTN_G RGB24to565(0, 140, 60)    // green button
#define C_BTN_GB RGB24to565(0, 220, 100)  // green button border
#define C_BTN_R RGB24to565(160, 20, 20)   // red button
#define C_BTN_RB RGB24to565(220, 60, 60)  // red button border
#define C_TXT_G RGB24to565(136, 255, 136) // green text
#define C_TXT_R RGB24to565(255, 136, 136) // red text

static void drawPauseOverlay()
{
    // Smooth dimming: blend with dark navy at ~55%
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
        g_rgb565Buf[i] = blend565(g_rgb565Buf[i], C_DIM, 9);

    int v = g_nesVolume;
    int b = g_settingBrightness;

    // ---- Card ----
    int cx = 300, cy = 225;
    int cw = 460, ch = 280, cr = 16;
    int cardX = cx - cw / 2, cardY = cy - ch / 2;
    drawRoundedRect(cardX, cardY, cw, ch, cr, C_CARD, C_ACCENT, 2);

    // ---- Title ----
    fillRect(cardX + cr, cardY + 2, cw - 2 * cr, 38, C_TITLE);
    drawText("GAME  PAUSED", cx - 6 * 6, cardY + 16, C_ACCENT);

    // ---- Separator ----
    for (int dx = cardX + 20; dx < cardX + cw - 20; dx++)
        g_rgb565Buf[(cardY + 42) * SCREEN_W + dx] = C_SEP;

    // ---- Volume slider ----
    drawText("VOLUME", cardX + 30, cardY + 58, C_LABEL);
    fillRect(cardX + 30, cardY + 80, 400, 10, C_TRACK);
    int vfill = 400 * v / 21;
    fillRect(cardX + 30, cardY + 80, vfill, 10, C_VOL);
    int vknob = cardX + 30 + vfill;
    fillRect(vknob - 5, cardY + 74, 10, 22, C_KNOB_V);

    // ---- Brightness slider ----
    drawText("BRIGHT", cardX + 30, cardY + 114, C_LABEL);
    fillRect(cardX + 30, cardY + 136, 400, 10, C_TRACK);
    int bfill = 400 * (b - 25) / 230;
    fillRect(cardX + 30, cardY + 136, bfill, 10, C_BRI);
    int bknob = cardX + 30 + bfill;
    fillRect(bknob - 5, cardY + 130, 10, 22, C_KNOB_B);

    // ---- Buttons ----
    int btnY = cardY + 175, btnH = 42;
    int btn1X = cardX + 40, btn1W = 170;
    int btn2X = cardX + 250, btn2W = 170;

    drawRoundedRect(btn1X, btnY, btn1W, btnH, 10, C_BTN_G, C_BTN_GB, 2);
    drawText("RESUME", btn1X + 46, btnY + 14, C_TXT_G);

    drawRoundedRect(btn2X, btnY, btn2W, btnH, 10, C_BTN_R, C_BTN_RB, 2);
    drawText("EXIT", btn2X + 58, btnY + 14, C_TXT_R);

    // Separator above buttons
    for (int dx = cardX + 20; dx < cardX + cw - 20; dx++)
        g_rgb565Buf[(btnY - 10) * SCREEN_W + dx] = C_SEP;

}

// ==================================================================
//  Input — reads GPIOs directly on Core 1, netplay-aware
// ==================================================================
static uint32_t g_prevPad = 0xFFFFFFFF;

extern "C" void osd_getinput(void)
{
    // 1. Quit request — stop audio BEFORE firing quit event
    // main_quit() → nes_destroy() frees APU, so we must prevent
    // the 120Hz audio timer from calling g_apuProcess() into freed memory
    if (g_quitRequested)
    {
        if (g_audioTimer) {
            esp_timer_stop(g_audioTimer);
            esp_timer_delete(g_audioTimer);
            g_audioTimer = nullptr;
        }
        g_apuProcess = nullptr;
        event_t evh = event_get(event_quit);
        if (evh)
            evh(0);
        g_quitRequested = false;
        return;
    }

    // 2. Touch handling — swipe gesture or pause menu
    {
        int16_t tx = 0, ty = 0;
        bool touched = (g_amoled->getPoint(&tx, &ty) > 0);
        if (g_rotation180 && touched)
        {
            tx = g_screenW - tx;
            ty = g_screenH - ty;
        }

        if (g_paused)
        {
            // === PAUSE MENU: touch zone handling ===
            if (touched)
            {
                // Volume slider: card starts at x=70+30=100, track is 400px wide
                // Track y: cardY+80 = 85+80 = 165, height 10 → touch 155..185
                if (ty >= 155 && ty <= 185 && tx >= 100 && tx <= 500)
                {
                    int vol = (tx - 100) * 21 / 400;
                    if (vol < 0)
                        vol = 0;
                    if (vol > 21)
                        vol = 21;
                    g_nesVolume = vol;
                    audioPlayer.setVolume((uint8_t)vol);
                    settings_changed(g_settingBrightness, vol);
                }
                // Brightness slider: track y=85+136=221, height 10 → touch 211..241
                else if (ty >= 211 && ty <= 241 && tx >= 100 && tx <= 500)
                {
                    int bri = 25 + (tx - 100) * 230 / 400;
                    if (bri < 25)
                        bri = 25;
                    if (bri > 255)
                        bri = 255;
                    g_amoled->setBrightness(bri);
                    settings_changed(bri, g_nesVolume);
                }
                // Resume button: x=110..280, y=260..302
                else if (ty >= 255 && ty <= 310 && tx >= 110 && tx <= 280)
                {
                    g_paused = false;
                    event_t e = event_get(event_togglepause);
                    if (e)
                        e(INP_STATE_MAKE);
                    delay(100);
                }
                // Exit button: x=320..490, y=260..302
                else if (ty >= 255 && ty <= 310 && tx >= 320 && tx <= 490)
                {
                    g_paused = false;
                    // ★ 先停音频再退出：main_quit() → nes_destroy() 会释放 APU，
                    // 必须赶在 120Hz 音频定时器回调 g_apuProcess() 之前停掉它
                    if (g_audioTimer) {
                        esp_timer_stop(g_audioTimer);
                        esp_timer_delete(g_audioTimer);
                        g_audioTimer = nullptr;
                    }
                    g_apuProcess = nullptr;
                    g_quitRequested = true;
                    event_t e = event_get(event_quit);
                    if (e) e(INP_STATE_MAKE);
                    return;
                }
            }
            // When paused, skip all game input
            return;
        }
        else
        {
            // === GAMEPLAY: detect swipe-down from top ===
            static int lastTouchY = -1;
            if (touched)
            {
                if (g_touchStartY < 0 && ty < 80)
                    g_touchStartY = ty;
                lastTouchY = ty;
            }
            else
            {
                if (g_touchStartY >= 0 && lastTouchY >= 0 &&
                    (lastTouchY - g_touchStartY) > 100)
                {
                    g_paused = true;
                    i2s_zero_dma_buffer(I2S_NUM_0);  // 清除 I2S DMA 残留音频
                    event_t e = event_get(event_togglepause);
                    if (e)
                        e(INP_STATE_MAKE);
                }
                g_touchStartY = -1;
                lastTouchY = -1;
            }
        }
    }

    // 3. QUIT button — always checked locally, 300ms debounce
    {
        static unsigned long quitTimer = 0;
        if (digitalRead(BTN_QUIT) == LOW)
        {
            if (quitTimer == 0)
                quitTimer = millis();
            else if (millis() - quitTimer >= 300)
            {
                quitTimer = 0;
                // ★ 先停音频再退出：防止 APU 被 nes_destroy() 释放后
                // 120Hz 音频定时器回调 g_apuProcess() 访问野指针
                if (g_audioTimer) {
                    esp_timer_stop(g_audioTimer);
                    esp_timer_delete(g_audioTimer);
                    g_audioTimer = nullptr;
                }
                g_apuProcess = nullptr;
                g_quitRequested = true;
                event_t evh = event_get(event_quit);
                if (evh)
                    evh(0);
                return;
            }
        }
        else
        {
            quitTimer = 0;
        }
    }

    // 3. Read GPIOs directly — Core 0 main loop is blocked during emulation
    //    Build pad in NES_PAD_* bit order (A=0x01, B=0x02, SEL=0x04, START=0x08,
    //    UP=0x10, DOWN=0x20, LEFT=0x40, RIGHT=0x80)
    uint8_t local_pad = 0;
    if (digitalRead(BTN_A) == LOW)
        local_pad |= NES_PAD_A;
    if (digitalRead(BTN_B) == LOW)
        local_pad |= NES_PAD_B;
    if (digitalRead(BTN_SELECT) == LOW)
        local_pad |= NES_PAD_SELECT;
    if (digitalRead(BTN_START) == LOW)
        local_pad |= NES_PAD_START;
    if (digitalRead(BTN_UP) == LOW)
        local_pad |= NES_PAD_UP;
    if (digitalRead(BTN_DOWN) == LOW)
        local_pad |= NES_PAD_DOWN;
    if (digitalRead(BTN_LEFT) == LOW)
        local_pad |= NES_PAD_LEFT;
    if (digitalRead(BTN_RIGHT) == LOW)
        local_pad |= NES_PAD_RIGHT;

    // 4. Netplay or single-player event dispatch
    if (netplay_is_enabled())
    {
        uint8_t p1 = 0, p2 = 0;
        if (netplay_sync_frame(local_pad, &p1, &p2))
        {
            static int npDbg = 0;
            if (local_pad || p1 || p2 || ++npDbg % 120 == 0)
            {
                ets_printf("[NP] local=%02X p1=%02X p2=%02X role=%s\n",
                           local_pad, p1, p2,
                           netplay_role() == NP_ROLE_HOST ? "HOST" : "CLIENT");
            }
            if (netplay_should_reset())
            {
                event_t e = event_get(event_hard_reset);
                if (e)
                    e(INP_STATE_MAKE);
            }
            netplay_dispatch_pads(p1, p2);
        }
        else
        {
            // Netplay lost — fall back to local input, then exit
            static const int evt[8] = {
                event_joypad1_a, event_joypad1_b,
                event_joypad1_select, event_joypad1_start,
                event_joypad1_up, event_joypad1_down,
                event_joypad1_left, event_joypad1_right};
            static uint8_t last = 0;
            uint8_t diff = local_pad ^ last;
            for (int i = 0; i < 8; i++)
            {
                if (diff & (1 << i))
                {
                    event_t evh = event_get(evt[i]);
                    if (evh)
                        evh((local_pad & (1 << i)) ? INP_STATE_MAKE : INP_STATE_BREAK);
                }
            }
            last = local_pad;
            g_quitRequested = true;
        }
        return;
    }

    // 5. Single player: fire events from direct GPIO reads
    {
        static const int evt[8] = {
            event_joypad1_a, event_joypad1_b,
            event_joypad1_select, event_joypad1_start,
            event_joypad1_up, event_joypad1_down,
            event_joypad1_left, event_joypad1_right};
        static uint8_t last = 0;
        uint8_t diff = local_pad ^ last;
        for (int i = 0; i < 8; i++)
        {
            if (diff & (1 << i))
            {
                event_t evh = event_get(evt[i]);
                if (evh)
                    evh((local_pad & (1 << i)) ? INP_STATE_MAKE : INP_STATE_BREAK);
            }
        }
        last = local_pad;
    }
}

extern "C" void osd_getmouse(int *x, int *y, int *button)
{
    (void)x;
    (void)y;
    (void)button;
}

// ==================================================================
//  Timer
// ==================================================================
static esp_timer_handle_t g_nesTimer = nullptr;

extern "C" int osd_installtimer(int frequency, void *func, int funcsize,
                                void *counter, int countersize)
{
    (void)funcsize;
    (void)countersize;

    if (g_nesTimer)
    {
        esp_timer_stop(g_nesTimer);
        esp_timer_delete(g_nesTimer);
        g_nesTimer = nullptr;
    }

    if (!func || frequency <= 0)
    {
        Serial.printf("[OSD] Timer install failed: func=%p, freq=%d\n", func, frequency);
        return -1;
    }

    int64_t periodUs = 1000000LL / frequency;
    if (periodUs < 1000)
        periodUs = 1000;

    esp_timer_create_args_t timerArgs = {};
    timerArgs.callback = (esp_timer_cb_t)func;
    timerArgs.arg = counter;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "nesTimer";

    esp_err_t err = esp_timer_create(&timerArgs, &g_nesTimer);
    if (err != ESP_OK || !g_nesTimer)
    {
        Serial.printf("[OSD] esp_timer_create failed: %d\n", err);
        return -1;
    }

    err = esp_timer_start_periodic(g_nesTimer, periodUs);
    if (err != ESP_OK)
    {
        esp_timer_delete(g_nesTimer);
        g_nesTimer = nullptr;
        return -1;
    }

    Serial.printf("[OSD] Timer: %d Hz, period=%lld us\n", frequency, (long long)periodUs);
    return 0;
}

// ==================================================================
//  File I/O
// ==================================================================
extern "C" void osd_fullname(char *fullname, const char *shortname)
{
    strncpy(fullname, shortname, PATH_MAX);
    fullname[PATH_MAX - 1] = '\0';
}

extern "C" char *osd_newextension(char *string, char *ext)
{
    size_t len = strlen(string);
    if (len >= 4)
    {
        string[len - 3] = ext[1];
        string[len - 2] = ext[2];
        string[len - 1] = ext[3];
    }
    return string;
}

extern "C" int osd_makesnapname(char *filename, int len)
{
    (void)filename;
    (void)len;
    return -1;
}

// ==================================================================
//  Sound callbacks required by nofrendo
// ==================================================================
extern "C" void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = NES_AUDIO_RATE;
    info->bps = 16;
}

extern "C" void osd_setsound(void (*playfunc)(void *buffer, int size))
{
    g_apuProcess = playfunc;
    Serial.printf("[OSD] Sound callback: %p\n", (void *)playfunc);
}

// ==================================================================
//  NES Audio — I2S reconfigure
// ==================================================================
static void nes_i2s_reconfig()
{
    esp_err_t err = i2s_set_clk(
        I2S_NUM_0,
        NES_AUDIO_RATE,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_STEREO);
    if (err != ESP_OK)
    {
        Serial.printf("[Audio] i2s_set_clk failed: %d\n", err);
    }
    else
    {
        Serial.printf("[Audio] I2S: %d Hz stereo 16bit\n", NES_AUDIO_RATE);
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
}

// ==================================================================
//  NES Audio — I2S consumer task
// ==================================================================
static void nes_audio_task(void *arg)
{
    (void)arg;
    Serial.println("[Audio] Task started");

    while (g_audioTaskRun)
    {
        size_t itemSize = 0;
        void *item = xRingbufferReceive(g_audioRing, &itemSize, pdMS_TO_TICKS(20));
        if (!item)
            continue;

        // Paused: drain ring buffer silently
        if (g_paused) {
            vRingbufferReturnItem(g_audioRing, item);
            continue;
        }

        size_t written = 0;
        esp_err_t err = i2s_write(I2S_NUM_0, item, itemSize, &written, pdMS_TO_TICKS(100));

        if (err != ESP_OK || written != itemSize)
        {
            g_audioWriteFailCount++;
        }

        vRingbufferReturnItem(g_audioRing, item);
    }

    Serial.println("[Audio] Task stopped");
    g_audioTask = nullptr;
    vTaskDelete(nullptr);
}

// ==================================================================
//  NES Audio — Timer callback (produces audio independently of video)
//  ★ 这是关键：音频生产不再和推屏绑定
// ==================================================================
static void nes_audio_timer_cb(void *arg)
{
    (void)arg;

    if (!g_apuProcess || !g_audioMonoBuf || !g_audioStereoBuf || !g_audioRing)
    {
        return;
    }

    // 精确计算本次应该生产多少采样
    // 22050 / 120 = 183.75
    double exactSamples = (double)NES_AUDIO_RATE / (double)AUDIO_TIMER_HZ;

    portENTER_CRITICAL(&g_audioMux);
    g_audioFracAccum += exactSamples;
    int samples = (int)g_audioFracAccum;
    g_audioFracAccum -= (double)samples;
    portEXIT_CRITICAL(&g_audioMux);

    if (samples <= 0)
        return;
    if (samples > AUDIO_MAX_SAMPLES)
        samples = AUDIO_MAX_SAMPLES;

    // APU 生成 mono PCM
    g_apuProcess(g_audioMonoBuf, samples);

    // 音量
    float vol = (float)g_nesVolume / 21.0f;
    if (vol < 0.0f)
        vol = 0.0f;
    if (vol > 1.0f)
        vol = 1.0f;
    vol *= 0.60f;

    // mono → stereo + volume
    for (int i = 0; i < samples; i++)
    {
        int32_t s32 = (int32_t)((float)g_audioMonoBuf[i] * vol);
        if (s32 > 32767)
            s32 = 32767;
        if (s32 < -32768)
            s32 = -32768;
        int16_t s = (int16_t)s32;
        g_audioStereoBuf[i * 2] = s;
        g_audioStereoBuf[i * 2 + 1] = s;
    }

    size_t bytes = samples * 2 * sizeof(int16_t);

    // 非阻塞写入 ring buffer
    BaseType_t ok = xRingbufferSend(g_audioRing, g_audioStereoBuf, bytes, 0);

    if (ok != pdTRUE)
    {
        // 满了，丢掉最旧的数据腾出空间
        size_t oldSize = 0;
        void *oldItem = xRingbufferReceive(g_audioRing, &oldSize, 0);
        if (oldItem)
        {
            vRingbufferReturnItem(g_audioRing, oldItem);
        }

        // 重试
        ok = xRingbufferSend(g_audioRing, g_audioStereoBuf, bytes, 0);
        if (ok != pdTRUE)
        {
            g_audioDropCount++;
        }
    }
}

// ==================================================================
//  NES Audio — init
// ==================================================================
static void nes_audio_init()
{
    nes_i2s_reconfig();

    g_audioFracAccum = 0.0;
    g_audioDropCount = 0;
    g_audioWriteFailCount = 0;

    size_t monoBytes = AUDIO_MAX_SAMPLES * sizeof(int16_t);
    size_t stereoBytes = AUDIO_MAX_SAMPLES * 2 * sizeof(int16_t);

    g_audioMonoBuf = (int16_t *)heap_caps_malloc(monoBytes,
                                                 MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    g_audioStereoBuf = (int16_t *)heap_caps_malloc(stereoBytes,
                                                   MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);

    if (!g_audioMonoBuf || !g_audioStereoBuf)
    {
        Serial.println("[Audio] FATAL: buffer alloc failed");
        if (g_audioMonoBuf)
        {
            free(g_audioMonoBuf);
            g_audioMonoBuf = nullptr;
        }
        if (g_audioStereoBuf)
        {
            free(g_audioStereoBuf);
            g_audioStereoBuf = nullptr;
        }
        return;
    }

    memset(g_audioMonoBuf, 0, monoBytes);
    memset(g_audioStereoBuf, 0, stereoBytes);

    // 创建 ring buffer
    g_audioRing = xRingbufferCreate(AUDIO_RINGBUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!g_audioRing)
    {
        Serial.println("[Audio] FATAL: ring create failed");
        free(g_audioMonoBuf);
        g_audioMonoBuf = nullptr;
        free(g_audioStereoBuf);
        g_audioStereoBuf = nullptr;
        return;
    }

    // 启动 I2S consumer task
    g_audioTaskRun = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        nes_audio_task,
        "nes_audio",
        AUDIO_TASK_STACK,
        nullptr,
        AUDIO_TASK_PRIORITY,
        &g_audioTask,
        0 // core 0: 让音频独占一个核
    );

    if (ok != pdPASS)
    {
        Serial.println("[Audio] FATAL: task create failed");
        g_audioTaskRun = false;
        vRingbufferDelete(g_audioRing);
        g_audioRing = nullptr;
        free(g_audioMonoBuf);
        g_audioMonoBuf = nullptr;
        free(g_audioStereoBuf);
        g_audioStereoBuf = nullptr;
        return;
    }

    // ★ 启动独立的音频生产定时器
    // 120Hz = 每 8333us 触发一次
    esp_timer_create_args_t tArgs = {};
    tArgs.callback = nes_audio_timer_cb;
    tArgs.arg = nullptr;
    tArgs.dispatch_method = ESP_TIMER_TASK;
    tArgs.name = "audioGen";

    esp_err_t err = esp_timer_create(&tArgs, &g_audioTimer);
    if (err == ESP_OK && g_audioTimer)
    {
        int64_t periodUs = 1000000LL / AUDIO_TIMER_HZ;
        err = esp_timer_start_periodic(g_audioTimer, periodUs);
        if (err == ESP_OK)
        {
            Serial.printf("[Audio] Timer: %d Hz (%lld us)\n",
                          AUDIO_TIMER_HZ, (long long)periodUs);
        }
        else
        {
            Serial.printf("[Audio] Timer start failed: %d\n", err);
        }
    }
    else
    {
        Serial.printf("[Audio] Timer create failed: %d\n", err);
    }

    Serial.printf("[Audio] Ready: %d Hz, ring=%d bytes, timer=%d Hz\n",
                  NES_AUDIO_RATE, AUDIO_RINGBUFFER_BYTES, AUDIO_TIMER_HZ);
}

// ==================================================================
//  NES Audio — deinit
// ==================================================================
static void nes_audio_deinit()
{
    // 1) 先停 timer（阻止再产生数据）
    if (g_audioTimer) {
        esp_timer_stop(g_audioTimer);
        esp_timer_delete(g_audioTimer);
        g_audioTimer = nullptr;
    }

    // 2) 通知 task 退出并等它自己走完
    // ★ 关键：把 g_audioTask 一次性读到局部变量然后立即清空全局指针。
    // 音频 task 在 Core 0 退出时也会写 g_audioTask = NULL，
    // 如果 Core 1 多次读取 g_audioTask，可能在两次读取之间被 Core 0 置 NULL，
    // 导致 eTaskGetState(NULL) → assert 崩溃。
    g_audioTaskRun = false;
    TaskHandle_t audioTask = g_audioTask;
    g_audioTask = nullptr;  // 抢先清空，避免 Core 0 的音频 task 和 Core 1 竞争
    if (audioTask) {
        for (int i = 0; i < 100; i++) {
            if (eTaskGetState(audioTask) == eDeleted) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // 兜底：如果没有优雅退出，强杀
        if (eTaskGetState(audioTask) != eDeleted) {
            vTaskDelete(audioTask);
        }
    }

    // 3) 清 I2S DMA（驱动本身由 AudioPlayer::init() 负责卸载/重装）
    // ★ 不能在这里 i2s_driver_uninstall：因为旧的 Audio 对象析构时
    // ~Audio() → stopSong() → i2s_zero_dma_buffer() 需要 I2S 驱动仍然安装
    i2s_zero_dma_buffer(I2S_NUM_0);

    // 4) 释放 ringbuffer 和临时 buf
    if (g_audioRing) {
        vRingbufferDelete(g_audioRing);
        g_audioRing = nullptr;
    }
    if (g_audioMonoBuf) {
        heap_caps_free(g_audioMonoBuf);
        g_audioMonoBuf = nullptr;
    }
    if (g_audioStereoBuf) {
        heap_caps_free(g_audioStereoBuf);
        g_audioStereoBuf = nullptr;
    }
}

// ==================================================================
//  Config
// ==================================================================
static bool cfg_open(void) { return false; }
static void cfg_close(void) {}

static int cfg_read_int(const char *g, const char *k, int def)
{
    (void)g;
    (void)k;
    return def;
}

static const char *cfg_read_string(const char *g, const char *k, const char *def)
{
    (void)g;
    (void)k;
    return def;
}

static void cfg_write_int(const char *g, const char *k, int v)
{
    (void)g;
    (void)k;
    (void)v;
}

static void cfg_write_string(const char *g, const char *k, const char *v)
{
    (void)g;
    (void)k;
    (void)v;
}

static char cfg_filename[PATH_MAX] = "/sd/nofrendo.cfg";

extern "C" config_t config = {
    cfg_open,
    cfg_close,
    cfg_read_int,
    cfg_read_string,
    cfg_write_int,
    cfg_write_string,
    cfg_filename};

// ==================================================================
//  Log
// ==================================================================
static int logprint(const char *string)
{
    return Serial.print(string);
}

// ==================================================================
//  OSD Init / Shutdown
// ==================================================================
extern "C" int osd_init(void)
{
    nofrendo_log_chain_logfunc(logprint);
    Serial.println("[OSD] Initialized");

    if (!g_fbData)
    {
        g_fbData = (uint8_t *)ps_malloc(NES_W * NES_H);
        if (!g_fbData)
        {
            Serial.println("[OSD] FATAL: fb alloc failed!");
            return -1;
        }
    }

    return 0;
}

extern "C" void osd_shutdown(void)
{
    if (g_nesTimer)
    {
        esp_timer_stop(g_nesTimer);
        esp_timer_delete(g_nesTimer);
        g_nesTimer = nullptr;
    }
    Serial.println("[OSD] Shutdown");
}

extern "C" void osd_getvideoinfo(vidinfo_t *info)
{
    info->default_width = NES_W;
    info->default_height = NES_H;
    info->driver = &nes_vid_driver;
}

// ==================================================================
//  OSD Main
// ==================================================================
extern "C" int osd_main(int argc, char *argv[])
{
    (void)argc;
    config.filename = cfg_filename;
    return main_loop(argv[0], system_autodetect);
}

// ==================================================================
//  Setup
// ==================================================================
extern "C" void nofrendo_osd_setup(Ws_AMOLED *amoled, uint16_t w, uint16_t h)
{
    g_amoled = amoled;
    g_screenW = w;
    g_screenH = h;
    g_prevPad = 0xFFFFFFFF;
    g_osdCleanupInProgress = false;
    g_osdCleanupDone = false;
    g_quitRequested = false;
    g_paused = false;

    // ★ 整屏缓冲，PSRAM
    if (!g_rgb565Buf)
    {
        g_rgb565Buf = (uint16_t *)ps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t));
        if (!g_rgb565Buf)
        {
            Serial.println("[OSD] WARNING: RGB565 pre-alloc failed, will retry in blit");
        }
        else
        {
            buildScaleTables();
            Serial.printf("[OSD] RGB565 pre-allocated: %d bytes\n",
                          SCREEN_W * SCREEN_H * (int)sizeof(uint16_t));
        }
    }

    nes_audio_init();


    Serial.printf("[OSD] Setup: %dx%d, NES %dx%d -> %dx%d\n",
                  g_screenW, g_screenH, NES_W, NES_H, SCREEN_W, SCREEN_H);
}

// ==================================================================
//  Cleanup
// ==================================================================
extern "C" void nofrendo_osd_cleanup(void)
{
    if (g_osdCleanupInProgress || g_osdCleanupDone) {
        return;
    }
    g_osdCleanupInProgress = true;

    // 1) 停 NES 帧定时器
    if (g_nesTimer) {
        esp_timer_stop(g_nesTimer);
        esp_timer_delete(g_nesTimer);
        g_nesTimer = nullptr;
    }

    // 2) ★关键：先停音频，防止后续野指针回调
    nes_audio_deinit();

    // 3) 释放视频缓冲
    if (g_fbData) {
        heap_caps_free(g_fbData);
        g_fbData = nullptr;
    }
    if (g_rgb565Buf) {
        heap_caps_free(g_rgb565Buf);
        g_rgb565Buf = nullptr;
    }

    // 4) 清 APU 回调，避免任何残留调用
    g_apuProcess = nullptr;

    // 5) 复位杂项状态
    g_paused         = false;
    g_quitRequested  = false;
    g_padState       = 0;
    g_prevPad        = 0xFFFFFFFF;
    g_touchStartY    = -1;
    g_touchStartX    = 0;
    g_audioFracAccum = 0.0;
    g_audioDropCount = 0;
    g_audioWriteFailCount = 0;

    g_osdCleanupDone = true;
    g_osdCleanupInProgress = false;
    Serial.println("[OSD] full cleanup done");
}