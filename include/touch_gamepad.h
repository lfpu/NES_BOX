#ifndef TOUCH_GAMEPAD_H
#define TOUCH_GAMEPAD_H

#include <Arduino.h>
#include <lvgl.h>
#include "nes_launcher.h"

// ===================== 触摸屏虚拟手柄 =====================
// 用于没有物理按键时，在触摸屏上显示虚拟手柄
class TouchGamepad {
public:
    TouchGamepad();

    // 初始化（在LVGL环境中创建UI）
    void init(uint16_t screenW, uint16_t screenH);

    // 显示/隐藏
    void show();
    void hide();

    // 获取当前按键状态
    uint8_t getPadState() const { return _padState; }

    // 设置透明度
    void setOpacity(uint8_t opa);

private:
    static void _btnPressCb(lv_event_t* e);
    static void _btnReleaseCb(lv_event_t* e);

    void _createDpad(lv_obj_t* parent, int x, int y);
    void _createActionBtns(lv_obj_t* parent, int x, int y);
    void _createMenuBtns(lv_obj_t* parent);
    lv_obj_t* _createCircleBtn(lv_obj_t* parent, int x, int y,
                                int w, int h, const char* label,
                                uint8_t padBit, uint32_t color);

    lv_obj_t* _overlay;
    uint16_t _scrW, _scrH;
    volatile uint8_t _padState;
};

#endif // TOUCH_GAMEPAD_H