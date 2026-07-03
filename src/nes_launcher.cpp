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

    BaseType_t ret = xTaskCreatePinnedToCore(
        _emuTaskFunc,
        "nes_emu",
        32768,        // 32KB stack
        this,
        5,            // priority
        &_emuTaskHandle,
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
    if (!_running) {
        // nofrendo already exited (e.g., via pause menu quit)
        // Still need to clean up OSD resources
        nofrendo_osd_cleanup();
        return;
    }

    Serial.println("[NES] Stopping...");

    // Signal nofrendo to quit via the event system
    g_quitRequested = true;
    _running = false;

    // Wait for emulation task to finish (up to 3 seconds)
    if (_emuTaskHandle) {
        for (int i = 0; i < 150; i++) {
            if (eTaskGetState(_emuTaskHandle) == eDeleted ||
                eTaskGetState(_emuTaskHandle) == eReady) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        _emuTaskHandle = nullptr;
    }

    nofrendo_osd_cleanup();
    Serial.println("[NES] Stopped");
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

    // Reset damage detector for the new game session
    damageDetector_reset();

    // Build argv for nofrendo_main
    char romPathCStr[512];
    strncpy(romPathCStr, _romPath.c_str(), sizeof(romPathCStr) - 1);
    romPathCStr[sizeof(romPathCStr) - 1] = '\0';

    char* argv[1];
    argv[0] = romPathCStr;

    int ret = nofrendo_main(1, argv);

    Serial.printf("[NES] nofrendo_main returned %d\n", ret);
    _running = false;
}
