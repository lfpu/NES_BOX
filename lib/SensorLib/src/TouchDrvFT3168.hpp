#pragma once
#include "TouchDrvInterface.hpp"
#include "SensorCommon.tpp"


#define FT3168_SLAVE_ADDRESS       (0x38)
class TouchDrvFT3168 : public TouchDrvInterface , public SensorCommon<TouchDrvFT3168>
{
  friend class SensorCommon<TouchDrvFT3168>;
public:
  enum GesTrue {
    NO_GESTURE = 0x00,       //无手势
    MOVE_UP = 0x04,          //向上滑动
    MOVE_LEFT = 0x01,        //向左滑动
    MOVE_DOWN = 0x08,        //向下滑动
    MOVE_RIGHT = 0x02,       //向右滑动
    ZOOM_IN = 0x00,
    ZOOM_OUT = 0x00,
  } ;
  enum PowerMode {
    PMODE_ACTIVE = 0,         // ~4mA
    PMODE_MONITOR = 1,        // ~3mA
    PMODE_DEEPSLEEP = 3,      // ~100uA  The reset pin must be pulled down to wake up
  };
  #if defined(ARDUINO)
    TouchDrvFT3168(PLATFORM_WIRE_TYPE &w, int sda = DEFAULT_SDA, int scl = DEFAULT_SCL, uint8_t addr = FT3168_SLAVE_ADDRESS)
    {
      Serial.print("1\n");
      __wire = &w;
      __sda = sda;
      __scl = scl;
      __addr = addr;
    }
    bool begin(PLATFORM_WIRE_TYPE &w,uint8_t addr = FT3168_SLAVE_ADDRESS,int sda = DEFAULT_SDA,int scl = DEFAULT_SCL)
    {
      Serial.print("2\n");
      return SensorCommon::begin(w, addr, sda, scl);
    }
  #endif
  TouchDrvFT3168()
  {
    Serial.print("3\n");
    __wire = &Wire;
    __sda = DEFAULT_SDA;
    __scl = DEFAULT_SCL;
    __addr = FT3168_SLAVE_ADDRESS;
  }
  ~TouchDrvFT3168()
  {
    deinit();
  }
  void deinit()
  {
    // end();
  }
  void reset()
  {
    if (__rst != SENSOR_PIN_NONE)
    {
      Serial.print("4\n");
      this->setGpioMode(__rst, OUTPUT);
      this->setGpioLevel(__rst, HIGH);
      delay(10);
      this->setGpioLevel(__rst, LOW);
      delay(30);
      this->setGpioLevel(__rst, HIGH);
      // For the variant of GPIO extended RST,
      // communication and delay are carried out simultaneously, and 160ms is measured in T-RGB esp-idf new api
      delay(160);
    }
  }
  bool begin(uint8_t addr, iic_fptr_t readRegCallback, iic_fptr_t writeRegCallback)
  {
    Serial.print("5\n");
    return SensorCommon::begin(addr, readRegCallback, writeRegCallback);
  }
  uint8_t getDeviceMode(void)
  {
    Serial.print("6\n");
    return readRegister(0x00) & 0x03;
  }
  uint8_t getGesture()
  {
    Serial.print("7\n");
    bool FingerIndex = false;
    FingerIndex = (bool)readRegister(0x02);
    if(FingerIndex)
    {
      int val = readRegister(0xD1);
      switch (val)
      {
        case 0x04:
            return MOVE_UP;
        case 0x02:
            return MOVE_RIGHT;
        case 0x08:
            return MOVE_DOWN;
        case 0x01:
            return MOVE_LEFT;
        default:
            break;
      }
    }
    return NO_GESTURE;
  }
  void setThreshold(uint8_t value)
  {
    Serial.print("8\n");
    //writeRegister(FT6X36_REG_THRESHOLD, value);
  }
  uint8_t getThreshold(void)
  {
    Serial.print("9\n");
    //return readRegister(FT6X36_REG_THRESHOLD);
  }

  uint8_t getMonitorTime(void)
  {
    Serial.print("10\n");
    //return readRegister(FT6X36_REG_MONITOR_TIME);
  }

