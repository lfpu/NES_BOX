#include "game_profile_store.h"
#include <Arduino.h>
#include <SD.h>
#include <string.h>

static GpsEntry s_cache[GPS_MAX];
static int      s_count = 0;
static bool     s_loaded = false;

/* -------- 内部工具 -------- */
static void ensureDir(const char* dir) {
    if (!SD.exists(dir)) SD.mkdir(dir);
}

static int findIndexByName(const char* name) {
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_cache[i].nameSubstr, name) == 0) return i;
    }
    return -1;
}

/* 简单的行解析：ROM|0xAAAA|hits|score|deathHits */
static bool parseLine(char* line, GpsEntry* out) {
    // 跳过空行/注释
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#' || *line == '\r' || *line == '\n') return false;

    char* p1 = strchr(line, '|'); if (!p1) return false; *p1 = 0;
    char* p2 = strchr(p1+1, '|'); if (!p2) return false; *p2 = 0;
    char* p3 = strchr(p2+1, '|'); if (!p3) return false; *p3 = 0;
    char* p4 = strchr(p3+1, '|'); if (!p4) return false; *p4 = 0;

    strncpy(out->nameSubstr, line, GPS_NAMELEN - 1);
    out->nameSubstr[GPS_NAMELEN - 1] = 0;

    out->hpAddr    = (uint16_t)strtoul(p1 + 1, NULL, 0);
    out->hits      = (uint16_t)strtoul(p2 + 1, NULL, 10);
    out->score     = (int32_t) strtol (p3 + 1, NULL, 10);
    out->deathHits = (uint16_t)strtoul(p4 + 1, NULL, 10);
    return true;
}

/* -------- 加载 -------- */
extern "C" void gps_load(void) {
    if (s_loaded) return;
    s_loaded = true;
    s_count = 0;

    ensureDir("/game");

    File f = SD.open(GPS_PATH, FILE_READ);
    if (!f) {
        Serial.printf("[GPS] no profile file, will create on first commit\n");
        return;
    }

    char buf[192];
    while (f.available() && s_count < GPS_MAX) {
        int n = f.readBytesUntil('\n', buf, sizeof(buf)-1);
        if (n <= 0) break;
        buf[n] = 0;

        GpsEntry e{};
        if (parseLine(buf, &e)) {
            s_cache[s_count++] = e;
        }
    }
    f.close();
    Serial.printf("[GPS] loaded %d entries from %s\n", s_count, GPS_PATH);
}

/* -------- 查询 -------- */
extern "C" const GpsEntry* gps_find(const char* romFilename) {
    if (!s_loaded) gps_load();
    if (!romFilename) return nullptr;

    for (int i = 0; i < s_count; i++) {
        if (strstr(romFilename, s_cache[i].nameSubstr) != nullptr) {
            Serial.printf("[GPS] hit '%s' -> $%04X (score=%ld, hits=%u)\n",
                          s_cache[i].nameSubstr,
                          s_cache[i].hpAddr,
                          (long)s_cache[i].score,
                          (unsigned)s_cache[i].hits);
            return &s_cache[i];
        }
    }
    return nullptr;
}

/* -------- 回写整表 -------- */
static bool flushToDisk(void) {
    ensureDir("/game");
    // 用临时文件写完后 rename，避免掉电写坏
    const char* tmp = "/game/game_profile.cfg.tmp";
    SD.remove(tmp);
    File f = SD.open(tmp, FILE_WRITE);
    if (!f) {
        Serial.printf("[GPS] open %s for write FAILED\n", tmp);
        return false;
    }
    f.println("# GamePad HP profile — auto-learned (do not edit while running)");
    f.println("# format: ROM_SUBSTR|HP_ADDR_HEX|HITS|SCORE|DEATH_HITS");
    for (int i = 0; i < s_count; i++) {
        f.printf("%s|0x%04X|%u|%ld|%u\n",
                 s_cache[i].nameSubstr,
                 s_cache[i].hpAddr,
                 (unsigned)s_cache[i].hits,
                 (long)s_cache[i].score,
                 (unsigned)s_cache[i].deathHits);
    }
    f.close();
    SD.remove(GPS_PATH);
    // Arduino-SD 没有 rename，只能重写
    File src = SD.open(tmp, FILE_READ);
    File dst = SD.open(GPS_PATH, FILE_WRITE);
    if (!src || !dst) { if (src) src.close(); if (dst) dst.close(); return false; }
    uint8_t buf[256];
    int r;
    while ((r = src.read(buf, sizeof(buf))) > 0) dst.write(buf, r);
    src.close(); dst.close();
    SD.remove(tmp);
    Serial.printf("[GPS] flushed %d entries to %s\n", s_count, GPS_PATH);
    return true;
}

/* -------- 提交（增/覆盖） -------- */
extern "C" bool gps_commit(const char* romName,
                           uint16_t hpAddr,
                           uint16_t hits,
                           int32_t  score,
                           uint16_t deathHits) {
    if (!s_loaded) gps_load();
    if (!romName || !*romName) return false;

    int idx = findIndexByName(romName);
    if (idx >= 0) {
        // 只在新数据"更可信"时才覆盖
        if (score > s_cache[idx].score) {
            s_cache[idx].hpAddr    = hpAddr;
            s_cache[idx].hits      = hits;
            s_cache[idx].score     = score;
            s_cache[idx].deathHits = deathHits;
            Serial.printf("[GPS] UPDATE '%s' -> $%04X score=%ld\n",
                          romName, hpAddr, (long)score);
        } else {
            Serial.printf("[GPS] keep old (score %ld >= %ld) for '%s'\n",
                          (long)s_cache[idx].score, (long)score, romName);
            return false;
        }
    } else {
        if (s_count >= GPS_MAX) {
            Serial.println("[GPS] cache full, cannot add");
            return false;
        }
        GpsEntry& e = s_cache[s_count++];
        strncpy(e.nameSubstr, romName, GPS_NAMELEN - 1);
        e.nameSubstr[GPS_NAMELEN - 1] = 0;
        e.hpAddr    = hpAddr;
        e.hits      = hits;
        e.score     = score;
        e.deathHits = deathHits;
        Serial.printf("[GPS] ADD '%s' -> $%04X score=%ld\n",
                      romName, hpAddr, (long)score);
    }
    return flushToDisk();
}

extern "C" void gps_dump(void) {
    if (!s_loaded) gps_load();
    Serial.printf("[GPS] === %d entries ===\n", s_count);
    for (int i = 0; i < s_count; i++) {
        Serial.printf("  [%d] %-40s $%04X hits=%u score=%ld deaths=%u\n",
                      i, s_cache[i].nameSubstr, s_cache[i].hpAddr,
                      s_cache[i].hits, (long)s_cache[i].score,
                      s_cache[i].deathHits);
    }
}