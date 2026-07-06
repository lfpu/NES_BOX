#include "game_ui.h"
#include "audio_player.h"
#include <SD.h>
#include <FS.h>
#include <algorithm>
#include <cstring>

#include "Ws_AMOLED.h"
extern AudioPlayer audioPlayer;
extern Ws_AMOLED amoled;
extern "C" void settings_changed(int brightness, int volume);
extern int g_settingBrightness;
extern int g_settingVolume;
extern lv_obj_t *loading;
extern uint8_t *g_selectPCM;
extern size_t g_selectSize;
extern uint8_t *g_startPCM;
extern size_t g_startSize;
extern volatile bool g_netplayWanted;
extern int g_rotation180;
extern uint16_t screenW, screenH;

// ===================== 构造/析构 =====================
GameUI::GameUI()
    : _selectedIndex(0),
      _viewMode(VIEW_CAROUSEL),
      _state(UI_BROWSING),
      _scrW(600),
      _scrH(450),
      _mainScreen(nullptr),
      _listContainer(nullptr),
      _listView(nullptr),
      _carouselContainer(nullptr),
      _gameCard(nullptr),
      _gameNameLabel(nullptr),
      _gameIndexLabel(nullptr),
      _gameTotalLabel(nullptr),
      _instructionLabel(nullptr),
      _progressDots(nullptr),
      _decorLine1(nullptr),
      _decorLine2(nullptr),
      _viewModeBtn(nullptr),
      _settingsBtn(nullptr),
      _headerLabel(nullptr),
      _launchOverlay(nullptr),
      _settingsOverlay(nullptr),
      _batteryLabel(nullptr),
      _brightnessSlider(nullptr),
      _volumeSlider(nullptr),
      _gameSearcher(nullptr),
      _searchOverlay(nullptr),
      _searchResultList(nullptr),
      _searchTextArea(nullptr),
      _searchKeyboard(nullptr),
      _bgImg(nullptr),
      _bgLayer(nullptr),
      _bgImgA(nullptr),
      _bgImgB(nullptr),
      _shaderOverlay(nullptr),
      _bgUsingA(true),
      _lastNavDir(1),
      _lastCarouselAnimTick(0),
      _launchCb(nullptr),
      _lastInputTime(0)
{
    _bgPathA[0] = ' ';
    _bgPathB[0] = ' ';
}

GameUI::~GameUI() {}

void GameUI::init(uint16_t screenW, uint16_t screenH)
{
    _scrW = screenW;
    _scrH = screenH;

    _leftNavData.ui = this;
    _leftNavData.isLeft = true;

    _rightNavData.ui = this;
    _rightNavData.isLeft = false;
}

// ===================== 加载游戏列表（带缓存） =====================
bool GameUI::loadGameList(const char *path)
{
    _games.clear();

    String cachePath = String(path) + "/.cache";

    // 1. 尝试从缓存加载
    File cacheFile = SD.open(cachePath, FILE_READ);
    if (cacheFile && cacheFile.size() > 0)
    {
        Serial.println("[GameUI] Loading from cache...");
        while (cacheFile.available())
        {
            String line = cacheFile.readStringUntil('\n');
            // line.trim();
            int sep = line.indexOf('|');
            if (sep > 0)
            {
                GameInfo info;
                info.fullPath = line.substring(0, sep);
                info.name = line.substring(sep + 1);
                info.imgPath = line.substring(sep + 1) + ".png";
                _games.push_back(info);
                if (loading && (_games.size() % 20 == 0))
                {
                    lv_label_set_text(loading, info.name.c_str());
                    lv_timer_handler();
                    delay(5);
                }
            }
        }
        cacheFile.close();
        if (!_games.empty())
        {
            Serial.printf("[GameUI] Cache loaded: %d games\n", _games.size());
            // Still sort (cache should be sorted but just in case)
            std::sort(_games.begin(), _games.end(), [](const GameInfo &a, const GameInfo &b)
                      { return a.name < b.name; });
            return true;
        }
    }

    // 2. 缓存不存在或为空，从 SD 扫描
    Serial.printf("[GameUI] Scanning %s ...\n", path);
    File root = SD.open(path);
    if (!root || !root.isDirectory())
    {
        Serial.printf("[GameUI] Failed to open: %s\n", path);
        return false;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (!file.isDirectory())
        {
            String fname = String(file.name());
            if (fname.endsWith(".nes") || fname.endsWith(".NES"))
            {
                GameInfo info;
                info.fullPath = String(path) + "/" + fname;
                info.name = fname.substring(0, fname.lastIndexOf('.'));
                info.name.replace('_', ' ');
                info.imgPath = info.name + ".png";
                _games.push_back(info);
                Serial.printf("[GameUI] Found: %s\n", info.name.c_str());
                // Update splash screen with game name
                if (loading)
                {
                    lv_label_set_text(loading, info.name.c_str());
                    lv_timer_handler();
                    delay(10);
                }
            }
        }
        file = root.openNextFile();
    }
    root.close();

    std::sort(_games.begin(), _games.end(), [](const GameInfo &a, const GameInfo &b)
              { return a.name < b.name; });

    // 3. 写入缓存文件
    cachePath = String(path) + "/.cache";
    File cacheOut = SD.open(cachePath, FILE_WRITE);
    if (cacheOut)
    {
        for (const auto &g : _games)
        {
            cacheOut.printf("%s|%s\n", g.fullPath.c_str(), g.name.c_str());
        }
        cacheOut.close();
        Serial.printf("[GameUI] Cache saved: %d games\n", _games.size());
    }

    Serial.printf("[GameUI] Total: %d games\n", _games.size());
    return _games.size() > 0;
}

