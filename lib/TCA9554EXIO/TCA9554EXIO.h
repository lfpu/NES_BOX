#pragma once

#include <stdio.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"



// EXIO 输出缓存（非常重要，用来避免覆盖）
static uint8_t exio_output_state = 0x00;

// =========================
// I2C 初始化
// =========================
void i2c_master_init(void);
// =========================
// 写寄存器
// =========================
esp_err_t exio_write_reg(uint8_t reg, uint8_t data);
// =========================
// EXIO 初始化
// =========================
void exio_init(void);
// =========================
// 设置某一位（类似 GPIO）
// =========================
void exio_set_bit(uint8_t bit);
// =========================
// 清某一位
// =========================
void exio_clear_bit(uint8_t bit);
// =========================
// 切换某一位
// =========================
void exio_toggle_bit(uint8_t bit);

void EXIO_Initialize(void);