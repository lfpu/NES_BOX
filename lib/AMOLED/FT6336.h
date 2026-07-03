#ifndef _FT6336_H
#define _FT6336_H

#define I2C_ADDR_FT6336 0x38

#include <Wire.h>
class FT6336
{
public:
    //FT6336(int8_t sda_pin = -1, int8_t scl_pin = -1, int8_t rst_pin = -1, int8_t int_pin = -1);
    FT6336(void);
    void begin(void);
    bool getTouch(int16_t *x, int16_t *y, uint8_t *gesture);

private:
    int8_t _sda, _scl, _rst, _int;

    uint8_t i2c_read(uint8_t addr);
    uint8_t i2c_read_continuous(uint8_t addr, uint8_t *data, uint32_t length);
    void i2c_write(uint8_t addr, uint8_t data);
    uint8_t i2c_write_continuous(uint8_t addr, const uint8_t *data, uint32_t length);
};

#endif