/*
 * damage_detector.h — NES RAM health-value monitor
 *
 * Runs on Core 1 inside scaleAndPush() (~60 fps).
 * Detects player damage by monitoring a game-specific NES RAM address.
 * When the monitored value decreases, g_damageDetected is set true.
 * Core 0 reads and clears g_damageDetected to trigger the vibration motor.
 */

#ifndef DAMAGE_DETECTOR_H
#define DAMAGE_DETECTOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inter-core flag: Core 1 sets on damage, Core 0 reads & resets
extern volatile bool g_damageDetected;

// Call once when loading a new ROM (before emulation starts)
void damageDetector_setRom(const char* romName);
// Call once per frame from scaleAndPush() on Core 1
void damageDetector_update(void);

// Reset state when loading a new ROM (call from _emuLoop before emulation starts)
void damageDetector_reset(void);

#ifdef __cplusplus
}
#endif

#endif // DAMAGE_DETECTOR_H
