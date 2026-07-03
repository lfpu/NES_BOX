#ifndef GAME_UI_H
#define GAME_UI_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>

struct GameInfo
{
    String name;
    String fullPath;
    String imgPath;
};

enum ViewMode
{
    VIEW_LIST = 0,
    VIEW_CAROUSEL
};

enum UIState
{
    UI_BROWSING = 0,
    UI_LAUNCHING,
    UI_GAME_RUNNING
};
class GameUI;
struct CarouselNavData
{
    GameUI *ui;
    bool isLeft;
};

class GameUI
{
public:
    GameUI();
    ~GameUI();

    void init(uint16_t screenW, uint16_t screenH);
    bool loadGameList(const char *path = "/game/nes");
    void show();
    void switchViewMode();
    void navigateUp();
    void navigateDown();
    void navigateLeft();
    void navigateRight();
    void selectCurrentGame();
    const GameInfo *getSelectedGame() const;
    UIState getState() const { return _state; }
    void setLaunchCallback(std::function<void(const String &)> cb) { _launchCb = cb; }
    void handleInput(uint8_t btnUp, uint8_t btnDown, uint8_t btnLeft,
                     uint8_t btnRight, uint8_t btnSelect);
    void update();
    void showSettings();
    void hideSettings();

private:
    void _createListView();
    void _createCarouselView();
    void _createSettingsPanel();
    void _destroyCurrentView();
    void _updateCarouselDisplay(bool animate = true);
    void _playLaunchAnimation();
    void _updateInfoLabels();

    // ===== Carousel 高级背景/卡片动画 =====
    void _buildGameImagePath(const GameInfo &game, char *outLvglPath, size_t outSize);
    void _applyBgImageCover(lv_obj_t *img, const char *lvglPath, lv_opa_t opa, int yOffset, uint16_t extraZoom);
    void _animateCardEnter(int dir);
    void _animateBackgroundTo(const char *lvglPath, int dir, bool animate);
    String _truncateGameName(const String &name, int maxChars);

    CarouselNavData _leftNavData;
    CarouselNavData _rightNavData;

    static void _listBtnClickCb(lv_event_t *e);
    static void _switchModeBtnCb(lv_event_t *e);
    static void _launchAnimReady(lv_anim_t *a);
    static void _settingsBtnCb(lv_event_t *e);
    static void _settingsCloseCb(lv_event_t *e);
    static void _brightnessCb(lv_event_t *e);
    static void _volumeCb(lv_event_t *e);
    static void _clearCacheCb(lv_event_t *e);
    static void _netplayCb(lv_event_t *e);
    static void _rotationCb(lv_event_t *e);
    static void _shutDownCb(lv_event_t *e);
    static void _searchGameCb(lv_event_t *e);
    static void _searchCloseCb(lv_event_t *e);
    static void _searchResultCb(lv_event_t *e);
    static void _searchLaunchCb(lv_event_t *e);
    static void _searchScrollCb(lv_event_t *e);
    static void _searchFocusCb(lv_event_t *e);
    static void _carouselNavCb(lv_event_t *e);

    std::vector<GameInfo> _games;
    int _selectedIndex;
    ViewMode _viewMode;
    UIState _state;
    uint16_t _scrW, _scrH;

    lv_obj_t *_mainScreen;
    lv_obj_t *_listContainer;
    lv_obj_t *_listView;
    lv_obj_t *_carouselContainer;
    lv_obj_t *_gameCard;
    lv_obj_t *_gameNameLabel;
    lv_obj_t *_gameIndexLabel;
    lv_obj_t *_gameTotalLabel;
    lv_obj_t *_instructionLabel;
    lv_obj_t *_progressDots;
    lv_obj_t *_decorLine1;
    lv_obj_t *_decorLine2;
    lv_obj_t *_viewModeBtn;
    lv_obj_t *_settingsBtn;
    lv_obj_t *_headerLabel;
    lv_obj_t *_launchOverlay;
    lv_obj_t *_settingsOverlay;
    lv_obj_t *_batteryLabel;
    lv_obj_t *_brightnessSlider;
    lv_obj_t *_volumeSlider;
    lv_obj_t *_gameSearcher;
    lv_obj_t *_searchOverlay;
    lv_obj_t *_searchResultList;
    lv_obj_t *_searchTextArea;
    lv_obj_t *_searchKeyboard;
    lv_obj_t *_bgImg;

    // ===== Carousel background animation layers =====
    lv_obj_t *_bgLayer;
    lv_obj_t *_bgImgA;
    lv_obj_t *_bgImgB;
    lv_obj_t *_shaderOverlay;
    bool _bgUsingA;
    int _lastNavDir;
    uint32_t _lastCarouselAnimTick;
    char _bgPathA[128];
    char _bgPathB[128];

    std::function<void(const String &)> _launchCb;

    unsigned long _lastInputTime;
    static const unsigned long INPUT_DEBOUNCE = 200;

    static const uint32_t COLOR_BG = 0x0A0A2E;
    static const uint32_t COLOR_CARD_BG = 0x1A1A4E;
    static const uint32_t COLOR_ACCENT = 0x00D4FF;
    static const uint32_t COLOR_ACCENT2 = 0xFF6B35;
    static const uint32_t COLOR_TEXT = 0xFFFFFF;
    static const uint32_t COLOR_TEXT_DIM = 0x8888AA;
    static const uint32_t COLOR_SUCCESS = 0x00FF88;
    static const uint32_t COLOR_LIST_BG = 0x12123A;
    static const uint32_t COLOR_LIST_ITEM = 0x1E1E5A;
    static const uint32_t COLOR_LIST_HOVER = 0x2A2A7A;
};

#endif