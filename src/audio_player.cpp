// audio_player.cpp — 完整替换
#include "audio_player.h"
#include "Audio.h"
#include <SD.h>
#include <driver/i2s.h>

// PCM 播放的 I2S 采样率，必须和你的 PCM 文件一致
// 如果你的 PCM 是 22050Hz mono 16bit，就写 22050
// 如果是 16000Hz，就写 16000
#define PCM_SAMPLE_RATE  22050

// 每次写入 I2S 的块大小（字节）
// 越小延迟越低，但 CPU 开销越大
#define PCM_CHUNK_BYTES  1024

AudioPlayer::AudioPlayer()
    : _audio(nullptr)
    , _bclk(0), _lrc(0), _dout(0)
    , _initialized(false)
    , _pcmTask(nullptr)
    , _pcmPlaying(false)
    , _pcmBuf(nullptr)
    , _pcmSize(0)
    , _pcmVolume(12)
{
}

AudioPlayer::~AudioPlayer()
{
    end();
}

bool AudioPlayer::init(uint8_t bclk, uint8_t lrc, uint8_t dout)
{
    _bclk = bclk;
    _lrc  = lrc;
    _dout = dout;

    if (_audio) {
        delete (Audio*)_audio;
        _audio = nullptr;
    }

    _audio = new Audio(false, 3, I2S_NUM_0);
    if (!_audio) return false;

    Audio *a = (Audio*)_audio;
    a->setPinout(_bclk, _lrc, _dout);
    a->setVolume(12);
    _initialized = true;

    Serial.printf("[Audio] I2S init: BCLK=%d LRC=%d DOUT=%d\n", _bclk, _lrc, _dout);
    return true;
}

bool AudioPlayer::playSFX(fs::FS &fs, const char *path)
{
    if (!_initialized || !_audio) return false;
    Audio *a = (Audio*)_audio;
    if (a->isRunning()) return false;
    return a->connecttoFS(fs, path);
}

void AudioPlayer::loop()
{
    if (_initialized && _audio) {
        ((Audio*)_audio)->loop();
    }
}

void AudioPlayer::end()
{
    stopPCM();

    if (_audio) {
        Audio *a = (Audio*)_audio;
        a->stopSong();
        delete a;
        _audio = nullptr;
    }
    _initialized = false;
    Serial.println("[Audio] I2S released");
}

bool AudioPlayer::reinit()
{
    if (_bclk == 0) return false;
    return init(_bclk, _lrc, _dout);
}

bool AudioPlayer::isPlaying()
{
    if (_pcmPlaying) return true;
    if (!_initialized || !_audio) return false;
    return ((Audio*)_audio)->isRunning();
}

void AudioPlayer::setVolume(uint8_t vol)
{
    if (_initialized && _audio) {
        ((Audio*)_audio)->setVolume(vol);
    }
}
void AudioPlayer::pauseResume(){
        if (_initialized && _audio) {
        ((Audio*)_audio)->pauseResume();
    }
}
// ===================== PCM Preload =====================

bool AudioPlayer::loadPCM(const char *vfsPath, uint8_t *&buf, size_t &size)
{
    FILE *f = fopen(vfsPath, "rb");
    if (!f) {
        Serial.printf("[Audio] PCM not found: %s\n", vfsPath);
        return false;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);

    buf = (uint8_t *)ps_malloc(size);
    if (!buf) {
        Serial.printf("[Audio] PCM alloc failed: %d bytes\n", (int)size);
        fclose(f);
        return false;
    }
    size_t rd = fread(buf, 1, size, f);
    fclose(f);

    Serial.printf("[Audio] PCM loaded: %s (%d bytes)\n", vfsPath, (int)rd);
    return rd == size;
}

// ===================== PCM Stop =====================

