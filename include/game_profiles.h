#ifndef GAME_PROFILES_H
#define GAME_PROFILES_H

#include <stdint.h>
#include <string.h>
#include <Print.h>
#include <Arduino.h>
#include "game_profile_store.h"

typedef struct
{
    const char *nameSubstr;
    uint16_t healthAddr;
} GameProfile;

/* 内置回退表（原样保留） */
static const GameProfile gameProfiles[] = {
    {"Super Mario USA (Japan)", 0x075A},
    {"Contra (U) ",              0x0032},
    {"Mega Man",                 0x006A},
    {"Mega Man 2",               0x006A},
    {"Mega Man 3",               0x006A},
    {"Mega Man 4",               0x006A},
    {"Mega Man 5",               0x006A},
    {"Mega Man 6",               0x006A},
    {"Castlevania",              0x0006},
    {"Ninja Gaiden",             0x0065},
    {"Duck Tales",               0x0B5A},
    {"Battletoads",              0x0057},
    {"Ghosts 'n Goblins",        0x0050},
    {"Double Dragon III - The Sacred Stones (USA)", 0x0018},
    {"Bubble Bobble",            0x001C},
    {"Batman",                   0x00B7},
    {"Chip  n Dale Rescue Rangers (U) ",  0x0570},
    {"Chip  n Dale Rescue Rangers 2 (U) ",0x0570},
    {"Toki (U) ",                0x00B7},
    {"Little Nemo - The Dream Master (U) ", 0x00B7},
    {"Adventure Island",         0x00B7},
    {"Adventure Island II",      0x00B7},
    {"Adventure Island III",     0x00B7},
    {NULL, 0}
};

/* 静态转发结果 —— 用于返回 SD 命中的地址 */
static GameProfile s_sdProfileHolder;

static inline const GameProfile *findGameProfile(const char *romFilename)
{
    if (!romFilename) return NULL;

    /* 1) 先查 SD 卡自学表 */
    const GpsEntry* e = gps_find(romFilename);
    if (e) {
        s_sdProfileHolder.nameSubstr = e->nameSubstr;
        s_sdProfileHolder.healthAddr = e->hpAddr;
        Serial.print("[OSD] Profile(SD): ");
        Serial.print(e->nameSubstr);
        Serial.print(" -> 0x");
        Serial.println(e->hpAddr, HEX);
        return &s_sdProfileHolder;
    }

    /* 2) 回退到内置表 */
    for (int i = 0; gameProfiles[i].nameSubstr != NULL; i++) {
        if (strstr(romFilename, gameProfiles[i].nameSubstr) != NULL) {
            Serial.print("[OSD] Profile(builtin): ");
            Serial.print(gameProfiles[i].nameSubstr);
            Serial.print(" -> 0x");
            Serial.println(gameProfiles[i].healthAddr, HEX);
            return &gameProfiles[i];
        }
    }
    return NULL;
}

#endif