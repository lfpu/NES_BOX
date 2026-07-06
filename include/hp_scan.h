#ifndef HP_SCAN_H
#define HP_SCAN_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 主循环调用 */
void hp_scan_update(void);

/* 切 ROM 时调用：告诉扫描器当前 ROM 名（用于自动落盘） */
void hp_scan_set_rom(const char* romName);

/* 手动清空状态 */
void hp_scan_reset(void);

/* 查询：已经学到并落盘了吗？若是，返回 HP 地址（非 0）；否则 0 */
uint16_t hp_scan_get_learned_addr(void);

#ifdef __cplusplus
}
#endif
#endif