void AudioPlayer::stopPCM()
{
    _pcmPlaying = false;

    if (_pcmTask) {
        // 等任务自己退出
        for (int i = 0; i < 50 && _pcmTask; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 如果还没退出，强制删除
        if (_pcmTask) {
            vTaskDelete(_pcmTask);
            _pcmTask = nullptr;
        }
    }
}

// ===================== PCM Play (non-blocking) =====================

void AudioPlayer::playPCM(uint8_t *buf, size_t size, uint8_t volume)
{
    if (!buf || !size) return;

    // 如果上一个还在播，先停掉
    if (_pcmPlaying) {
        stopPCM();
    }

    _pcmBuf    = buf;
    _pcmSize   = size;
    _pcmVolume = volume;
    _pcmPlaying = true;

    // 创建播放任务，优先级比音频消费低一点
    BaseType_t ok = xTaskCreatePinnedToCore(
        _pcmTaskFunc,
        "pcm_sfx",
        4096,
        this,       // 传 this 指针
        3,          // 优先级 3（低于 NES audio task 的 5）
        &_pcmTask,
        0           // core 0
    );

    if (ok != pdPASS) {
        Serial.println("[Audio] PCM task create failed!");
        _pcmPlaying = false;
        _pcmTask = nullptr;
    }
}

// ===================== PCM Task =====================

void AudioPlayer::_pcmTaskFunc(void *arg)
{
    AudioPlayer *self = (AudioPlayer *)arg;

    uint8_t  *buf    = self->_pcmBuf;
    size_t    size   = self->_pcmSize;
    uint8_t   volume = self->_pcmVolume;

    if (!buf || !size) {
        self->_pcmPlaying = false;
        self->_pcmTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // 配置 I2S 采样率匹配 PCM 文件
    esp_err_t err = i2s_set_clk(
        I2S_NUM_0,
        PCM_SAMPLE_RATE,
        I2S_BITS_PER_SAMPLE_16BIT,
        I2S_CHANNEL_STEREO
    );

    if (err != ESP_OK) {
        Serial.printf("[Audio] PCM i2s_set_clk failed: %d\n", err);
    }

    i2s_zero_dma_buffer(I2S_NUM_0);

    int numSamples = size / sizeof(int16_t);
    int16_t *monoSamples = (int16_t *)buf;

    float vol = volume / 21.0f;
    if (vol < 0.0f) vol = 0.0f;
    if (vol > 1.0f) vol = 1.0f;

    // 分块处理，每块 PCM_CHUNK_BYTES / 4 个 mono 采样
    // （因为 stereo 输出每个采样占 4 字节）
    int samplesPerChunk = PCM_CHUNK_BYTES / (2 * sizeof(int16_t));

    // 在堆上分配 stereo chunk buffer
    int16_t *stereoChunk = (int16_t *)malloc(PCM_CHUNK_BYTES);
    if (!stereoChunk) {
        Serial.println("[Audio] PCM stereo chunk alloc failed!");
        self->_pcmPlaying = false;
        self->_pcmTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    int offset = 0;

    while (offset < numSamples && self->_pcmPlaying) {
        int remain = numSamples - offset;
        int chunk = (remain < samplesPerChunk) ? remain : samplesPerChunk;

        // mono → stereo + volume
        for (int i = 0; i < chunk; i++) {
            int32_t s32 = (int32_t)((float)monoSamples[offset + i] * vol);
            if (s32 >  32767) s32 =  32767;
            if (s32 < -32768) s32 = -32768;
            int16_t s = (int16_t)s32;
            stereoChunk[i * 2]     = s;
            stereoChunk[i * 2 + 1] = s;
        }

        size_t stereoBytes = chunk * 2 * sizeof(int16_t);
        size_t written = 0;

        // 用短超时写入，避免永久阻塞
        i2s_write(I2S_NUM_0, stereoChunk, stereoBytes, &written, pdMS_TO_TICKS(50));

        offset += chunk;

        // 让出 CPU 给其他任务
        taskYIELD();
    }

    free(stereoChunk);

    // 等 DMA buffer 播完
    vTaskDelay(pdMS_TO_TICKS(30));

    self->_pcmPlaying = false;
    self->_pcmTask = nullptr;

    Serial.printf("[Audio] PCM done: %d samples\n", numSamples);
    vTaskDelete(nullptr);
}