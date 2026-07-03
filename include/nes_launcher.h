#ifndef NES_LAUNCHER_H
#define NES_LAUNCHER_H

#include <Arduino.h>
#include <stdint.h>

#define NES_WIDTH  256
#define NES_HEIGHT 240

#define NES_PAD_A      0x01
#define NES_PAD_B      0x02
#define NES_PAD_SELECT 0x04
#define NES_PAD_START  0x08
#define NES_PAD_UP     0x10
#define NES_PAD_DOWN   0x20
#define NES_PAD_LEFT   0x40
#define NES_PAD_RIGHT  0x80

class Ws_AMOLED;

// Thin wrapper around arduino-nofrendo
class NESLauncher {
public:
    NESLauncher();
    ~NESLauncher();

    bool init(Ws_AMOLED* amoled, uint16_t screenW, uint16_t screenH);
    bool loadAndRun(const String& romPath);
    void stop();
    bool isRunning() const { return _running; }
    void setButtonState(uint8_t pad1State);
    uint8_t getButtonState() const { return _padState; }
    void frameUpdate();

private:
    Ws_AMOLED* _amoled;
    uint16_t _screenW, _screenH;
    volatile bool _running;
    volatile uint8_t _padState;
    TaskHandle_t _emuTaskHandle;
    String _romPath;

    static void _emuTaskFunc(void* param);
    void _emuLoop();
};

#endif // NES_LAUNCHER_H
