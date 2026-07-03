/*
 * hp_scan.c — NES RAM health-address scanner (debug tool)
 *
 * Scans ALL NES RAM ($0000-$07FF) every frame for bytes that decrease.
 * Prints candidate addresses to serial via ets_printf (works from Core 1).
 *
 * Usage:
 *   1. Enable in platformio.ini:  -DHP_SCAN_ENABLE
 *   2. Build & flash
 *   3. Start a game, intentionally take damage
 *   4. Watch serial output for addresses like:  [HP_SCAN] $0050: 3 -> 2
 *   5. Add confirmed address to game_profiles.h
 *   6. Disable HP_SCAN_ENABLE or leave it — no overhead when off
 *
 * Tuning: adjust MIN_VALUE / MAX_VALUE / PRINT_LIMIT below
 */

#include <stdint.h>
#include <string.h>

#include <nes/nes.h>

extern int ets_printf(const char *fmt, ...);

/* ---- tunable ---- */
#define MIN_VALUE 0
#define MAX_VALUE 50
#define PRINT_LIMIT 10

/* ---- state ---- */
static uint8_t prev_ram[0x800];
static int initialized = 0;

/* =================================================================== */
void hp_scan_update(void)
{
    uint8_t *ram = nes_getcontextptr()->cpu->mem_page[0];
    if (!ram)
        return;

    if (!initialized)
    {
        memcpy(prev_ram, ram, 0x800);
        initialized = 1;
        ets_printf("[HP_SCAN] Baseline captured (0x800 bytes)\n");
        return;
    }

    int printed = 0;
    ets_printf("===============================\n");
    for (int i = 0; i < 0x800; i++)
    {
        uint8_t oldv = prev_ram[i];
        uint8_t newv = ram[i];

        if (newv < oldv)
        {
            if (newv >= MIN_VALUE && newv <= MAX_VALUE)
            {
                ets_printf("[HP_SCAN] $%04X: %d -> %d\n", i, oldv, newv);
                printed++;
                if (printed >= PRINT_LIMIT)
                    break;
            }
        }
    }

    memcpy(prev_ram, ram, 0x800);
}
