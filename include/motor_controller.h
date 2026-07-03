/*
 * motor_controller.h — Vibration motor driver (TCA9554 I2C expander, bit 7)
 *
 * Runs on Core 1 inside scaleAndPush() (~60 fps).
 * Frame-based timing: 11 frames ≈ 180ms pulse.
 *
 * Pulse behavior:
 *   - On damage: TCA9554 bit 7 = HIGH for 11 frames (~180ms)
 *   - Rapid hits: frame counter resets (motor stays on continuously)
 */

#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Pulse duration in frames (60 fps × 180ms ≈ 11)
#define MOTO_PULSE_FRAMES  11

// Initialize motor (called once from setup())
void motorController_init(void);

// Update motor state (called every frame from scaleAndPush on Core 1)
void motorController_update(void);

#ifdef __cplusplus
}
#endif

#endif // MOTOR_CONTROLLER_H
