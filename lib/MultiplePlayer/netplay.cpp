
#include "netplay.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_crc.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

extern "C" {
#include "noftypes.h"          // ← 新增，必须在 nesinput.h 之前
#include "event.h"
#include "nes/nesinput.h"
}

// Use enum names to avoid hardcoded indices (order: A,B,SEL,START,U,D,L,R)
// Matches NES_PAD_* / INP_PAD_* bit order
static const int evt_p1[8] = {
    event_joypad1_a, event_joypad1_b,
    event_joypad1_select, event_joypad1_start,
    event_joypad1_up, event_joypad1_down,
    event_joypad1_left, event_joypad1_right
};
static const int evt_p2[8] = {
    event_joypad2_a, event_joypad2_b,
    event_joypad2_select, event_joypad2_start,
    event_joypad2_up, event_joypad2_down,
    event_joypad2_left, event_joypad2_right
};

#define PKT_MAGIC 0x4E50  // 'NP'

enum : uint8_t {
    PKT_HELLO     = 0x01,
    PKT_HELLO_ACK = 0x02,
    PKT_INPUT     = 0x10,
    PKT_RESET     = 0x20,
    PKT_SYNC_CRC  = 0x30,
};

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  type;
    uint8_t  rsv;
    uint32_t frame_id;
    uint8_t  pad;
    uint8_t  pad_rsv[3];
    uint32_t crc;
} np_pkt_t;

// ---- 全局状态 ----
static netplay_state_t s_state = NP_IDLE;
static netplay_role_t  s_role  = NP_ROLE_NONE;
static uint8_t  s_peer_mac[6];
static uint8_t  s_my_mac[6];
static uint32_t s_frame = 0;
static volatile bool s_reset_pending = false;
static uint32_t s_reset_at = 0;
static volatile bool s_abort = false;

static uint8_t s_remote_buf[NETPLAY_BUF_SIZE];
static bool    s_remote_valid[NETPLAY_BUF_SIZE];
static uint8_t s_local_buf[NETPLAY_BUF_SIZE];
static SemaphoreHandle_t s_sem = nullptr;

static const void *s_sync_ram = nullptr;
static uint32_t s_sync_size = 0;
static uint32_t s_remote_crc = 0;
static bool s_remote_crc_ready = false;

static bool s_initialized = false;

static const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static inline uint8_t idx_of(uint32_t f) { return f % NETPLAY_BUF_SIZE; }
static int mac_cmp(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6); }

static void send_to(const uint8_t *mac, const np_pkt_t *p) {
    esp_now_send(mac, (const uint8_t*)p, sizeof(np_pkt_t));
}

// ---- 接收回调 ----
// ---- 接收回调（旧版 ESP-NOW API）----
static void on_recv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (len != sizeof(np_pkt_t)) return;
    const np_pkt_t *p = (const np_pkt_t*)data;
    if (p->magic != PKT_MAGIC) return;

    switch (p->type) {
    case PKT_HELLO:
    case PKT_HELLO_ACK:
        if (s_state == NP_DISCOVERING) {
            memcpy(s_peer_mac, mac_addr, 6);          // ← 改这里
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, s_peer_mac, 6);
            peer.channel = 0;
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;
            if (!esp_now_is_peer_exist(s_peer_mac)) esp_now_add_peer(&peer);

            if (p->type == PKT_HELLO) {
                np_pkt_t ack = { .magic = PKT_MAGIC, .type = PKT_HELLO_ACK };
                send_to(s_peer_mac, &ack);
            }
            s_role = (mac_cmp(s_my_mac, s_peer_mac) < 0) ? NP_ROLE_HOST : NP_ROLE_CLIENT;
            s_state = NP_CONNECTED;
            Serial.printf("[NP] Connected as %s, peer %02X:%02X:%02X:%02X:%02X:%02X\n",
                          s_role == NP_ROLE_HOST ? "HOST" : "CLIENT",
                          s_peer_mac[0], s_peer_mac[1], s_peer_mac[2],
                          s_peer_mac[3], s_peer_mac[4], s_peer_mac[5]);
        }
        break;

    case PKT_INPUT: {
        static uint32_t rxCt = 0;
        uint8_t i = idx_of(p->frame_id);
        s_remote_buf[i] = p->pad;
        s_remote_valid[i] = true;
        if (s_sem) xSemaphoreGive(s_sem);
        if (++rxCt % 30 == 1) {
            ets_printf("[NP] rx INPUT #%u frame=%u pad=%02X\n",
                       (unsigned)rxCt, (unsigned)p->frame_id, p->pad);
        }
        break;
    }

    case PKT_RESET:
        s_reset_pending = true;
        s_reset_at = p->frame_id;
        Serial.printf("[NP] RESET scheduled at frame %u\n", (unsigned)p->frame_id);
        break;

    case PKT_SYNC_CRC:
        s_remote_crc = p->crc;
        s_remote_crc_ready = true;
        break;
    }
}

