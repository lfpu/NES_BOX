#include "TCA9554EXIO.h"
#include <stdio.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

void i2c_master_init(void)
{

    i2c_config_t conf;

    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_SDA_PIN;
    conf.scl_io_num = I2C_SCL_PIN;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;

    i2c_param_config(I2C_PORT, &conf);
    i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

esp_err_t exio_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t buf[2] = {reg, data};
    return i2c_master_write_to_device(I2C_PORT, EXIO_ADDR, buf, 2, pdMS_TO_TICKS(10));
}

void exio_init(void)
{
    // 配置方向寄存器（0x03）
    // 0 = 输出
    exio_write_reg(0x03, 0x00);

    // 初始化输出寄存器
    exio_output_state = 0x00;
    exio_write_reg(0x01, exio_output_state);
}
void exio_set_bit(uint8_t bit)
{
    exio_output_state |= (1 << bit);
    exio_write_reg(0x01, exio_output_state);
}
void exio_clear_bit(uint8_t bit)
{
    exio_output_state &= ~(1 << bit);
    exio_write_reg(0x01, exio_output_state);
}
void exio_toggle_bit(uint8_t bit)
{
    exio_output_state ^= (1 << bit);
    exio_write_reg(0x01, exio_output_state);
}

void EXIO_Initialize(void)
{
    i2c_master_init();
    exio_init();
}