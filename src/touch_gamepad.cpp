#include "touch_gamepad.h"

TouchGamepad::TouchGamepad()
    : _overlay(nullptr)
    , _scrW(0), _scrH(0)
    , _padState(0)
{
}

void TouchGamepad::init(uint16_t screenW, uint16_t screenH)
{
    _scrW = screenW;
    _scrH = screenH;
}

void TouchGamepad::show()
{
    if (_overlay) return;

    _overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_overlay, _scrW, _scrH);
    lv_obj_set_pos(_overlay, 0, 0);
    lv_obj_set_style_bg_opa(_overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_overlay, 0, 0);
    lv_obj_clear_flag(_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // 左侧十字键
    _createDpad(_overlay, 20, _scrH - 200);

    // 右侧AB键
    _createActionBtns(_overlay, _scrW - 180, _scrH - 200);

    // 中间SELECT/START
    _createMenuBtns(_overlay);
}

void TouchGamepad::hide()
{
    if (_overlay) {
        lv_obj_del(_overlay);
        _overlay = nullptr;
    }
    _padState = 0;
}

void TouchGamepad::setOpacity(uint8_t opa)
{
    if (_overlay) {
        // 设置所有子对象透明度
        uint32_t cnt = lv_obj_get_child_cnt(_overlay);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t* child = lv_obj_get_child(_overlay, i);
            lv_obj_set_style_opa(child, opa, 0);
        }
    }
}

void TouchGamepad::_createDpad(lv_obj_t* parent, int x, int y)
{
    int btnSize = 60;
    int gap = 5;

    // 上
    _createCircleBtn(parent, x + btnSize + gap, y,
                     btnSize, btnSize, LV_SYMBOL_UP,
                     NES_PAD_UP, 0x444488);
    // 下
    _createCircleBtn(parent, x + btnSize + gap, y + (btnSize + gap) * 2,
                     btnSize, btnSize, LV_SYMBOL_DOWN,
                     NES_PAD_DOWN, 0x444488);
    // 左
    _createCircleBtn(parent, x, y + btnSize + gap,
                     btnSize, btnSize, LV_SYMBOL_LEFT,
                     NES_PAD_LEFT, 0x444488);
    // 右
    _createCircleBtn(parent, x + (btnSize + gap) * 2, y + btnSize + gap,
                     btnSize, btnSize, LV_SYMBOL_RIGHT,
                     NES_PAD_RIGHT, 0x444488);
}

void TouchGamepad::_createActionBtns(lv_obj_t* parent, int x, int y)
{
    int btnSize = 70;

    // B键（左）
    _createCircleBtn(parent, x, y + 50,
                     btnSize, btnSize, "B",
                     NES_PAD_B, 0xCC2222);

    // A键（右上）
    _createCircleBtn(parent, x + 80, y + 10,
                     btnSize, btnSize, "A",
                     NES_PAD_A, 0xCC2222);
}

void TouchGamepad::_createMenuBtns(lv_obj_t* parent)
{
    int btnW = 80;
    int btnH = 35;
    int centerX = _scrW / 2;
    int y = _scrH - 60;

    // SELECT
    _createCircleBtn(parent, centerX - btnW - 15, y,
                     btnW, btnH, "SEL",
                     NES_PAD_SELECT, 0x555555);

    // START
    _createCircleBtn(parent, centerX + 15, y,
                     btnW, btnH, "STA",
                     NES_PAD_START, 0x555555);
}

lv_obj_t* TouchGamepad::_createCircleBtn(lv_obj_t* parent, int x, int y,
                                          int w, int h, const char* label,
                                          uint8_t padBit, uint32_t color)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color + 0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_90, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, w / 2, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    // 存储padBit到user_data（打包this指针和bit）
    // 用简单方法：高24位存this指针索引，低8位存padBit
    uint32_t userData = (uint32_t)(uintptr_t)this;
    // 我们把padBit存在另一个地方
    lv_obj_set_user_data(btn, (void*)(uintptr_t)((userData & 0xFFFFFF00) | padBit));

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);

    // 按下和释放事件
    lv_obj_add_event_cb(btn, _btnPressCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(btn, _btnReleaseCb, LV_EVENT_RELEASED, this);

    return btn;
}

void TouchGamepad::_btnPressCb(lv_event_t* e)
{
    TouchGamepad* self = (TouchGamepad*)lv_event_get_user_data(e);
    lv_obj_t* btn = lv_event_get_target(e);
    uint32_t ud = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
    uint8_t padBit = ud & 0xFF;
    self->_padState |= padBit;
}

void TouchGamepad::_btnReleaseCb(lv_event_t* e)
{
    TouchGamepad* self = (TouchGamepad*)lv_event_get_user_data(e);
    lv_obj_t* btn = lv_event_get_target(e);
    uint32_t ud = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
    uint8_t padBit = ud & 0xFF;
    self->_padState &= ~padBit;
}