// ===================== 显示UI =====================
void GameUI::show()
{
    _state = UI_BROWSING;
    _mainScreen = lv_scr_act();
    lv_obj_set_style_bg_color(_mainScreen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(_mainScreen, LV_OPA_COVER, 0);

    // 标题
    _headerLabel = lv_label_create(_mainScreen);
    lv_label_set_text(_headerLabel, LV_SYMBOL_PLAY " NES BOX Game");
    lv_obj_set_style_text_font(_headerLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_headerLabel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(_headerLabel, LV_ALIGN_TOP_MID, 0, 12);

    // 设置按钮（标题栏左侧）
    _settingsBtn = lv_btn_create(_mainScreen);
    lv_obj_set_size(_settingsBtn, 60, 40);
    lv_obj_align(_settingsBtn, LV_ALIGN_TOP_LEFT, 8, 5);
    lv_obj_set_style_bg_color(_settingsBtn, lv_color_hex(0x2A2A6A), 0);
    lv_obj_set_style_bg_color(_settingsBtn, lv_color_hex(COLOR_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_radius(_settingsBtn, 20, 0);
    lv_obj_set_style_shadow_width(_settingsBtn, 0, 0);
    lv_obj_set_style_border_width(_settingsBtn, 1, 0);
    lv_obj_set_style_border_color(_settingsBtn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(_settingsBtn, _settingsBtnCb, LV_EVENT_CLICKED, this);
    lv_obj_t *gearLbl = lv_label_create(_settingsBtn);
    lv_label_set_text(gearLbl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gearLbl, &lv_font_montserrat_20, 0);
    lv_obj_center(gearLbl);

    // 电池指示器（标题栏左侧，设置按钮右边）
    _batteryLabel = lv_label_create(_mainScreen);
    lv_label_set_text(_batteryLabel, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(_batteryLabel, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(_batteryLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(_batteryLabel, LV_ALIGN_TOP_LEFT, 80, 10);
    lv_obj_set_style_text_font(_batteryLabel, &lv_font_montserrat_32, 0);

    // 搜索游戏
    _gameSearcher = lv_btn_create(_mainScreen);
    lv_obj_set_size(_gameSearcher, 50, 36);
    lv_obj_align(_gameSearcher, LV_ALIGN_TOP_RIGHT, -100, 5);
    lv_obj_set_style_bg_color(_gameSearcher, lv_color_hex(0x2A2A6A), 0);
    lv_obj_set_style_bg_color(_gameSearcher, lv_color_hex(COLOR_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_radius(_gameSearcher, 8, 0);
    lv_obj_set_style_shadow_width(_gameSearcher, 0, 0);
    lv_obj_set_style_border_width(_gameSearcher, 1, 0);
    lv_obj_set_style_border_color(_gameSearcher, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(_gameSearcher, _searchGameCb, LV_EVENT_CLICKED, this);

    lv_obj_t *SbtnLbl = lv_label_create(_gameSearcher);
    lv_obj_set_style_text_font(SbtnLbl, &lv_font_montserrat_16, 0);
    lv_label_set_text(SbtnLbl, LV_SYMBOL_GPS);
    lv_obj_center(SbtnLbl);

    // 视图切换按钮（标题栏右侧）
    _viewModeBtn = lv_btn_create(_mainScreen);
    lv_obj_set_size(_viewModeBtn, 80, 36);
    lv_obj_align(_viewModeBtn, LV_ALIGN_TOP_RIGHT, -10, 5);
    lv_obj_set_style_bg_color(_viewModeBtn, lv_color_hex(0x2A2A6A), 0);
    lv_obj_set_style_bg_color(_viewModeBtn, lv_color_hex(COLOR_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_radius(_viewModeBtn, 8, 0);
    lv_obj_set_style_shadow_width(_viewModeBtn, 0, 0);
    lv_obj_set_style_border_width(_viewModeBtn, 1, 0);
    lv_obj_set_style_border_color(_viewModeBtn, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(_viewModeBtn, _switchModeBtnCb, LV_EVENT_CLICKED, this);

    lv_obj_t *btnLbl = lv_label_create(_viewModeBtn);
    lv_obj_set_style_text_font(btnLbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(btnLbl, (_viewMode == VIEW_LIST) ? LV_SYMBOL_IMAGE " Card" : LV_SYMBOL_LIST " List");
    lv_obj_center(btnLbl);

    if (_viewMode == VIEW_LIST)
    {
        _createListView();
    }
    else
    {
        _createCarouselView();
    }
}

// ===================== 创建列表视图 =====================
void GameUI::_createListView()
{
    _listContainer = lv_obj_create(_mainScreen);
    lv_obj_set_size(_listContainer, _scrW - 20, _scrH - 50);
    lv_obj_align(_listContainer, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(_listContainer, lv_color_hex(COLOR_LIST_BG), 0);
    lv_obj_set_style_bg_opa(_listContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_listContainer, 12, 0);
    lv_obj_set_style_border_width(_listContainer, 0, 0);
    lv_obj_set_style_pad_all(_listContainer, 5, 0);

    _listView = lv_list_create(_listContainer);
    lv_obj_set_size(_listView, _scrW - 40, _scrH - 65);
    lv_obj_center(_listView);
    lv_obj_set_style_bg_color(_listView, lv_color_hex(COLOR_LIST_BG), 0);
    lv_obj_set_style_border_width(_listView, 0, 0);
    lv_obj_set_style_radius(_listView, 8, 0);
    lv_obj_set_style_pad_row(_listView, 4, 0);

    if (_games.empty())
    {
        lv_obj_t *emptyLabel = lv_label_create(_listView);
        lv_label_set_text(emptyLabel, "No .nes files found in /game/nes");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_align(emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(emptyLabel, _scrW - 80);
        return;
    }

    for (int i = 0; i < (int)_games.size(); i++)
    {
        lv_obj_t *btn = lv_list_add_btn(_listView, LV_SYMBOL_FILE, _games[i].name.c_str());
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_LIST_ITEM), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_LIST_HOVER), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_pad_ver(btn, 12, 0);
        lv_obj_set_style_border_width(btn, 0, 0);

        // 找到按钮内的label并设置样式
        uint32_t childCnt = lv_obj_get_child_cnt(btn);
        for (uint32_t c = 0; c < childCnt; c++)
        {
            lv_obj_t *child = lv_obj_get_child(btn, c);
            if (lv_obj_check_type(child, &lv_label_class))
            {
                lv_obj_set_style_text_color(child, lv_color_hex(COLOR_TEXT), 0);
                lv_obj_set_style_text_font(child, &lv_font_montserrat_18, 0);
                break;
            }
        }

        // 序号标签
        lv_obj_t *indexLabel = lv_label_create(btn);
        char idxBuf[16];
        snprintf(idxBuf, sizeof(idxBuf), "#%d", i + 1);
        lv_label_set_text(indexLabel, idxBuf);
        lv_obj_set_style_text_color(indexLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_set_style_text_font(indexLabel, &lv_font_montserrat_14, 0);
        lv_obj_align(indexLabel, LV_ALIGN_RIGHT_MID, -10, 0);

        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, _listBtnClickCb, LV_EVENT_CLICKED, this);
    }

    // 底部信息
    lv_obj_t *infoBar = lv_label_create(_mainScreen);
    char infoBuf[64];
    snprintf(infoBuf, sizeof(infoBuf), "Total: %d games | Tap to launch", (int)_games.size());
    lv_label_set_text(infoBar, infoBuf);
    lv_obj_set_style_text_color(infoBar, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_set_style_text_font(infoBar, &lv_font_montserrat_12, 0);
    lv_obj_align(infoBar, LV_ALIGN_BOTTOM_MID, 0, -2);
}

// ===================== 创建轮播视图 =====================
void GameUI::_createCarouselView()
{
    _carouselContainer = lv_obj_create(_mainScreen);
    lv_obj_set_size(_carouselContainer, _scrW, _scrH - 45);
    lv_obj_align(_carouselContainer, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(_carouselContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_carouselContainer, 0, 0);
    lv_obj_set_style_pad_all(_carouselContainer, 0, 0);
    lv_obj_clear_flag(_carouselContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_img_src(_carouselContainer, NULL, 0);
    lv_obj_set_style_bg_img_tiled(_carouselContainer, false, 0);

    // ===== Switch 风格动态背景层：必须先于所有 UI 元素创建 =====
    _bgLayer = lv_obj_create(_carouselContainer);
    lv_obj_set_size(_bgLayer, _scrW, _scrH - 45);
    lv_obj_align(_bgLayer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(_bgLayer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_bgLayer, 0, 0);
    lv_obj_set_style_pad_all(_bgLayer, 0, 0);
    lv_obj_clear_flag(_bgLayer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_bgLayer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_to_index(_bgLayer, 0);

    _bgImgA = lv_img_create(_bgLayer);
    _bgImgB = lv_img_create(_bgLayer);
    lv_obj_clear_flag(_bgImgA, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(_bgImgB, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_bgImgA, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_bgImgB, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_img_opa(_bgImgA, LV_OPA_TRANSP, 0);
    lv_obj_set_style_img_opa(_bgImgB, LV_OPA_TRANSP, 0);

    // Shader 风格暗色遮罩：提升文字可读性，同时让背景更有高级感
    _shaderOverlay = lv_obj_create(_bgLayer);
    lv_obj_set_size(_shaderOverlay, _scrW, _scrH - 45);
    lv_obj_align(_shaderOverlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_shaderOverlay, lv_color_hex(0x000014), 0);
    lv_obj_set_style_bg_opa(_shaderOverlay, LV_OPA_20, 0);
    lv_obj_set_style_border_width(_shaderOverlay, 0, 0);
    lv_obj_set_style_pad_all(_shaderOverlay, 0, 0);
    lv_obj_clear_flag(_shaderOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_shaderOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(_shaderOverlay);

    if (_games.empty())
    {
        lv_obj_t *emptyLabel = lv_label_create(_carouselContainer);
        lv_label_set_text(emptyLabel, LV_SYMBOL_WARNING "\nNo games found!");
        lv_obj_set_style_text_color(emptyLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_set_style_text_font(emptyLabel, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(emptyLabel);
        return;
    }

    // 装饰线
    _decorLine1 = lv_obj_create(_carouselContainer);
    lv_obj_set_size(_decorLine1, _scrW - 60, 2);
    lv_obj_align(_decorLine1, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_bg_color(_decorLine1, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(_decorLine1, LV_OPA_50, 0);
    lv_obj_set_style_radius(_decorLine1, 1, 0);
    lv_obj_set_style_border_width(_decorLine1, 0, 0);

    // 游戏卡片
    _gameCard = lv_obj_create(_carouselContainer);
    lv_obj_set_size(_gameCard, _scrW - 80, 200);
    lv_obj_align(_gameCard, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(_gameCard, lv_color_hex(COLOR_CARD_BG), 0);
    lv_obj_set_style_bg_opa(_gameCard, LV_OPA_90, 0);
    lv_obj_set_style_radius(_gameCard, 20, 0);
    lv_obj_set_style_border_width(_gameCard, 2, 0);
    lv_obj_set_style_border_color(_gameCard, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(_gameCard, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(_gameCard, 30, 0);
    lv_obj_set_style_shadow_color(_gameCard, lv_color_hex(0x000044), 0);
    lv_obj_set_style_shadow_opa(_gameCard, LV_OPA_80, 0);
    lv_obj_clear_flag(_gameCard, LV_OBJ_FLAG_SCROLLABLE);

    // 卡片内图标
    lv_obj_t *iconBg = lv_obj_create(_gameCard);
    lv_obj_set_size(iconBg, 80, 80);
    lv_obj_align(iconBg, LV_ALIGN_LEFT_MID, 15, 0);
    lv_obj_set_style_bg_color(iconBg, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(iconBg, LV_OPA_20, 0);
    lv_obj_set_style_radius(iconBg, 15, 0);
    lv_obj_set_style_border_width(iconBg, 1, 0);
    lv_obj_set_style_border_color(iconBg, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(iconBg, LV_OPA_40, 0);
    lv_obj_clear_flag(iconBg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *iconLabel = lv_label_create(iconBg);
    lv_label_set_text(iconLabel, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(iconLabel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(iconLabel);

    // 游戏名称
    _gameNameLabel = lv_label_create(_gameCard);
    lv_obj_set_style_text_font(_gameNameLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_gameNameLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(_gameNameLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(_gameNameLabel, _scrW - 220);
    lv_label_set_long_mode(_gameNameLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_gameNameLabel, LV_ALIGN_LEFT_MID, 115, -15);

    // 文件路径
    lv_obj_t *pathLabel = lv_label_create(_gameCard);
    lv_obj_set_style_text_font(pathLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pathLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_set_width(pathLabel, _scrW - 220);
    lv_label_set_long_mode(pathLabel, LV_LABEL_LONG_DOT);
    lv_obj_align(pathLabel, LV_ALIGN_LEFT_MID, 115, 25);
    lv_obj_set_user_data(_gameCard, (void *)pathLabel);

    // 左右箭头
    lv_obj_t *btnLeft = lv_btn_create(_carouselContainer);
    lv_obj_set_size(btnLeft, 40, 60);
    lv_obj_set_style_radius(btnLeft, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btnLeft, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(btnLeft, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btnLeft, 0, 0);
    lv_obj_align(btnLeft, LV_ALIGN_LEFT_MID, 5, -20);
    lv_obj_add_event_cb(btnLeft, _carouselNavCb, LV_EVENT_CLICKED, &_leftNavData);

    lv_obj_t *leftLabel = lv_label_create(btnLeft);
    lv_label_set_text(leftLabel, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(leftLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(leftLabel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(leftLabel);

    // 右箭头
    lv_obj_t *btnRight = lv_btn_create(_carouselContainer);
    lv_obj_set_size(btnRight, 40, 60);
    lv_obj_set_style_radius(btnRight, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btnRight, lv_color_hex(0x202020), 0);
    lv_obj_set_style_bg_opa(btnRight, LV_OPA_40, 0);
    lv_obj_set_style_border_width(btnRight, 0, 0);
    lv_obj_align(btnRight, LV_ALIGN_RIGHT_MID, -5, -20);
    lv_obj_add_event_cb(btnRight, _carouselNavCb, LV_EVENT_CLICKED, &_rightNavData);

    lv_obj_t *rightLabel = lv_label_create(btnRight);
    lv_label_set_text(rightLabel, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(rightLabel, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(rightLabel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_center(rightLabel);

    // 序号
    _gameIndexLabel = lv_label_create(_carouselContainer);
    lv_obj_set_style_text_font(_gameIndexLabel, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(_gameIndexLabel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(_gameIndexLabel, LV_ALIGN_BOTTOM_MID, 0, -60);

    _gameTotalLabel = lv_label_create(_carouselContainer);
    lv_obj_set_style_text_font(_gameTotalLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_gameTotalLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(_gameTotalLabel, LV_ALIGN_BOTTOM_MID, 0, -42);

    // 进度点
    _progressDots = lv_obj_create(_carouselContainer);
    lv_obj_set_size(_progressDots, _scrW - 100, 12);
    lv_obj_align(_progressDots, LV_ALIGN_BOTTOM_MID, 0, -25);
    lv_obj_set_style_bg_opa(_progressDots, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_progressDots, 0, 0);
    lv_obj_set_style_pad_all(_progressDots, 0, 0);
    lv_obj_set_flex_flow(_progressDots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_progressDots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(_progressDots, LV_OBJ_FLAG_SCROLLABLE);

    int dotCount = min((int)_games.size(), 15);
    for (int i = 0; i < dotCount; i++)
    {
        lv_obj_t *dot = lv_obj_create(_progressDots);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_TEXT_DIM), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_40, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }

    // 装饰线2
    _decorLine2 = lv_obj_create(_carouselContainer);
    lv_obj_set_size(_decorLine2, _scrW - 60, 2);
    lv_obj_align(_decorLine2, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_set_style_bg_color(_decorLine2, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(_decorLine2, LV_OPA_50, 0);
    lv_obj_set_style_radius(_decorLine2, 1, 0);
    lv_obj_set_style_border_width(_decorLine2, 0, 0);

    // 操作提示
    _instructionLabel = lv_label_create(_carouselContainer);
    lv_label_set_text(_instructionLabel, LV_SYMBOL_SETTINGS "  SELECT=Launch  |  " LV_SYMBOL_LEFT LV_SYMBOL_RIGHT "=Navigate  |  START=List");
    lv_obj_set_style_text_font(_instructionLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_instructionLabel, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(_instructionLabel, LV_ALIGN_BOTTOM_MID, 0, -2);

    // 初始显示
    _updateCarouselDisplay(false);
}

// ===================== 销毁当前视图 =====================
void GameUI::_destroyCurrentView()
{
    if (_listContainer)
    {
        lv_obj_del(_listContainer);
        _listContainer = nullptr;
        _listView = nullptr;
    }
    if (_carouselContainer)
    {
        lv_obj_del(_carouselContainer);
        _carouselContainer = nullptr;

        _bgImg = nullptr;
        _bgLayer = nullptr;
        _bgImgA = nullptr;
        _bgImgB = nullptr;
        _shaderOverlay = nullptr;
        _bgUsingA = true;
        _bgPathA[0] = ' ';
        _bgPathB[0] = ' ';

        _gameCard = nullptr;
        _gameNameLabel = nullptr;
        _gameIndexLabel = nullptr;
        _gameTotalLabel = nullptr;
        _instructionLabel = nullptr;
        _progressDots = nullptr;
        _decorLine1 = nullptr;
        _decorLine2 = nullptr;
    }
    if (_viewModeBtn)
    {
        lv_obj_del(_viewModeBtn);
        _viewModeBtn = nullptr;
    }
    if (_settingsBtn)
    {
        lv_obj_del(_settingsBtn);
        _settingsBtn = nullptr;
    }
    if (_batteryLabel)
    {
        lv_obj_del(_batteryLabel);
        _batteryLabel = nullptr;
    }
    if (_gameSearcher)
    {
        lv_obj_del(_gameSearcher);
        _gameSearcher = nullptr;
    }
    if (_searchOverlay)
    {
        lv_obj_del(_searchOverlay);
        _searchOverlay = nullptr;
        _searchResultList = nullptr;
        _searchTextArea = nullptr;
        _searchKeyboard = nullptr;
    }
    if (_settingsOverlay)
    {
        lv_obj_del(_settingsOverlay);
        _settingsOverlay = nullptr;
        _brightnessSlider = nullptr;
        _volumeSlider = nullptr;
    }
    if (_headerLabel)
    {
        lv_obj_del(_headerLabel);
        _headerLabel = nullptr;
    }
    // 清理可能残留的底部info标签等
    // 直接清屏重建更安全
    lv_obj_clean(lv_scr_act());
}

// ===================== 背景图片路径构建 =====================
void GameUI::_buildGameImagePath(const GameInfo &game, char *outLvglPath, size_t outSize)
{
    if (!outLvglPath || outSize == 0)
        return;

    const char *imgName = game.imgPath.c_str();
    if (imgName == nullptr || imgName[0] == '\0')
    {
        imgName = "default.png";
    }

    char sdPath[128];
    snprintf(sdPath, sizeof(sdPath), "/game/img/%s", imgName);

    // SD.exists 要用真实 SD 路径；LVGL 显示图片才用 S: 路径
    if (!SD.exists(sdPath))
    {
        imgName = "default.png";
    }

    snprintf(outLvglPath, outSize, "S:/game/img/%s", imgName);
}

// ===================== 背景图 Cover 缩放，不平铺 =====================
void GameUI::_applyBgImageCover(lv_obj_t *img,
                                const char *lvglPath,
                                lv_opa_t opa,
                                int yOffset,
                                uint16_t extraZoom)
{
    if (!img || !lvglPath)
        return;

    lv_img_set_src(img, lvglPath);

#if LVGL_VERSION_MAJOR >= 8
    lv_img_set_size_mode(img, LV_IMG_SIZE_MODE_REAL);
#endif

    lv_img_set_antialias(img, true);

    lv_img_header_t header;
    lv_res_t res = lv_img_decoder_get_info(lvglPath, &header);

    int targetW = _scrW;
    int targetH = _scrH - 45;
    uint32_t zoom = 256;

    if (res == LV_RES_OK && header.w > 0 && header.h > 0)
    {
        uint32_t zoomX = ((uint32_t)targetW * 256U) / header.w;
        uint32_t zoomY = ((uint32_t)targetH * 256U) / header.h;

        // cover：铺满容器，可能裁剪边缘，但不会留黑边，也不会平铺
        zoom = (zoomX > zoomY) ? zoomX : zoomY;
        zoom += extraZoom;
        if (zoom < 1)
            zoom = 1;
    }

    lv_img_set_zoom(img, zoom);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, yOffset);
    lv_obj_set_style_img_opa(img, opa, 0);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);

    Serial.printf("[GameUI] BG cover: %s, zoom=%lu, opa=%d\n",
                  lvglPath,
                  (unsigned long)zoom,
                  (int)opa);
}

// ===================== 卡片惯性滑入 + 弹性回弹 =====================
void GameUI::_animateCardEnter(int dir)
{
    if (!_gameCard)
        return;

    lv_anim_del(_gameCard, NULL);

    // 每次开始前都恢复干净状态，避免 opacity / translate 残留
    lv_obj_set_style_translate_x(_gameCard, 0, 0);
    lv_obj_set_style_translate_y(_gameCard, 0, 0);
    lv_obj_set_style_opa(_gameCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(_gameCard, LV_OPA_60, 0);

#if LVGL_VERSION_MAJOR >= 8
    lv_obj_set_style_transform_zoom(_gameCard, 256, 0);
#endif

    uint32_t now = lv_tick_get();
    bool allowAnim = (now - _lastCarouselAnimTick > 80);
    _lastCarouselAnimTick = now;
    if (!allowAnim)
        return;

    int startY = dir * 38;
    int startX = dir * 10;

    lv_obj_set_style_translate_y(_gameCard, startY, 0);
    lv_obj_set_style_translate_x(_gameCard, startX, 0);
    lv_obj_set_style_opa(_gameCard, LV_OPA_40, 0);

#if LVGL_VERSION_MAJOR >= 8
    lv_obj_set_style_transform_zoom(_gameCard, 246, 0);
#endif

    lv_anim_t ay;
    lv_anim_init(&ay);
    lv_anim_set_var(&ay, _gameCard);
    lv_anim_set_values(&ay, startY, 0);
    lv_anim_set_time(&ay, 180);
    lv_anim_set_exec_cb(&ay, [](void *obj, int32_t v)
                        { lv_obj_set_style_translate_y((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&ay, lv_anim_path_overshoot);
    lv_anim_start(&ay);

    lv_anim_t ax;
    lv_anim_init(&ax);
    lv_anim_set_var(&ax, _gameCard);
    lv_anim_set_values(&ax, startX, 0);
    lv_anim_set_time(&ax, 160);
    lv_anim_set_exec_cb(&ax, [](void *obj, int32_t v)
                        { lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
    lv_anim_start(&ax);

    lv_anim_t ao;
    lv_anim_init(&ao);
    lv_anim_set_var(&ao, _gameCard);
    lv_anim_set_values(&ao, LV_OPA_40, LV_OPA_COVER);
    lv_anim_set_time(&ao, 150);
    lv_anim_set_exec_cb(&ao, [](void *obj, int32_t v)
                        { lv_obj_set_style_opa((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&ao, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&ao, [](lv_anim_t *a)
                         {
                             lv_obj_t *card = (lv_obj_t *)a->var;
                             if (!card)
                                 return;
                             lv_obj_set_style_translate_x(card, 0, 0);
                             lv_obj_set_style_translate_y(card, 0, 0);
                             lv_obj_set_style_opa(card, LV_OPA_COVER, 0);
                             lv_obj_set_style_border_opa(card, LV_OPA_60, 0);
#if LVGL_VERSION_MAJOR >= 8
                             lv_obj_set_style_transform_zoom(card, 256, 0);
#endif
                         });
    lv_anim_start(&ao);

#if LVGL_VERSION_MAJOR >= 8
    lv_anim_t az;
    lv_anim_init(&az);
    lv_anim_set_var(&az, _gameCard);
    lv_anim_set_values(&az, 246, 256);
    lv_anim_set_time(&az, 180);
    lv_anim_set_exec_cb(&az, [](void *obj, int32_t v)
                        { lv_obj_set_style_transform_zoom((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&az, lv_anim_path_overshoot);
    lv_anim_start(&az);
#endif

    lv_anim_t ab;
    lv_anim_init(&ab);
    lv_anim_set_var(&ab, _gameCard);
    lv_anim_set_values(&ab, LV_OPA_COVER, LV_OPA_60);
    lv_anim_set_time(&ab, 260);
    lv_anim_set_exec_cb(&ab, [](void *obj, int32_t v)
                        { lv_obj_set_style_border_opa((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&ab, lv_anim_path_ease_out);
    lv_anim_start(&ab);
}

// ===================== 双背景层交叉淡入 + 跟随滑动 =====================
void GameUI::_animateBackgroundTo(const char *lvglPath, int dir, bool animate)
{
    if (!_bgImgA || !_bgImgB || !lvglPath)
        return;

    lv_obj_t *oldImg = _bgUsingA ? _bgImgA : _bgImgB;
    lv_obj_t *newImg = _bgUsingA ? _bgImgB : _bgImgA;
    char *targetBuf = _bgUsingA ? _bgPathB : _bgPathA;

    strncpy(targetBuf, lvglPath, 127);
    targetBuf[127] = '\0';

    lv_anim_del(oldImg, NULL);
    lv_anim_del(newImg, NULL);
    if (_shaderOverlay)
        lv_anim_del(_shaderOverlay, NULL);

    // 约 35% 透明度，背景存在感够，但不抢文字
    const lv_opa_t BG_FINAL_OPA = 120;

    if (!animate)
    {
        _applyBgImageCover(newImg, targetBuf, BG_FINAL_OPA, 0, 12);
        lv_obj_add_flag(oldImg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_to_index(newImg, 0);
        if (_shaderOverlay)
            lv_obj_move_foreground(_shaderOverlay);
        _bgUsingA = !_bgUsingA;
        return;
    }

    _applyBgImageCover(newImg, targetBuf, LV_OPA_TRANSP, dir * 60, 18);
    lv_obj_move_to_index(oldImg, 0);
    lv_obj_move_to_index(newImg, 1);
    if (_shaderOverlay)
        lv_obj_move_foreground(_shaderOverlay);

    lv_anim_t ainOpa;
    lv_anim_init(&ainOpa);
    lv_anim_set_var(&ainOpa, newImg);
    lv_anim_set_values(&ainOpa, LV_OPA_TRANSP, BG_FINAL_OPA);
    lv_anim_set_time(&ainOpa, 220);
    lv_anim_set_exec_cb(&ainOpa, [](void *obj, int32_t v)
                        { lv_obj_set_style_img_opa((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&ainOpa, lv_anim_path_ease_out);
    lv_anim_start(&ainOpa);

    lv_anim_t ainY;
    lv_anim_init(&ainY);
    lv_anim_set_var(&ainY, newImg);
    lv_anim_set_values(&ainY, dir * 60, 0);
    lv_anim_set_time(&ainY, 240);
    lv_anim_set_exec_cb(&ainY, [](void *obj, int32_t v)
                        { lv_obj_align((lv_obj_t *)obj, LV_ALIGN_CENTER, 0, v); });
    lv_anim_set_path_cb(&ainY, lv_anim_path_ease_out);
    lv_anim_start(&ainY);

    lv_anim_t aoutOpa;
    lv_anim_init(&aoutOpa);
    lv_anim_set_var(&aoutOpa, oldImg);
    lv_anim_set_values(&aoutOpa, BG_FINAL_OPA, LV_OPA_TRANSP);
    lv_anim_set_time(&aoutOpa, 180);
    lv_anim_set_exec_cb(&aoutOpa, [](void *obj, int32_t v)
                        { lv_obj_set_style_img_opa((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&aoutOpa, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&aoutOpa, [](lv_anim_t *a)
                         {
        lv_obj_t* img = (lv_obj_t*)a->var;
        if (img)
            lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN); });
    lv_anim_start(&aoutOpa);

    lv_anim_t aoutY;
    lv_anim_init(&aoutY);
    lv_anim_set_var(&aoutY, oldImg);
    lv_anim_set_values(&aoutY, 0, -dir * 40);
    lv_anim_set_time(&aoutY, 180);
    lv_anim_set_exec_cb(&aoutY, [](void *obj, int32_t v)
                        { lv_obj_align((lv_obj_t *)obj, LV_ALIGN_CENTER, 0, v); });
    lv_anim_set_path_cb(&aoutY, lv_anim_path_ease_in);
    lv_anim_start(&aoutY);

    // shader 风格：切换瞬间稍暗，然后回落，类似高级 UI 的 cinematic fade
    if (_shaderOverlay)
    {
        lv_obj_set_style_bg_opa(_shaderOverlay, LV_OPA_70, 0);
        lv_anim_t ashader;
        lv_anim_init(&ashader);
        lv_anim_set_var(&ashader, _shaderOverlay);
        lv_anim_set_values(&ashader, LV_OPA_70, LV_OPA_50);
        lv_anim_set_time(&ashader, 260);
        lv_anim_set_exec_cb(&ashader, [](void *obj, int32_t v)
                            { lv_obj_set_style_bg_opa((lv_obj_t *)obj, v, 0); });
        lv_anim_set_path_cb(&ashader, lv_anim_path_ease_out);
        lv_anim_start(&ashader);
    }

    _bgUsingA = !_bgUsingA;
}

// ===================== 更新轮播显示 =====================
void GameUI::_updateCarouselDisplay(bool animate)
{
    if (_games.empty() || !_gameCard)
        return;

    const GameInfo &game = _games[_selectedIndex];

    int dir = _lastNavDir;
    if (dir == 0)
        dir = 1;

    // 先处理背景图片。PNG 从 SD 读取/解码会阻塞 UI，
    // 所以不要在卡片动画启动后再 lv_img_set_src，否则动画容易卡在半透明状态。
    char lvglPath[128];
    _buildGameImagePath(game, lvglPath, sizeof(lvglPath));
    _animateBackgroundTo(lvglPath, dir, false);

    // 更新名称
    lv_label_set_text(_gameNameLabel, game.name.c_str());

    // 更新路径
    lv_obj_t *pathLabel = (lv_obj_t *)lv_obj_get_user_data(_gameCard);
    if (pathLabel)
    {
        lv_label_set_text(pathLabel, game.fullPath.c_str());
    }

    // 最后做卡片动画，避免被图片解码打断
    if (animate)
    {
        _animateCardEnter(dir);
    }
    else
    {
        lv_anim_del(_gameCard, NULL);
        lv_obj_set_style_translate_x(_gameCard, 0, 0);
        lv_obj_set_style_translate_y(_gameCard, 0, 0);
        lv_obj_set_style_opa(_gameCard, LV_OPA_COVER, 0);
        lv_obj_set_style_border_opa(_gameCard, LV_OPA_60, 0);
#if LVGL_VERSION_MAJOR >= 8
        lv_obj_set_style_transform_zoom(_gameCard, 256, 0);
#endif
    }

    // 更新序号
    _updateInfoLabels();

    // 更新进度点
    if (_progressDots)
    {
        int dotCount = lv_obj_get_child_cnt(_progressDots);
        int totalGames = (int)_games.size();

        for (int i = 0; i < dotCount; i++)
        {
            lv_obj_t *dot = lv_obj_get_child(_progressDots, i);
            if (!dot)
                continue;

            int mappedIndex;
            if (totalGames <= 15)
            {
                mappedIndex = i;
            }
            else
            {
                mappedIndex = (int)((float)i / 14.0f * (totalGames - 1) + 0.5f);
            }

            bool isActive = (mappedIndex == _selectedIndex) ||
                            (totalGames > 15 && abs(mappedIndex - _selectedIndex) <= (totalGames / 30 + 1));

            if (isActive)
            {
                lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_ACCENT), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                lv_obj_set_size(dot, 12, 8);
            }
            else
            {
                lv_obj_set_style_bg_color(dot, lv_color_hex(COLOR_TEXT_DIM), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_40, 0);
                lv_obj_set_size(dot, 8, 8);
            }

            lv_obj_set_style_radius(dot, 4, 0);
        }
    }
}

// ===================== 更新信息标签 =====================
void GameUI::_updateInfoLabels()
{
    if (!_gameIndexLabel || !_gameTotalLabel)
        return;

    char idxBuf[32];
    snprintf(idxBuf, sizeof(idxBuf), "%d / %d", _selectedIndex + 1, (int)_games.size());
    lv_label_set_text(_gameIndexLabel, idxBuf);

    char totalBuf[48];
    snprintf(totalBuf, sizeof(totalBuf), "Total %d NES Games", (int)_games.size());
    lv_label_set_text(_gameTotalLabel, totalBuf);
}

// ===================== 启动动画 =====================
void GameUI::_playLaunchAnimation()
{
    if (_games.empty())
        return;
    _state = UI_LAUNCHING;

    // 全屏遮罩
    _launchOverlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_launchOverlay, _scrW, _scrH);
    lv_obj_set_pos(_launchOverlay, 0, 0);
    lv_obj_set_style_bg_color(_launchOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_launchOverlay, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_launchOverlay, 0, 0);
    lv_obj_clear_flag(_launchOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(_launchOverlay);

    // LOADING文字
    lv_obj_t *launchTitle = lv_label_create(_launchOverlay);
    lv_label_set_text(launchTitle, LV_SYMBOL_PLAY "  LOADING...");
    lv_obj_set_style_text_font(launchTitle, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(launchTitle, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(launchTitle, LV_ALIGN_CENTER, 0, -30);

    // 游戏名
    lv_obj_t *launchName = lv_label_create(_launchOverlay);
    lv_label_set_text(launchName, _games[_selectedIndex].name.c_str());
    lv_obj_set_style_text_font(launchName, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(launchName, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(launchName, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(launchName, _scrW - 60);
    lv_label_set_long_mode(launchName, LV_LABEL_LONG_WRAP);
    lv_obj_align(launchName, LV_ALIGN_CENTER, 0, 20);

    // 进度条
    lv_obj_t *loadBar = lv_bar_create(_launchOverlay);
    lv_obj_set_size(loadBar, _scrW - 100, 8);
    lv_obj_align(loadBar, LV_ALIGN_CENTER, 0, 70);
    lv_obj_set_style_bg_color(loadBar, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(loadBar, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(loadBar, 4, 0);
    lv_obj_set_style_radius(loadBar, 4, LV_PART_INDICATOR);
    lv_bar_set_value(loadBar, 0, LV_ANIM_OFF);

    // 遮罩渐入
    lv_anim_t a_fade;
    lv_anim_init(&a_fade);
    lv_anim_set_var(&a_fade, _launchOverlay);
    lv_anim_set_values(&a_fade, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_time(&a_fade, 400);
    lv_anim_set_exec_cb(&a_fade, [](void *obj, int32_t v)
                        { lv_obj_set_style_bg_opa((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&a_fade, lv_anim_path_ease_in);
    lv_anim_start(&a_fade);

    // 进度条动画
    lv_anim_t a_bar;
    lv_anim_init(&a_bar);
    lv_anim_set_var(&a_bar, loadBar);
    lv_anim_set_values(&a_bar, 0, 100);
    lv_anim_set_time(&a_bar, 1200);
    lv_anim_set_delay(&a_bar, 400);
    lv_anim_set_exec_cb(&a_bar, [](void *obj, int32_t v)
                        { lv_bar_set_value((lv_obj_t *)obj, v, LV_ANIM_OFF); });
    lv_anim_set_path_cb(&a_bar, lv_anim_path_ease_in_out);
    lv_anim_set_user_data(&a_bar, this);
    lv_anim_set_ready_cb(&a_bar, _launchAnimReady);
    lv_anim_start(&a_bar);

    // 文字脉冲
    lv_anim_t a_pulse;
    lv_anim_init(&a_pulse);
    lv_anim_set_var(&a_pulse, launchTitle);
    lv_anim_set_values(&a_pulse, 100, 255);
    lv_anim_set_time(&a_pulse, 500);
    lv_anim_set_delay(&a_pulse, 300);
    lv_anim_set_exec_cb(&a_pulse, [](void *obj, int32_t v)
                        { lv_obj_set_style_text_opa((lv_obj_t *)obj, v, 0); });
    lv_anim_set_path_cb(&a_pulse, lv_anim_path_ease_in_out);
    lv_anim_set_playback_time(&a_pulse, 500);
    lv_anim_set_repeat_count(&a_pulse, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a_pulse);
}

// ===================== 启动动画完成 =====================
void GameUI::_launchAnimReady(lv_anim_t *a)
{
    GameUI *self = (GameUI *)a->user_data;
    if (!self)
        return;

    Serial.println("[GameUI] Launch animation complete");
    self->_state = UI_GAME_RUNNING;

    if (self->_launchCb && !self->_games.empty())
    {
        self->_launchCb(self->_games[self->_selectedIndex].fullPath);
    }
}

// ===================== 切换视图模式 =====================
void GameUI::switchViewMode()
{
    if (_state != UI_BROWSING)
        return;
    audioPlayer.playPCM(g_selectPCM, g_selectSize, g_settingVolume);
    _viewMode = (_viewMode == VIEW_LIST) ? VIEW_CAROUSEL : VIEW_LIST;
    Serial.printf("[GameUI] Switch to: %s\n", _viewMode == VIEW_LIST ? "LIST" : "CAROUSEL");

    _destroyCurrentView();
    show();
}

// ===================== 导航 =====================
void GameUI::navigateUp()
{
    if (_state != UI_BROWSING || _games.empty())
        return;
    if (_viewMode != VIEW_CAROUSEL)
        return;

    _selectedIndex--;
    if (_selectedIndex < 0)
        _selectedIndex = (int)_games.size() - 1;
    _lastNavDir = -1;
    audioPlayer.playPCM(g_selectPCM, g_selectSize, g_settingVolume);
    _updateCarouselDisplay(true);
    Serial.printf("[GameUI] UP -> %d: %s\n", _selectedIndex, _games[_selectedIndex].name.c_str());
}

void GameUI::navigateDown()
{
    if (_state != UI_BROWSING || _games.empty())
        return;
    if (_viewMode != VIEW_CAROUSEL)
        return;

    _selectedIndex++;
    if (_selectedIndex >= (int)_games.size())
        _selectedIndex = 0;
    _lastNavDir = 1;
    audioPlayer.playPCM(g_selectPCM, g_selectSize, g_settingVolume);
    _updateCarouselDisplay(true);
    Serial.printf("[GameUI] DOWN -> %d: %s\n", _selectedIndex, _games[_selectedIndex].name.c_str());
}

void GameUI::navigateLeft()
{
    navigateUp();
}

void GameUI::navigateRight()
{
    navigateDown();
}

// ===================== 选中游戏 =====================
void GameUI::selectCurrentGame()
{
    if (_state != UI_BROWSING || _games.empty())
        return;

    audioPlayer.playPCM(g_startPCM, g_startSize, g_settingVolume);

    Serial.printf("[GameUI] SELECT: %s (%s)\n",
                  _games[_selectedIndex].name.c_str(),
                  _games[_selectedIndex].fullPath.c_str());
    _playLaunchAnimation();
}

// ===================== 获取选中游戏 =====================
const GameInfo *GameUI::getSelectedGame() const
{
    if (_games.empty())
        return nullptr;
    return &_games[_selectedIndex];
}

// ===================== 列表按钮点击 =====================
void GameUI::_listBtnClickCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    int index = (int)(intptr_t)lv_obj_get_user_data(btn);

    if (self && index >= 0 && index < (int)self->_games.size())
    {
        self->_selectedIndex = index;
        audioPlayer.playPCM(g_startPCM, g_startSize, g_settingVolume);
        Serial.printf("[GameUI] List click: %d - %s\n", index, self->_games[index].name.c_str());
        self->_playLaunchAnimation();
    }
}

// ===================== 视图切换按钮 =====================
void GameUI::_switchModeBtnCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (self)
    {
        self->switchViewMode();
    }
}
void GameUI::_searchGameCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (!self || self->_searchOverlay)
        return;

    if (self->_bgLayer)
        lv_obj_add_flag(self->_bgLayer, LV_OBJ_FLAG_HIDDEN);

    self->_searchOverlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(self->_searchOverlay, self->_scrW, self->_scrH);
    lv_obj_set_pos(self->_searchOverlay, 0, 0);
    lv_obj_set_style_bg_color(self->_searchOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(self->_searchOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(self->_searchOverlay, 0, 0);
    lv_obj_clear_flag(self->_searchOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(self->_searchOverlay, _searchCloseCb, LV_EVENT_CLICKED, self);

    lv_obj_t *card = lv_obj_create(self->_searchOverlay);
    lv_obj_set_size(card, 500, 340);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 15);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD_BG), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    self->_searchTextArea = lv_textarea_create(card);
    lv_obj_set_size(self->_searchTextArea, 400, 40);
    lv_obj_align(self->_searchTextArea, LV_ALIGN_TOP_MID, 0, 10);
    lv_textarea_set_placeholder_text(self->_searchTextArea, "Search games...");
    lv_obj_set_style_text_font(self->_searchTextArea, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(self->_searchTextArea, lv_color_hex(0x1A1A3E), 0);
    lv_obj_set_style_text_color(self->_searchTextArea, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_color(self->_searchTextArea, lv_color_hex(COLOR_ACCENT), 0);

    self->_searchKeyboard = lv_keyboard_create(self->_searchOverlay);
    lv_keyboard_set_textarea(self->_searchKeyboard, self->_searchTextArea);

    self->_searchResultList = lv_list_create(card);
    lv_obj_set_size(self->_searchResultList, 460, 220);
    lv_obj_align(self->_searchResultList, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(self->_searchResultList, lv_color_hex(COLOR_LIST_BG), 0);
    lv_obj_set_style_border_width(self->_searchResultList, 0, 0);

    lv_obj_add_event_cb(self->_searchTextArea, _searchResultCb, LV_EVENT_VALUE_CHANGED, self);

    // 滑动结果列表 → 隐藏键盘；点输入框 → 显示键盘
    lv_obj_add_event_cb(self->_searchResultList, _searchScrollCb, LV_EVENT_SCROLL, self);
    lv_obj_add_event_cb(self->_searchTextArea, _searchFocusCb, LV_EVENT_CLICKED, self);

    _searchResultCb(e);
}

void GameUI::_searchResultCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (!self || !self->_searchResultList)
        return;
    const char *query = lv_textarea_get_text(self->_searchTextArea);
    lv_obj_clean(self->_searchResultList);
    for (int i = 0; i < (int)self->_games.size(); i++)
    {
        String name = self->_games[i].name;
        name.toLowerCase();
        String q = String(query);
        q.toLowerCase();
        if (q.length() == 0 || name.indexOf(q) >= 0)
        {
            lv_obj_t *btn = lv_list_add_btn(self->_searchResultList, LV_SYMBOL_FILE, self->_games[i].name.c_str());
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_LIST_ITEM), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_LIST_HOVER), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 6, 0);
            lv_obj_set_style_pad_ver(btn, 8, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            uint32_t cc = lv_obj_get_child_cnt(btn);
            for (uint32_t c = 0; c < cc; c++)
            {
                lv_obj_t *ch = lv_obj_get_child(btn, c);
                if (lv_obj_check_type(ch, &lv_label_class))
                {
                    lv_obj_set_style_text_color(ch, lv_color_hex(COLOR_TEXT), 0);
                    lv_obj_set_style_text_font(ch, &lv_font_montserrat_16, 0);
                    break;
                }
            }
            lv_obj_set_user_data(btn, (void *)(intptr_t)i);
            lv_obj_add_event_cb(btn, _searchLaunchCb, LV_EVENT_CLICKED, self);
        }
    }
}
void GameUI::_searchCloseCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (self && self->_searchOverlay)
    {
        lv_obj_del(self->_searchOverlay);
        self->_searchOverlay = nullptr;
        self->_searchTextArea = nullptr;
        self->_searchResultList = nullptr;
    }

    if (self->_bgLayer)
        lv_obj_clear_flag(self->_bgLayer, LV_OBJ_FLAG_HIDDEN);
}
void GameUI::_searchLaunchCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (self && idx >= 0 && idx < (int)self->_games.size())
    {
        self->_selectedIndex = idx;
        self->_searchCloseCb(e);
        self->_playLaunchAnimation();
    }
}
void GameUI::_searchScrollCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (self && self->_searchKeyboard)
        lv_obj_add_flag(self->_searchKeyboard, LV_OBJ_FLAG_HIDDEN);
}
void GameUI::_searchFocusCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (self && self->_searchKeyboard)
        lv_obj_clear_flag(self->_searchKeyboard, LV_OBJ_FLAG_HIDDEN);
}

// ===================== 处理按键输入 =====================
void GameUI::handleInput(uint8_t btnUp, uint8_t btnDown, uint8_t btnLeft,
                         uint8_t btnRight, uint8_t btnSelect)
{
    if (_state != UI_BROWSING)
        return;

    unsigned long now = millis();
    if (now - _lastInputTime < INPUT_DEBOUNCE)
        return;

    bool pressed = false;

    if (btnUp == LOW)
    {
        navigateUp();
        pressed = true;
    }
    else if (btnDown == LOW)
    {
        navigateDown();
        pressed = true;
    }
    else if (btnLeft == LOW)
    {
        navigateLeft();
        pressed = true;
    }
    else if (btnRight == LOW)
    {
        navigateRight();
        pressed = true;
    }
    else if (btnSelect == LOW)
    {
        selectCurrentGame();
        pressed = true;
    }

    if (pressed)
    {
        _lastInputTime = now;
    }
}

// ===================== 截断名称 =====================
String GameUI::_truncateGameName(const String &name, int maxChars)
{
    if ((int)name.length() <= maxChars)
        return name;
    return name.substring(0, maxChars - 3) + "...";
}

// ===================== 更新循环 =====================
void GameUI::update()
{
    // 电池指示器：每5秒检测一次
    if (_batteryLabel)
    {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 5000)
        {
            lastCheck = millis();
            int raw = analogRead(17); // BAT_ADC, ~1:3 divider
            uint16_t mv = raw * 10000 / 4095;
            int pct = (mv > 1000) ? constrain(map(mv, 3000, 4200, 0, 100), 0, 100) : -1;
            // Serial.printf("raw=%d mv=%d pct=%d\n", raw, mv, pct);
            if (pct >= 0)
            {
                lv_obj_set_style_text_color(_batteryLabel,
                                            lv_color_hex(pct > 20 ? 0xFFFFFF : 0xFF4444), 0);

                static char buf[16];
                sprintf(buf, "%u", mv);
                // ets_printf("[BAT] now: %d\n", mv);
                lv_label_set_text(_batteryLabel,
                                  pct > 20 ? pct > 50 ? LV_SYMBOL_BATTERY_FULL : LV_SYMBOL_BATTERY_2 : LV_SYMBOL_BATTERY_EMPTY);
            }
        }
    }
}

// ===================== 设置面板 =====================
void GameUI::showSettings()
{
    if (_settingsOverlay)
        return; // already open

    if (_bgLayer)
        lv_obj_add_flag(_bgLayer, LV_OBJ_FLAG_HIDDEN);

    _createSettingsPanel();
}

void GameUI::hideSettings()
{
    if (_settingsOverlay)
    {
        lv_obj_del(_settingsOverlay);
        _settingsOverlay = nullptr;
        _brightnessSlider = nullptr;
        _volumeSlider = nullptr;
    }

    if (_bgLayer)
        lv_obj_clear_flag(_bgLayer, LV_OBJ_FLAG_HIDDEN);
}

void GameUI::_createSettingsPanel()
{
    // 半透明遮罩
    _settingsOverlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_settingsOverlay, _scrW, _scrH);
    lv_obj_set_pos(_settingsOverlay, 0, 0);
    lv_obj_set_style_bg_color(_settingsOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(_settingsOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(_settingsOverlay, 0, 0);
    lv_obj_clear_flag(_settingsOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(_settingsOverlay, _settingsCloseCb, LV_EVENT_CLICKED, this);

    // 设置卡片
    lv_obj_t *card = lv_obj_create(_settingsOverlay);
    lv_obj_set_size(card, 380, 300);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD_BG), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_shadow_width(card, 30, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000044), 0);
    lv_obj_set_scroll_dir(card, LV_DIR_VER); // 可上下滑动显示不全的项目

    // 标题
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // 关闭按钮
    lv_obj_t *closeBtn = lv_btn_create(card);
    lv_obj_set_size(closeBtn, 30, 30);
    lv_obj_align(closeBtn, LV_ALIGN_TOP_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(closeBtn, lv_color_hex(0x333366), 0);
    lv_obj_set_style_radius(closeBtn, 15, 0);
    lv_obj_set_style_shadow_width(closeBtn, 0, 0);
    lv_obj_set_style_border_width(closeBtn, 0, 0);
    lv_obj_add_event_cb(closeBtn, _settingsCloseCb, LV_EVENT_CLICKED, this);
    lv_obj_t *xLbl = lv_label_create(closeBtn);
    lv_label_set_text(xLbl, LV_SYMBOL_CLOSE);
    lv_obj_center(xLbl);

    // ---- 亮度滑块 ----
    lv_obj_t *brightLabel = lv_label_create(card);
    lv_label_set_text(brightLabel, LV_SYMBOL_EYE_OPEN " Brightness");
    lv_obj_set_style_text_font(brightLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(brightLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(brightLabel, LV_ALIGN_TOP_LEFT, 30, 60);

    _brightnessSlider = lv_slider_create(card);
    lv_obj_set_size(_brightnessSlider, 300, 15);
    lv_obj_align(_brightnessSlider, LV_ALIGN_TOP_LEFT, 30, 88);
    lv_slider_set_range(_brightnessSlider, 25, 255);
    lv_slider_set_value(_brightnessSlider, g_settingBrightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_brightnessSlider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_brightnessSlider, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_radius(_brightnessSlider, 5, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_brightnessSlider, 5, LV_PART_MAIN);
    lv_obj_add_event_cb(_brightnessSlider, _brightnessCb, LV_EVENT_VALUE_CHANGED, this);

    // ---- 音量滑块 ----
    lv_obj_t *volLabel = lv_label_create(card);
    lv_label_set_text(volLabel, LV_SYMBOL_VOLUME_MID " Volume");
    lv_obj_set_style_text_font(volLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(volLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(volLabel, LV_ALIGN_TOP_LEFT, 30, 120);

    _volumeSlider = lv_slider_create(card);
    lv_obj_set_size(_volumeSlider, 300, 15);
    lv_obj_align(_volumeSlider, LV_ALIGN_TOP_LEFT, 30, 148);
    lv_slider_set_range(_volumeSlider, 0, 21);
    lv_slider_set_value(_volumeSlider, g_settingVolume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_volumeSlider, lv_color_hex(COLOR_ACCENT2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_volumeSlider, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_radius(_volumeSlider, 5, LV_PART_INDICATOR);
    lv_obj_set_style_radius(_volumeSlider, 5, LV_PART_MAIN);
    lv_obj_add_event_cb(_volumeSlider, _volumeCb, LV_EVENT_VALUE_CHANGED, this);

    // ---- 联机模式切换 ----
    extern volatile bool g_netplayWanted;
    lv_obj_t *npLabel = lv_label_create(card);
    lv_label_set_text(npLabel, LV_SYMBOL_WIFI " Online Play");
    lv_obj_set_style_text_font(npLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(npLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(npLabel, LV_ALIGN_TOP_LEFT, 30, 190);

    lv_obj_t *npSw = lv_switch_create(card);
    lv_obj_align(npSw, LV_ALIGN_TOP_RIGHT, -30, 180);
    if (g_netplayWanted)
        lv_obj_add_state(npSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(npSw, _netplayCb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_state(npSw, LV_STATE_DISABLED); /// 暂停使用

    // ---- 屏幕旋转 ----
    lv_obj_t *rotLabel = lv_label_create(card);
    lv_label_set_text(rotLabel, LV_SYMBOL_REFRESH " Rotate 180");
    lv_obj_set_style_text_font(rotLabel, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(rotLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_align(rotLabel, LV_ALIGN_TOP_LEFT, 30, 230);

    lv_obj_t *rotSw = lv_switch_create(card);
    lv_obj_align(rotSw, LV_ALIGN_TOP_RIGHT, -30, 225);
    if (g_rotation180)
        lv_obj_add_state(rotSw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(rotSw, _rotationCb, LV_EVENT_VALUE_CHANGED, this);

    // ---- 清除缓存按钮 ----
    lv_obj_t *clearBtn = lv_btn_create(card);
    lv_obj_set_size(clearBtn, 200, 36);
    lv_obj_align(clearBtn, LV_ALIGN_TOP_LEFT, 30, 270);
    lv_obj_set_style_bg_color(clearBtn, lv_color_hex(0x553333), 0);
    lv_obj_set_style_bg_color(clearBtn, lv_color_hex(0x883333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(clearBtn, 8, 0);
    lv_obj_set_style_border_width(clearBtn, 1, 0);
    lv_obj_set_style_border_color(clearBtn, lv_color_hex(0x884444), 0);
    lv_obj_set_style_shadow_width(clearBtn, 0, 0);
    lv_obj_add_event_cb(clearBtn, _clearCacheCb, LV_EVENT_CLICKED, this);
    lv_obj_t *clearLbl = lv_label_create(clearBtn);
    lv_label_set_text(clearLbl, LV_SYMBOL_TRASH " Re-Scan Game");
    lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(clearLbl, lv_color_hex(0xFFAAAA), 0);
    lv_obj_center(clearLbl);

    // ----- 关机按钮------
    lv_obj_t *ShutDown = lv_btn_create(card);
    lv_obj_set_size(ShutDown, 200, 40);
    lv_obj_align(ShutDown, LV_ALIGN_TOP_LEFT, 30, 320);
    lv_obj_set_style_bg_color(ShutDown, lv_color_hex(0xD32F2F), 0);
    lv_obj_set_style_bg_color(ShutDown, lv_color_hex(0x553333), LV_STATE_PRESSED);
    lv_obj_set_style_radius(ShutDown, 8, 0);
    lv_obj_set_style_border_width(ShutDown, 1, 0);
    lv_obj_set_style_border_color(ShutDown, lv_color_hex(0x884444), 0);
    lv_obj_set_style_shadow_width(clearBtn, 1, 0);
    lv_obj_add_event_cb(ShutDown, _shutDownCb, LV_EVENT_CLICKED, this);
    lv_obj_t *shutLBL = lv_label_create(ShutDown);
    lv_label_set_text(shutLBL, LV_SYMBOL_POWER " Shut Down");
    lv_obj_set_style_text_font(shutLBL, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(shutLBL, lv_color_hex(0x000), 0);
    lv_obj_center(shutLBL);

    // 提示文字
    lv_obj_t *hint = lv_label_create(card);
    lv_label_set_text(hint, "Tap outside to close");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 200, 370);

    // 版本号
    lv_obj_t *Vint = lv_label_create(card);
    lv_label_set_text(Vint, "version: 1.0");
    lv_obj_set_style_text_font(Vint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(Vint, lv_color_hex(COLOR_TEXT_DIM), 0);
    lv_obj_align(Vint, LV_ALIGN_TOP_LEFT, 200, 390);
}

// ===================== 设置面板回调 =====================
void GameUI::_settingsBtnCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (self)
        self->showSettings();
}

void GameUI::_settingsCloseCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    if (self)
        self->hideSettings();
}

void GameUI::_brightnessCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    int b = (int)lv_slider_get_value(slider);
    int v = self->_volumeSlider ? (int)lv_slider_get_value(self->_volumeSlider) : g_settingVolume;
    amoled.setBrightness(b);
    settings_changed(b, v);
}

void GameUI::_volumeCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    lv_obj_t *slider = lv_event_get_target(e);
    int b = self->_brightnessSlider ? (int)lv_slider_get_value(self->_brightnessSlider) : g_settingBrightness;
    int v = (int)lv_slider_get_value(slider);
    audioPlayer.setVolume((uint8_t)v);
    settings_changed(b, v);
}
void GameUI::_netplayCb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    g_netplayWanted = on;
    // Trigger settings save (reuses existing debounce)
    int b = g_settingBrightness;
    int v = g_settingVolume;
    settings_changed(b, v);
    Serial.printf("[CFG] Netplay: %s\n", on ? "ON" : "OFF");
}

void GameUI::_rotationCb(lv_event_t *e)
{
    GameUI *self = (GameUI *)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target(e);
    g_rotation180 = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 180 : 0;
    amoled.setRotation(g_rotation180);
    // Re-read screen dimensions after rotation
    screenW = amoled.width();
    screenH = amoled.height();
    if (self)
    {
        self->_scrW = screenW;
        self->_scrH = screenH;
    }
    settings_changed(g_settingBrightness, g_settingVolume);
    Serial.printf("[CFG] Rotate: %d, screen=%dx%d\n", g_rotation180, screenW, screenH);
}

void GameUI::_clearCacheCb(lv_event_t *e)
{
    if (SD.remove("/game/nes/.cache"))
    {
        Serial.println("[GameUI] Cache cleared — rescan on next boot");
    }
    else
    {
        Serial.println("[GameUI] No cache to clear");
    }
    // Brief flash the button to indicate success
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x338833), 0);
    esp_restart();
}

void GameUI::_shutDownCb(lv_event_t *e)
{
    esp_deep_sleep_start();
}

void GameUI::_carouselNavCb(lv_event_t *e)
{
    CarouselNavData *nav =
        static_cast<CarouselNavData *>(
            lv_event_get_user_data(e));

    if (nav == nullptr || nav->ui == nullptr)
        return;

    if (nav->isLeft)
    {
        nav->ui->navigateUp();
    }
    else
    {
        nav->ui->navigateDown();
    }
}
