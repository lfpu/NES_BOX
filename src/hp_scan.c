/*
 * hp_scan.c — NES RAM HP address auto-finder with death rollback + SD persistence
 *
 * 工作流：
 *   1. 每帧扫描 $0000-$07FF：
 *      - 稳定 +1，合法掉血 +15，非法跳变/越界减分或拉黑
 *   2. 检测"死亡事件"：本帧有 >=100 字节从非零 → 零，视为死亡/切场景
 *      - 回溯最近 30 帧，若某候选在死前一帧刚变 0 → 死亡命中 +50
 *   3. 自动裁决：TOP1 满足 (hits>=5 && death>=1 && TOP1.score>=TOP2.score*2)
 *      → 调用 gps_commit() 写入 SD /sd/game/game_profile.cfg，锁定不再学习
 */
#include "hp_scan.h"
#include "game_profile_store.h"
#include <stdint.h>
#include <string.h>
#include <nes/nes.h>

#ifdef HP_SCAN_DEBUG
extern int ets_printf(const char *fmt, ...);
#define HP_SCAN_LOG(...) ets_printf(__VA_ARGS__)
#else
#define HP_SCAN_LOG(...) ((void)0)
#endif

/* ================= 调参 ================= */
#define RAM_SIZE          0x800
#define HP_MIN_VALUE      0
#define HP_MAX_VALUE      99
#define HP_MAX_STEP       8
#define REPORT_INTERVAL   300
#define TOP_N             5
#define WARMUP_FRAMES     60

#define W_HIT             15
#define W_STABLE           1
#define W_STABLE_CAP     200
#define P_BAD_JUMP        30
#define P_OUT_OF_RANGE    50

/* 死亡事件 */
#define DEATH_ZERO_THRESH 100    /* 一帧内 >=100 字节归零视为死亡 */
#define DEATH_ROLLBACK    30     /* 回溯窗口帧数 */
#define W_DEATH           50     /* 命中一次死亡回溯 +50 */

/* 自动落盘阈值 */
#define COMMIT_MIN_HITS    5
#define COMMIT_MIN_DEATH   1
#define COMMIT_LEAD_RATIO  2     /* TOP1.score >= TOP2.score * 2 */

/* ================= 状态 ================= */
typedef struct {
    uint8_t  last_val;
    uint8_t  blacklisted;
    uint16_t stable_cnt;
    uint16_t hit_cnt;
    uint16_t death_hits;
    int32_t  score;
} cand_t;

static cand_t   s_cand[RAM_SIZE];
static uint32_t s_frame = 0;
static int      s_inited = 0;

/* 环形快照：用于死亡回溯 */
static uint8_t  s_ringbuf[DEATH_ROLLBACK][RAM_SIZE];
static uint8_t  s_ringHead = 0;
static uint8_t  s_ringFilled = 0;

static char     s_rom[96] = {0};
static bool     s_committed = false;
static uint16_t s_learnedAddr = 0;

/* -------- 外部接口 -------- */
void hp_scan_set_rom(const char* romName) {
    if (!romName) { s_rom[0] = 0; return; }
    strncpy(s_rom, romName, sizeof(s_rom) - 1);
    s_rom[sizeof(s_rom) - 1] = 0;
    HP_SCAN_LOG("[HP_SCAN] rom set to '%s'\n", s_rom);

    /* 如果 SD 已经学过 → 直接锁定，不再学习 */
    const GpsEntry* e = gps_find(s_rom);
    if (e) {
        s_committed   = true;
        s_learnedAddr = e->hpAddr;
        HP_SCAN_LOG("[HP_SCAN] already learned: $%04X, skip scan\n", e->hpAddr);
    } else {
        s_committed   = false;
        s_learnedAddr = 0;
    }
}

uint16_t hp_scan_get_learned_addr(void) { return s_learnedAddr; }

void hp_scan_reset(void) {
    s_inited     = 0;
    s_frame      = 0;
    s_ringHead   = 0;
    s_ringFilled = 0;
    s_committed  = false;
    s_learnedAddr= 0;
}