  void setMonitorTime(uint8_t sec)
  {
    Serial.print("11\n");
    //writeRegister(FT6X36_REG_MONITOR_TIME, sec);
  }
  uint16_t getLibraryVersion()
  {
    Serial.print("12\n");
    //uint8_t buffer[2];
    //readRegister(FT6X36_REG_LIB_VERSION_H, buffer, 2);
    //return (buffer[0] << 8) | buffer[1];
  }
  void interruptPolling(void)
  {
    Serial.print("13\n");
    //datasheet this bit is 0,Actually, it's wrong
    // writeRegister(FT6X36_REG_INT_STATUS, 1);
  }
  // Triggers an interrupt whenever a touch is detected
  void interruptTrigger(void)
  {
    Serial.print("14\n");
    //datasheet this bit is 1,Actually, it's wrong
    //writeRegister(FT6X36_REG_INT_STATUS, 0);
  }
  uint8_t getPoint(int16_t *x_array, int16_t *y_array, uint8_t size = 1)
  {
    Serial.print("15\n");
    uint8_t gesture;
    bool FingerIndex = false;
    FingerIndex = (bool)readRegister(0x02);
    printf("FingerIndex: %d\n",FingerIndex);
    gesture = readRegister(0xD1);
    if (!(gesture == MOVE_UP || gesture == MOVE_DOWN))
    {
      printf("AA\n");
      gesture = NO_GESTURE;
    }
    uint8_t data[4];
    readRegister(0x03, data, 4);
    *x_array = (((int16_t)data[0] & 0x0F) << 8) | data[1];
    *y_array = (((int16_t)data[2] & 0x0F) << 8) | data[3];

    // *x = 240 - *x;
    updateXY(1, x_array, y_array);

    return FingerIndex;
  }
  bool isPressed()
  {
    Serial.print("16\n");
    //if (__irq != SENSOR_PIN_NONE)
    //{
    //  return this->getGpioLevel(__irq) == LOW;
    //}
    //return readRegister(FT6X36_REG_STATUS) & 0x0F;
  }
  void setPowerMode(PowerMode mode)
  {
    Serial.print("17\n");
    writeRegister(0x00,0x00); // 切换到工厂模式
  }
  void sleep()
  {
    Serial.print("18\n");
  }
  void wakeup()
  {
    Serial.print("19\n");
  }
  void idle()
  {
    Serial.print("20\n");
  }
  uint8_t getSupportTouchPoint()
  {
    Serial.print("21\n");
    return 1;
  }
  uint32_t getChipID(void)
  {
    Serial.print("22\n");
    //return readRegister(FT6X36_REG_CHIP_ID);
    return 0x11;
  }

  uint8_t getVendorID(void)
  {
    Serial.print("23\n");
    //return readRegister(FT6X36_REG_VENDOR1_ID);
    return 0x11;
  }
  uint8_t getErrorCode(void)
  {
    Serial.print("24\n");
    //return readRegister(FT6X36_REG_ERROR_STATUS);
    return 0x22;
  }
  const char *getModelName()
  {
    Serial.print("25\n");
    return "UNKNOWN";
  }
  bool getResolution(int16_t *x, int16_t *y)
  {
    Serial.print("26\n");
    return false;
  }

  void  setGpioCallback(gpio_mode_fptr_t mode_cb,gpio_write_fptr_t write_cb,gpio_read_fptr_t read_cb)
  {
    Serial.print("27\n");
    SensorCommon::setGpioModeCallback(mode_cb);
    SensorCommon::setGpioWriteCallback(write_cb);
    SensorCommon::setGpioReadCallback(read_cb);
  }
private:
  bool initImpl()
  {
    bool res = 0;
    Serial.print("28\n");
    if (__irq != SENSOR_PIN_NONE)
    {
      this->setGpioMode(__irq, INPUT);
    }
    //reset();
    res = writeRegister(0x00,0x00);
    if(!res)
    return 0;
    return true;
  }
  int getReadMaskImpl()
  {
    Serial.print("29\n");
    return -1;
  }
};
