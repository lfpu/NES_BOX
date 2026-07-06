/*
 * game_profile_store.h — SD-based auto-learned HP profile store
 *
 * File: /sd/game/game_profile.cfg
 * Format:
 *   # comments
 *   ROM_SUBSTR|HP_ADDR_HEX|HITS|SCORE|DEATH_HITS
 */
#ifndef GAME_PROFILE_STORE_H
#define GAME_PROFILE_STORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPS_PATH   "/game/game_profile.cfg"
#define GPS_MAX    128       // 最多缓存 128 条
#define GPS_NAMELEN 96

typedef struct {
    char     nameSubstr[GPS_NAMELEN];
    uint16_t hpAddr;
    uint16_t hits;
    int32_t  score;
    uint16_t deathHits;
} GpsEntry;

/* 启动时加载一次（damage_detector_reset 里调用） */
void  gps_load(void);

/* 查询：romFilename 匹配到的第一条 entry；找不到返回 NULL */
const GpsEntry* gps_find(const char* romFilename);

/*
 * 由 hp_scan 提交一次学习结果，内部：
 *   - 匹配同名 entry，若新 score 更高则覆盖
 *   - 否则追加
 *   - 立即回写 SD
 * 线程注意：只从 Core 1 (scaleAndPush) 调用。
 */
bool  gps_commit(const char* romName,
                 uint16_t hpAddr,
                 uint16_t hits,
                 int32_t  score,
                 uint16_t deathHits);

/* 调试：串口打印当前缓存 */
void  gps_dump(void);

#ifdef __cplusplus
}
#endif
#endif