/* -------- Top-N + 自动裁决 -------- */
static void evaluate_top_and_maybe_commit(void)
{
    int  top_idx[TOP_N];
    int32_t top_score[TOP_N];
    for (int i = 0; i < TOP_N; i++) { top_idx[i] = -1; top_score[i] = -0x7fffffff; }

    for (int a = 0; a < RAM_SIZE; a++) {
        if (s_cand[a].blacklisted) continue;
        if (s_cand[a].hit_cnt == 0) continue;
        int32_t s = s_cand[a].score;
        for (int k = 0; k < TOP_N; k++) {
            if (s > top_score[k]) {
                for (int j = TOP_N - 1; j > k; j--) {
                    top_score[j] = top_score[j-1];
                    top_idx[j]   = top_idx[j-1];
                }
                top_score[k] = s;
                top_idx[k]   = a;
                break;
            }
        }
    }

    HP_SCAN_LOG("\n[HP_SCAN] ===== frame %u  Top %d =====\n",
               (unsigned)s_frame, TOP_N);
    for (int k = 0; k < TOP_N; k++) {
        if (top_idx[k] < 0) break;
        int a = top_idx[k];
        HP_SCAN_LOG("[HP_SCAN] TOP%d $%04X  score=%ld hits=%u deaths=%u cur=%u\n",
                   k + 1, a,
                   (long)s_cand[a].score,
                   (unsigned)s_cand[a].hit_cnt,
                   (unsigned)s_cand[a].death_hits,
                   (unsigned)s_cand[a].last_val);
    }

    /* --- 自动裁决 --- */
    if (s_committed) return;
    if (top_idx[0] < 0) return;
    int a1 = top_idx[0];
    int32_t s1 = top_score[0];
    int32_t s2 = (top_idx[1] >= 0) ? top_score[1] : 0;
    if (s2 <= 0) s2 = 1;

    if (s_cand[a1].hit_cnt     >= COMMIT_MIN_HITS  &&
        s_cand[a1].death_hits  >= COMMIT_MIN_DEATH &&
        s1 >= s2 * COMMIT_LEAD_RATIO) {

        HP_SCAN_LOG("[HP_SCAN] *** LEARNED: $%04X for '%s' ***\n", a1, s_rom);
        if (s_rom[0]) {
            gps_commit(s_rom, (uint16_t)a1,
                       s_cand[a1].hit_cnt,
                       s_cand[a1].score,
                       s_cand[a1].death_hits);
            s_committed   = true;
            s_learnedAddr = (uint16_t)a1;
        }
    }
}

/* -------- 死亡事件回溯 -------- */
static void handle_death_event(const uint8_t* ram_now)
{
    if (s_ringFilled == 0) return;
    /* 找回溯窗口内最早的一帧 */
    uint8_t oldest = s_ringHead;   /* head 位置是最老的 */
    const uint8_t* ram_old = s_ringbuf[oldest];

    int rewarded = 0;
    for (int a = 0; a < RAM_SIZE; a++) {
        if (s_cand[a].blacklisted) continue;
        if (s_cand[a].hit_cnt == 0) continue;   /* 至少见过掉血 */
        /* 死亡前 = 有正值；死亡后 = 0 */
        if (ram_old[a] > 0 && ram_old[a] <= HP_MAX_VALUE && ram_now[a] == 0) {
            s_cand[a].death_hits++;
            s_cand[a].score += W_DEATH;
            rewarded++;
            if (rewarded <= 8) {
                HP_SCAN_LOG("[HP_SCAN] DEATH  $%04X  %u -> 0  (deaths=%u score=%ld)\n",
                           a, ram_old[a],
                           (unsigned)s_cand[a].death_hits,
                           (long)s_cand[a].score);
            }
        }
    }
    if (rewarded > 8) HP_SCAN_LOG("[HP_SCAN] ...+%d more death hits\n", rewarded - 8);
}

