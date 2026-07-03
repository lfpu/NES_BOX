/*
 * motor_controller.cpp — Vibration motor driver (Core 1, frame-based)
 *
 * Runs on Core 1 inside scaleAndPush() at ~60 fps.
 * Timing is frame-counted: 11 frames ≈ 180ms.
 *
 * Hardware: TCA9554 I2C GPIO expander at address 0x20, bit 7 (MOTO_PIN_EXIO).
 *           EXIO_Initialize() must be called once from setup() on Core 0.
 */

#include "motor_controller.h"
#include "damage_detector.h"
#include "config.h"
#include "TCA9554EXIO.h"

static int  s_pulseFrames = 0;   // remaining frames to keep motor ON
static bool s_motorActive = false;

// ===================================================================
void motorController_init(void)
{
    EXIO_Initialize();                           // I2C + TCA9554 setup
    exio_clear_bit(MOTO_PIN_EXIO);               // Motor OFF (LOW)
}

// ===================================================================
// Called every frame (~60 fps) from scaleAndPush() on Core 1
void motorController_update(void)
{
    // --- Count down pulse ---
    if (s_motorActive) {
        s_pulseFrames--;
        if (s_pulseFrames <= 0) {
            exio_clear_bit(MOTO_PIN_EXIO);       // Motor OFF
            s_motorActive = false;
        }
    }

    // --- Start new pulse on damage ---
    if (!s_motorActive && g_damageDetected) {
        exio_set_bit(MOTO_PIN_EXIO);             // Motor ON
        s_pulseFrames = MOTO_PULSE_FRAMES;
        s_motorActive = true;
        g_damageDetected = false;
    }

    // --- Re-trigger: extend pulse on consecutive hits ---
    if (s_motorActive && g_damageDetected) {
        s_pulseFrames = MOTO_PULSE_FRAMES;       // Reset counter
        g_damageDetected = false;
    }
}
