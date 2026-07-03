/*
 * game_profiles.h — Game health-value address database for damage detection
 *
 * Each entry maps a ROM filename substring to the NES RAM address ($0000-$07FF)
 * where the player's health/lives value is stored.
 *
 * Address sources: Data Crystal (datacrystal.tcrf.net), RomDetectives Wiki,
 *                  Game Genie code analysis, hp_scan.c field testing.
 *
 * Adding a new game:
 *   1. Find the health address using hp_scan.c or FCEUX memory viewer
 *   2. Add an entry: { "Name Substr", 0xADDR },
 *   3. The substring is matched case-sensitively against the ROM filename
 *
 * Sentinel: { NULL, 0 } must remain last
 */

#ifndef GAME_PROFILES_H
#define GAME_PROFILES_H

#include <stdint.h>
#include <string.h>
#include <Print.h>
#include <Arduino.h>

typedef struct
{
    const char *nameSubstr; // ROM filename substring (case-sensitive match)
    uint16_t healthAddr;    // NES RAM address $0000-$07FF
} GameProfile;

// Database — must be defined BEFORE findGameProfile()
static const GameProfile gameProfiles[] = {
    // === Platformers ===
    {"Super Mario USA (Japan)", 0x075A},                     // Lives (decrements on death)
    {"Contra (U) ", 0x0032},                                 // Lives
    {"Mega Man", 0x006A},                                    // Health bar (0-28, per-hit)
    {"Mega Man 2", 0x006A},                                  // Health bar
    {"Mega Man 3", 0x006A},                                  // Health bar
    {"Mega Man 4", 0x006A},                                  // Health bar
    {"Mega Man 5", 0x006A},                                  // Health bar
    {"Mega Man 6", 0x006A},                                  // Health bar
    {"Castlevania", 0x0006},                                 // Health (0x00-0x10)
    {"Ninja Gaiden", 0x0065},                                // HP (0-16)
    {"Duck Tales", 0x0B5A},                                  // Health
    {"Battletoads", 0x0057},                                 // Lives (0-3)
    {"Ghosts 'n Goblins", 0x0050},                           // Health / lives
    {"Double Dragon III - The Sacred Stones (USA)", 0x0018}, // Lives
    {"Bubble Bobble", 0x001C},                               // HP
    {"Batman", 0x00B7},                                      // Health (0-8)
    {"Chip  n Dale Rescue Rangers (U) ",0x0570}, // Health
    {"Chip  n Dale Rescue Rangers 2 (U) ",0x0570},           // Health
    {"Toki (U) ", 0x00B7},                                   // Health
    {"Little Nemo - The Dream Master (U) ", 0x00B7},        // Health
    {"Adventure Island", 0x00B7},                            // Health
    {"Adventure Island II", 0x00B7},                         // Health
    {"Adventure Island III", 0x00B7},                        // Health

    // === Action / Adventure ===
    {"Zelda II - The Adventure of Link (USA)", 0x00F4},                        // Hearts × 2 (0-20)
    {"Metroid", 0x0C46},                                                       // Energy (0-190)
    {"Punch-Out!!", 0x004A},                                                   // Health
    {"Teenage Mutant Ninja Turtles III - The Manhattan Project (U) ", 0x0024}, // Health
    {"Kirby's Adventure (USA)", 0x0597},                                       // Health (current, count by 8)
    {"Shadow of the Ninja (U) ", 0x0018},                                      // Health
    {"Kid Icarus", 0x00A6},                                                    // Health
    {"Ice Climber", 0x0020},                                                   // Lives (P1)

    // === Shooters ===
    {"Gradius", 0x0020},      // Lives
    {"Life Force", 0x006A},   // Lives
    {"Star Wars (U) ",0x0729},  // Health

    // === RPG ===
    {"Final Fantasy", 0x001D}, // HP in battle (single character)
    {NULL, 0} // Sentinel — must be last
};

static inline const GameProfile *findGameProfile(const char *romFilename)
{
    if (!romFilename)
        return NULL;
    for (int i = 0; gameProfiles[i].nameSubstr != NULL; i++)
    {
        if (strstr(romFilename, gameProfiles[i].nameSubstr) != NULL)
        {
            Serial.println("[OSD] Found game profile: " + String(gameProfiles[i].nameSubstr) +
                           " -> healthAddr=0x" + String(gameProfiles[i].healthAddr, HEX));
            return &gameProfiles[i];
        }
    }
    return NULL;
}

#endif // GAME_PROFILES_H