/* -------- 每帧调用 -------- */
void hp_scan_update(void)
{
    /* 已锁定：不再耗 CPU */
    if (s_committed) return;

    nes_t *ctx = nes_getcontextptr();
    if (!ctx || !ctx->cpu || !ctx->cpu->mem_page || !ctx->cpu->mem_page[0]) {
        return;
    }

    uint8_t *ram = ctx->cpu->mem_page[0];
    if (!ram) return;

    if (!s_inited) {
        for (int a = 0; a < RAM_SIZE; a++) {
            s_cand[a].last_val    = ram[a];
            s_cand[a].blacklisted = 0;
            s_cand[a].stable_cnt  = 0;
            s_cand[a].hit_cnt     = 0;
            s_cand[a].death_hits  = 0;
            s_cand[a].score       = 0;
        }
        s_inited     = 1;
        s_frame      = 0;
        s_ringHead   = 0;
        s_ringFilled = 0;
        HP_SCAN_LOG("[HP_SCAN] baseline captured, play & take damage / die...\n");
        return;
    }

    s_frame++;

    /* 1. 逐字节评估 */
    int zeroTransitions = 0;    /* 本帧从非零变零的字节数 */
    for (int a = 0; a < RAM_SIZE; a++) {
        uint8_t cur  = ram[a];
        uint8_t prev = s_cand[a].last_val;

        if (prev != 0 && cur == 0) zeroTransitions++;

        if (s_cand[a].blacklisted) { s_cand[a].last_val = cur; continue; }

        if (s_frame < WARMUP_FRAMES) {
            s_cand[a].last_val = cur;
            continue;
        }

        if (cur > HP_MAX_VALUE) {
            s_cand[a].score      -= P_OUT_OF_RANGE;
            s_cand[a].blacklisted = 1;
            s_cand[a].last_val    = cur;
            continue;
        }

        if (cur == prev) {
            if (s_cand[a].stable_cnt < W_STABLE_CAP) {
                s_cand[a].stable_cnt++;
                s_cand[a].score += W_STABLE;
            }
        } else if (cur < prev) {
            uint8_t diff = prev - cur;
            if (diff <= HP_MAX_STEP) {
                s_cand[a].hit_cnt++;
                s_cand[a].score += W_HIT;
            } else {
                s_cand[a].score -= P_BAD_JUMP;
                if (diff > 32) s_cand[a].blacklisted = 1;
            }
            s_cand[a].stable_cnt = 0;
        } else {
            /* 上升 */
            uint8_t diff = cur - prev;
            if (diff == 1 && s_cand[a].stable_cnt < 3) {
                s_cand[a].score -= P_BAD_JUMP;
                s_cand[a].blacklisted = 1;   /* 高频 +1 → 计时器 */
            }
            s_cand[a].stable_cnt = 0;
        }
        s_cand[a].last_val = cur;
    }

    /* 2. 死亡事件检测 */
    if (zeroTransitions >= DEATH_ZERO_THRESH && s_frame > WARMUP_FRAMES + DEATH_ROLLBACK) {
        HP_SCAN_LOG("[HP_SCAN] *** DEATH detected (%d bytes zeroed) — rollback %d frames\n",
                   zeroTransitions, (int)s_ringFilled);
        handle_death_event(ram);
        /* 死亡后先跳过一段避免过场动画噪声：清空 ring buffer */
        s_ringFilled = 0;
        s_ringHead   = 0;
    }

    /* 3. 更新环形快照 */
    memcpy(s_ringbuf[s_ringHead], ram, RAM_SIZE);
    s_ringHead = (s_ringHead + 1) % DEATH_ROLLBACK;
    if (s_ringFilled < DEATH_ROLLBACK) s_ringFilled++;

    /* 4. 定期打分 + 尝试落盘 */
    if ((s_frame % REPORT_INTERVAL) == 0) {
        evaluate_top_and_maybe_commit();
    }
}