#include "nes_launcher.h"
#include "damage_detector.h"
#include "Ws_AMOLED.h"

// ===================== OSD externs (from nofrendo_osd.cpp) =====================
extern "C" {
#include <vid_drv.h>
    void nofrendo_osd_setup(Ws_AMOLED*, uint16_t, uint16_t);
    void nofrendo_osd_cleanup();
    int  nofrendo_main(int argc, char *argv[]);
}

// Global pad state — read by osd_getinput() in nofrendo_osd.cpp
extern volatile uint8_t g_padState;

// Quit signal — set by stop(), checked by osd_getinput()
extern volatile bool g_quitRequested;

// ===================== Constructor / Destructor =====================
NESLauncher::NESLauncher()
    : _amoled(nullptr), _screenW(600), _screenH(450)
    , _running(false), _padState(0), _emuTaskHandle(nullptr)
{
}

NESLauncher::~NESLauncher()
{
    stop();
}

// ===================== Init — set up OSD globals =====================
bool NESLauncher::init(Ws_AMOLED* amoled, uint16_t screenW, uint16_t screenH)
{
    _amoled  = amoled;
    _screenW = screenW;
    _screenH = screenH;

    nofrendo_osd_setup(amoled, screenW, screenH);

    Serial.printf("[NES] Init OK: screen %dx%d\n", screenW, screenH);
    return true;
}

// ===================== Load ROM and start emulation =====================
bool NESLauncher::loadAndRun(const String& romPath)
{
    if (_running) {
        Serial.println("[NES] Already running!");
        return false;
    }

    _romPath  = romPath;
    _running  = true;
    _padState = 0;
    g_padState = 0;
    g_quitRequested = false;

    const char* full = _romPath.c_str();
    const char* base = strrchr(full, '/');
    base = base ? (base + 1) : full;
    const char* base2 = strrchr(base, '\\');
    if (base2) base = base2 + 1;
    damageDetector_setRom(base);

    BaseType_t ret = xTaskCreatePinnedToCore(
        _emuTaskFunc,
        "nes_emu",
        32768,        // 32KB stack
        this,
        5,            // priority
        (TaskHandle_t*)&_emuTaskHandle,
        1             // Core 1
    );

    if (ret != pdPASS) {
        Serial.println("[NES] Failed to create emu task!");
        _running = false;
        return false;
    }

    return true;
}

// ===================== Stop =====================
void NESLauncher::stop()
{
    if (!_running && !_emuTaskHandle) {
        return;
    }

    Serial.println("[NES] Stopping...");

    // Signal nofrendo to quit via the event system.
    // The emulator should exit on its own; avoid hard-killing the task.
    g_quitRequested = true;
    _running = false;

    // ★ 一次读到局部变量然后立即清空，防止 _emuLoop() 在 Core 1 置 NULL 后
    // eTaskGetState 收到 NULL 指针 → assert 崩溃
    TaskHandle_t emuTask = _emuTaskHandle;
    _emuTaskHandle = nullptr;

    // Wait for the emulation task to finish gracefully.
    if (emuTask) {
        for (int i = 0; i < 300; i++) {
            eTaskState state = eTaskGetState(emuTask);
            if (state == eDeleted || state == eReady) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        eTaskState state = eTaskGetState(emuTask);
        if (state == eDeleted || state == eReady) {
            // emuTask was our local copy; _emuTaskHandle already cleared above
        } else {
            Serial.println("[NES] emu task did not exit gracefully within timeout");
        }
    }

    Serial.println("[NES] Stop request issued");
}

// ===================== Button state =====================
void NESLauncher::setButtonState(uint8_t pad1State)
{
    _padState = pad1State;
    g_padState = pad1State;
}

// uint8_t NESLauncher::getButtonState() const
// {
//     return _padState;
// }

// ===================== Frame update (no-op; nofrendo manages its own timing) =====================
void NESLauncher::frameUpdate()
{
}

// ===================== FreeRTOS task entry =====================
void NESLauncher::_emuTaskFunc(void* param)
{
    NESLauncher* self = (NESLauncher*)param;
    self->_emuLoop();
    vTaskDelete(NULL);
}

// ===================== Emulation loop =====================
void NESLauncher::_emuLoop()
{
    Serial.printf("[NES] Emulation started on core %d\n", xPortGetCoreID());
    Serial.printf("[NES] ROM: %s\n", _romPath.c_str());

    const char* full = _romPath.c_str();
    const char* base = strrchr(full, '/');
    base = base ? (base + 1) : full;
    const char* base2 = strrchr(base, '\\');
    if (base2) base = base2 + 1;
    Serial.printf("[NES] ROM basename: %s\n", base);

    char romPathCStr[512];
    strncpy(romPathCStr, _romPath.c_str(), sizeof(romPathCStr) - 1);
    romPathCStr[sizeof(romPathCStr) - 1] = '\0';
    char* argv[1] = { romPathCStr };

    int ret = nofrendo_main(1, argv);
    Serial.printf("[NES] nofrendo_main returned %d\n", ret);

    // ★ 从游戏 EXIT 回来时，第一时间清理
    nofrendo_osd_cleanup();

    _running = false;
    _emuTaskHandle = nullptr;   // 清掉悬空句柄
}