static void on_send(const uint8_t *mac_addr, esp_now_send_status_t status) {
    (void)mac_addr; (void)status;
}

// ---- API ----
bool netplay_init(void) {
    if (s_initialized) return true;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, true);
    esp_wifi_set_ps(WIFI_PS_NONE);  // 关掉省电，降低延迟

    esp_read_mac(s_my_mac, ESP_MAC_WIFI_STA);
    Serial.printf("[NP] My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  s_my_mac[0], s_my_mac[1], s_my_mac[2],
                  s_my_mac[3], s_my_mac[4], s_my_mac[5]);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[NP] esp_now_init failed");
        return false;
    }
    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_send);

    esp_now_peer_info_t bp = {};
    memcpy(bp.peer_addr, BCAST, 6);
    bp.channel = 0;
    bp.ifidx = WIFI_IF_STA;
    bp.encrypt = false;
    esp_now_add_peer(&bp);

    if (!s_sem) s_sem = xSemaphoreCreateBinary();
    memset(s_remote_valid, 0, sizeof(s_remote_valid));
    for (int i = 0; i < NETPLAY_INPUT_DELAY; i++) {
        s_remote_buf[i] = 0;
        s_remote_valid[i] = true;
    }
    s_initialized = true;
    return true;
}

void netplay_deinit(void) {
    if (!s_initialized) return;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    s_state = NP_IDLE;
    s_role = NP_ROLE_NONE;
    s_frame = 0;
    s_initialized = false;
    Serial.println("[NP] deinit done");
}

bool netplay_start(uint32_t timeout_ms) {
    if (!s_initialized && !netplay_init()) return false;
    s_state = NP_DISCOVERING;
    s_abort = false;

    np_pkt_t hello = { .magic = PKT_MAGIC, .type = PKT_HELLO };
    uint32_t waited = 0;
    while (s_state == NP_DISCOVERING && !s_abort) {
        send_to(BCAST, &hello);
        delay(300);
        waited += 300;
        if (timeout_ms && waited >= timeout_ms) {
            s_state = NP_IDLE;
            Serial.println("[NP] discovery timeout");
            return false;
        }
    }
    if (s_abort) { s_state = NP_IDLE; return false; }

    s_frame = 0;
    // Pre-fill just enough for initial handshake (~100ms at 60fps)
    memset(s_remote_valid, 0, sizeof(s_remote_valid));
    memset(s_remote_buf, 0, sizeof(s_remote_buf));
    memset(s_local_buf, 0, sizeof(s_local_buf));
    int prefill = NETPLAY_INPUT_DELAY + 3;
    for (int i = 0; i < prefill; i++) {
        s_remote_buf[i] = 0;
        s_remote_valid[i] = true;
    }
    if (s_role == NP_ROLE_HOST) netplay_request_reset();
    return true;
}

void netplay_abort(void) { s_abort = true; }
bool netplay_is_enabled(void) { return s_state == NP_CONNECTED; }
netplay_state_t netplay_state(void) { return s_state; }
netplay_role_t  netplay_role(void)  { return s_role; }

void netplay_request_reset(void) {
    if (s_role != NP_ROLE_HOST) return;
    np_pkt_t p = { .magic = PKT_MAGIC, .type = PKT_RESET,
                   .frame_id = s_frame + 10 };
    send_to(s_peer_mac, &p);
    s_reset_pending = true;
    s_reset_at = p.frame_id;
}

