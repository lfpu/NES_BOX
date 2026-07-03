/*
 * hp_scan.h — NES RAM health-address scanner
 *
 * Enable with: -DHP_SCAN_ENABLE in platformio.ini build_flags
 */

#ifndef HP_SCAN_H
#define HP_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

void hp_scan_update(void);

#ifdef __cplusplus
}
#endif

#endif // HP_SCAN_H
