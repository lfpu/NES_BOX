/*
 * damage_detector.cpp — Per-frame NES RAM health-value monitor (Core 1)
 *
 * Called from scaleAndPush() every frame (~60 fps).
 * On the first frame: matches ROM filename against game_profiles.h database,
 *   records the current health value as baseline.
 * On subsequent frames: compares current value against previous; if it decreased,
 *   sets g_damageDetected = true (Core 0 reads this to pulse the motor).
 */

#include "damage_detector.h"
#include "game_profiles.h"

extern "C" {
#include <nes/nes.h>       // nes_getcontextptr(), nes_t
#include <noftypes.h>
}

#include <Arduino.h>

// === Inter-core flag: set by Core 1, read & cleared by Core 0 ===
volatile bool g_damageDetected = false;

// === Per-session state (Core 1 only) ===
static const GameProfile *s_profile = NULL;
static uint8_t            s_prevValue = 0;
static bool               s_initialized = false;

// ===================================================================
void damageDetector_reset(void)
{
    g_damageDetected = false;
    s_profile = NULL;
    s_prevValue = 0;
    s_initialized = false;
}

// ===================================================================
void damageDetector_update(void)
{
    // --- First-call initialization ---
    if (!s_initialized) {
        s_initialized = true;

        nes_t *ctx = nes_getcontextptr();
        if (!ctx || !ctx->rominfo || !ctx->rominfo->filename[0]) {
            // No ROM loaded yet or ROM info unavailable
            return;
        }

        // Extract basename from full path (strip directory)
        const char *fname = ctx->rominfo->filename;
        const char *sep   = strrchr(fname, '/');
        if (!sep) sep = strrchr(fname, '\\');
        const char *base  = sep ? sep + 1 : fname;

        s_profile = findGameProfile(base);
        if (s_profile) {
            s_prevValue = ctx->cpu->mem_page[0][s_profile->healthAddr];
            // Serial.printf("[DAMAGE] Game: %s  addr=$%04X  initial=%d\n",
            //               s_profile->nameSubstr, s_profile->healthAddr, s_prevValue);
        }
        return;  // Skip detection this frame (no baseline yet)
    }

    // --- Per-frame detection ---
    if (!s_profile) return;   // No matching profile

    nes_t *ctx = nes_getcontextptr();
    if (!ctx || !ctx->cpu || !ctx->cpu->mem_page[0]) return;

    uint8_t currentValue = ctx->cpu->mem_page[0][s_profile->healthAddr];

    // Damage = value decreased AND previous was non-zero
    // (prevents false trigger when value transitions 0→0 on first read)
    bool damageThisFrame = (currentValue < s_prevValue && s_prevValue > 0);
    if (damageThisFrame) {
        g_damageDetected = true;
        // Print BEFORE updating s_prevValue so prev shows the real old value
        Serial.printf("[DAMAGE] addr=$%04X  %d -> %d  damage=YES\n",
                      s_profile->healthAddr, s_prevValue, currentValue);
    }

    s_prevValue = currentValue;
}