bool netplay_should_reset(void) {
    if (s_reset_pending && s_frame >= s_reset_at) {
        s_reset_pending = false;
        memset(s_remote_valid, 0, sizeof(s_remote_valid));
        memset(s_remote_buf, 0, sizeof(s_remote_buf));
        memset(s_local_buf, 0, sizeof(s_local_buf));
        int prefill = NETPLAY_INPUT_DELAY + 3;
        for (int i = 0; i < prefill; i++) {
            s_remote_buf[i] = 0;
            s_remote_valid[i] = true;
        }
        s_frame = 0;
        return true;
    }
    return false;
}

void netplay_set_sync_region(const void *ram, uint32_t size) {
    s_sync_ram = ram;
    s_sync_size = size;
}

bool netplay_sync_frame(uint8_t local_pad,
                        uint8_t *out1, uint8_t *out2) {
    if (s_state != NP_CONNECTED) return false;

    uint32_t local_target = s_frame + NETPLAY_INPUT_DELAY;
    s_local_buf[idx_of(local_target)] = local_pad;

    np_pkt_t pkt = { .magic = PKT_MAGIC, .type = PKT_INPUT,
                     .frame_id = local_target, .pad = local_pad };
    send_to(s_peer_mac, &pkt);

    uint8_t i = idx_of(s_frame);
    TickType_t t0 = xTaskGetTickCount();
    while (!s_remote_valid[i]) {
        // Short semaphore wait (2ms) to avoid blocking the emulation loop
        if (xSemaphoreTake(s_sem, pdMS_TO_TICKS(1)) == pdFALSE) {
            if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(NETPLAY_TIMEOUT_MS)) {
                Serial.printf("[NP] Timeout frame %u\n", (unsigned)s_frame);
                s_state = NP_LOST;
                return false;
            }
            // Keep sending while waiting
            send_to(s_peer_mac, &pkt);
        }
    }

    uint8_t remote = s_remote_buf[i];
    uint8_t mine = (s_frame < NETPLAY_INPUT_DELAY) ? 0 : s_local_buf[i];
    s_remote_valid[i] = false;

    if (s_role == NP_ROLE_HOST) { *out1 = mine;   *out2 = remote; }
    else                        { *out1 = remote; *out2 = mine;   }

    // 周期 CRC 校验
    if (s_sync_ram && s_frame > 0 && (s_frame % NETPLAY_SYNC_INTERVAL == 0)) {
        uint32_t crc = esp_crc32_le(0, (const uint8_t*)s_sync_ram, s_sync_size);
        np_pkt_t sp = { .magic = PKT_MAGIC, .type = PKT_SYNC_CRC,
                        .frame_id = s_frame, .crc = crc };
        send_to(s_peer_mac, &sp);
        if (s_remote_crc_ready) {
            if (s_remote_crc != crc) {
                ets_printf("[NP] DESYNC local=%08X remote=%08X frame=%u (ignored)\n",
                           (unsigned)crc, (unsigned)s_remote_crc, (unsigned)s_frame);
                // TODO: re-enable reset after fixing input delay
                // if (s_role == NP_ROLE_HOST) netplay_request_reset();
            }
            s_remote_crc_ready = false;
        }
    }

    s_frame++;
    return true;
}

void netplay_dispatch_pads(uint8_t pad1, uint8_t pad2) {
    static uint8_t last1 = 0xFF, last2 = 0xFF;
    if (last1 == 0xFF) { last1 = 0; last2 = 0; }

    uint8_t d1 = pad1 ^ last1;
    uint8_t d2 = pad2 ^ last2;

    for (int b = 0; b < 8; b++) {
        if (d1 & (1 << b)) {
            event_t e = event_get(evt_p1[b]);
            if (e) e((pad1 & (1 << b)) ? INP_STATE_MAKE : INP_STATE_BREAK);
        }
        if (d2 & (1 << b)) {
            event_t e = event_get(evt_p2[b]);
            if (e) e((pad2 & (1 << b)) ? INP_STATE_MAKE : INP_STATE_BREAK);
        }
    }
    last1 = pad1;
    last2 = pad2;
}