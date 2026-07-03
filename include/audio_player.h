// audio_player.h — 在原有基础上添加以下成员
#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    bool init(uint8_t bclk, uint8_t lrc, uint8_t dout);
    bool reinit();
    void end();

    bool playSFX(fs::FS &fs, const char *path);
    void loop();
    bool isPlaying();
    void setVolume(uint8_t vol);
    void pauseResume();

    // PCM preload & playback
    bool loadPCM(const char *vfsPath, uint8_t *&buf, size_t &size);
    void playPCM(uint8_t *buf, size_t size, uint8_t volume);

    // 停止当前 PCM 播放
    void stopPCM();

    uint8_t getBclk() const { return _bclk; }
    uint8_t getLrc()  const { return _lrc; }
    uint8_t getDout() const { return _dout; }

private:
    void   *_audio;
    uint8_t _bclk, _lrc, _dout;
    bool    _initialized;

    // PCM 播放任务相关
    static void _pcmTaskFunc(void *arg);
    TaskHandle_t   _pcmTask;
    volatile bool  _pcmPlaying;

    // PCM 播放参数（传给任务用）
    uint8_t       *_pcmBuf;
    size_t         _pcmSize;
    uint8_t        _pcmVolume;
};

#endif