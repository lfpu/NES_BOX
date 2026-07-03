#include "FT6336.h"
#include <Wire.h>


#define I2C_ADDR_FT3168 0x38

FT6336::FT6336(void)
{
  _sda = 47;
  _scl = 48;
  _rst = -1;
  _int = -1;
}
void FT6336::begin(void)
{
  if (_sda != -1 && _scl != -1)
  {
    Wire.begin(_sda, _scl);
  }
  else
  {
    Wire.begin();
  }
  if (_int != -1)
  {
    pinMode(_int, OUTPUT);
    digitalWrite(_int, HIGH); // 高电平
    delay(1); 
    digitalWrite(_int, LOW); // 低电平
    delay(1);
  }

  // Reset Pin Configuration
  if (_rst != -1)
  {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(10);
    digitalWrite(_rst, HIGH);
    delay(300);
  }
  // Initialize Touch
  i2c_write(0x00, 0x00); // 切换到工厂模式
  //i2c_write(0xBC, 0x04); // 切换到工厂模式
}
bool FT6336::getTouch(int16_t *x, int16_t *y, uint8_t *gesture)
{
  uint8_t FingerIndex = false;
  FingerIndex = (bool)i2c_read(0x02); //报点个数
  if(!FingerIndex)
  return 0;
  uint8_t data[4];
  i2c_read_continuous(0x03, data, 4);
  *y = ((data[0] & 0x0F) << 8) | data[1];
  *x = 600-(((data[2] & 0x0F) << 8) | data[3]);
  *gesture = FingerIndex;
  return 1;
}
uint8_t FT6336::i2c_read(uint8_t addr)
{
    uint8_t rdData;
    uint8_t rdDataCount;
    do
    {
      Wire.beginTransmission(I2C_ADDR_FT6336);
      Wire.write(addr);
      Wire.endTransmission(false); // Restart
      rdDataCount = Wire.requestFrom(I2C_ADDR_FT6336, 1);
    } while (rdDataCount == 0);
    while (Wire.available())
    {
      rdData = Wire.read();
    }
    return rdData;
}

uint8_t FT6336::i2c_read_continuous(uint8_t addr, uint8_t *data, uint32_t length)
{
  Wire.beginTransmission(I2C_ADDR_FT6336);
  Wire.write(addr);
  if (Wire.endTransmission(true)) return -1;
  Wire.requestFrom(I2C_ADDR_FT6336, length);
  for (int i = 0; i < length; i++)
  {
    *data++ = Wire.read();
  }
  return 0;
}

void FT6336::i2c_write(uint8_t addr, uint8_t data)
{
  Wire.beginTransmission(I2C_ADDR_FT6336);
  Wire.write(addr);
  Wire.write(data);
  Wire.endTransmission();
}