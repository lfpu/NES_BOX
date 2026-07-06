#include "damage_detector.h"
#include "game_profiles.h"
#include "game_profile_store.h"
#include "hp_scan.h"

extern "C" {
#include <nes/nes.h>
#include <noftypes.h>
}
#include <Arduino.h>

volatile bool g_damageDetected = false;

static const GameProfile *s_profile = NULL;
static uint8_t            s_prevValue = 0;
static bool               s_initialized = false;
static char               s_romName[96] = {0};

/* 新增：切 ROM 时 launcher 调这个，把 ROM 文件名传下来 */
extern "C" void damageDetector_setRom(const char* romName)
{
    if (romName) {
        strncpy(s_romName, romName, sizeof(s_romName)-1);
        s_romName[sizeof(s_romName)-1] = 0;
    } else {
        s_romName[0] = 0;
    }
    gps_load();                       // 确保 SD 已加载
    hp_scan_set_rom(s_romName);       // 通知扫描器
    damageDetector_reset();
}

extern "C" void damageDetector_reset(void)
{
    s_initialized = false;
    s_profile     = NULL;
    s_prevValue   = 0;
    g_damageDetected = false;
}

extern "C" void damageDetector_update(void)
{
#ifdef HP_SCAN_ENABLE
    return;
#endif
    if (!s_initialized) {
        s_initialized = true;

        /* 1) 先看 hp_scan 是否已经学到（SD 里有） */
        uint16_t learned = hp_scan_get_learned_addr();
        static GameProfile learnedProf;
        if (learned) {
            learnedProf.nameSubstr = s_romName;
            learnedProf.healthAddr = learned;
            s_profile = &learnedProf;
        } else {
            /* 2) 再走 SD 优先 / 内置回退 的原有逻辑 */
            s_profile = findGameProfile(s_romName);
        }

        if (s_profile) {
            uint8_t *ram = nes_getcontextptr()->cpu->mem_page[0];
            if (!ram || s_profile->healthAddr >= 0x800) {
                Serial.printf("[DMG] invalid HP addr $%04X, disabling\n", s_profile->healthAddr);
                s_profile = NULL;
                s_prevValue = 0;
            } else {
                s_prevValue = ram[s_profile->healthAddr];
                Serial.printf("[DMG] armed at $%04X baseline=%u\n",
                              s_profile->healthAddr, s_prevValue);
            }
        } else {
            Serial.println("[DMG] no profile — waiting for hp_scan to learn...");
        }
        return;
    }

    /* 未知 ROM：每 60 帧回头看下 hp_scan 有没有刚学到，学到就立即启用 */
    if (!s_profile) {
        static int recheck = 0;
        if ((++recheck) >= 60) {
            recheck = 0;
            uint16_t learned = hp_scan_get_learned_addr();
            if (learned) {
                static GameProfile lp;
                lp.nameSubstr = s_romName;
                lp.healthAddr = learned;
                s_profile = &lp;
                uint8_t *ram = nes_getcontextptr()->cpu->mem_page[0];
                if (ram && learned < 0x800) {
                    s_prevValue = ram[learned];
                }
                Serial.printf("[DMG] hot-armed at $%04X\n", learned);
            }
        }
        return;
    }

    uint8_t *ram = nes_getcontextptr()->cpu->mem_page[0];
    if (!ram || s_profile->healthAddr >= 0x800) return;

    uint8_t cur = ram[s_profile->healthAddr];
    if (cur < s_prevValue) {
        uint8_t diff = s_prevValue - cur;
        if (diff <= 8) g_damageDetected = true;   /* 忽略死亡瞬间的暴降 */
    }
    s_prevValue = cur;
}