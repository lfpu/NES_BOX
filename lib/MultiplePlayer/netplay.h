#ifndef NETPLAY_H
#define NETPLAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// NES 标准 bit 顺序，与 NES_PAD_* / INP_PAD_* 一致
#define NP_BTN_A      0x01
#define NP_BTN_B      0x02
#define NP_BTN_SELECT 0x04
#define NP_BTN_START  0x08
#define NP_BTN_UP     0x10
#define NP_BTN_DOWN   0x20
#define NP_BTN_LEFT   0x40
#define NP_BTN_RIGHT  0x80

#define NETPLAY_INPUT_DELAY    3
#define NETPLAY_BUF_SIZE       16
#define NETPLAY_TIMEOUT_MS     2000
#define NETPLAY_SYNC_INTERVAL  60

typedef enum {
    NP_IDLE = 0,
    NP_DISCOVERING,
    NP_CONNECTED,
    NP_LOST
} netplay_state_t;

typedef enum {
    NP_ROLE_NONE = 0,
    NP_ROLE_HOST,
    NP_ROLE_CLIENT
} netplay_role_t;

bool  netplay_init(void);
void  netplay_deinit(void);
bool  netplay_start(uint32_t timeout_ms);     // 阻塞握手
void  netplay_abort(void);                    // 中止握手
bool  netplay_is_enabled(void);               // 已连接？
netplay_state_t netplay_state(void);
netplay_role_t  netplay_role(void);

// 主循环每帧调用：传本地 g_padState，返回 pad1/pad2
bool netplay_sync_frame(uint8_t local_pad,
                        uint8_t *out_pad1,
                        uint8_t *out_pad2);

// 把 pad1/pad2 变化转发给 nofrendo event 系统
void netplay_dispatch_pads(uint8_t pad1, uint8_t pad2);

// CRC 失步检测
void netplay_set_sync_region(const void *ram, uint32_t size);

// Host 广播 reset，两边同帧执行
void netplay_request_reset(void);
bool netplay_should_reset(void);

#ifdef __cplusplus
}
#endif
#endif