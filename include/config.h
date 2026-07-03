
//  =======================屏幕大小===================

#define SCREEN_W 600
#define SCREEN_H 450

// ===================== 引脚定义（基于原理图） =====================


// BAT_Control — Waveshare 2.41" 电池供电保持引脚（高电平维持供电）
#ifndef POWER_KEY_GPIO
#define POWER_KEY_GPIO 16
#endif

// SD卡 SPI引脚
#ifndef SD_MISO_PIN
#define SD_MISO_PIN  6
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN  5
#endif
#ifndef SD_CLK_PIN
#define SD_CLK_PIN   4
#endif
#ifndef SD_CS_PIN
#define SD_CS_PIN    2
#endif

// 物理按键引脚（使用板上空闲GPIO）
// 如果不接物理按键，将使用触摸屏操作
#ifndef BTN_UP
#define BTN_UP       1     // GPIO1
#endif
#ifndef BTN_DOWN
#define BTN_DOWN     7     // GPIO7
#endif
#ifndef BTN_LEFT
#define BTN_LEFT     8     // GPIO8
#endif
#ifndef BTN_RIGHT
#define BTN_RIGHT    18    // GPIO18
#endif
#ifndef BTN_A
#define BTN_A        38    // GPIO38
#endif
#ifndef BTN_B
#define BTN_B        39    // GPIO39
#endif
#ifndef BTN_SELECT
#define BTN_SELECT   40    // GPIO40
#endif
#ifndef BTN_START
#define BTN_START    41    // GPIO41
#endif
#ifndef BTN_QUIT
#define BTN_QUIT     43    // GPIO46
#endif

//Audio
#ifndef DI2S_LRC_PIN
#define DI2S_LRC_PIN      42
#endif
#ifndef DI2S_BCLK_PIN
#define DI2S_BCLK_PIN     45
#endif
#ifndef DI2S_DIN_PIN
#define DI2S_DIN_PIN      46
#endif

//I2C
#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN       47
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN       48
#endif
#define I2C_PORT I2C_NUM_0
#define EXIO_ADDR 0x20

/* ADC_11db  获取电量针脚*/
#ifndef ADC_11DB_PIN
#define ADC_11DB_PIN      17
#endif

/*震动*/
#ifndef MOTO_PIN_EXIO
#define MOTO_PIN_EXIO     7
#endif
