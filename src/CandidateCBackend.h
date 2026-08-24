#pragma once

#include <BackgroundAudioMP3.h>
#include <ESP32I2SAudio.h>
#include <FS.h>

#include "AdvEs8311Codec.h"
#include "BenchmarkBackend.h"

namespace adv_walkman {

class MonoBackgroundOutput final : public AudioOutputBase {
  public:
    MonoBackgroundOutput();

    bool setBuffers(size_t buffers, size_t bufferWords,
                    int32_t silenceSample = 0) override;
    bool setBitsPerSample(int bits) override;
    bool setFrequency(int frequency) override;
    bool setStereo(bool stereo = true) override;
    bool begin() override;
    bool end() override;
    bool getUnderflow() override;
    void onTransmit(void (*callback)(void*), void* data) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    size_t write(uint8_t value) override;
    int availableForWrite() override;

    void resetStats();
    uint32_t sampleRate() const;
    uint32_t backpressureEvents() const;
    uint32_t outputUnderflows() const;

  private:
    static constexpr size_t kScratchFrames = 1152;

    ESP32I2SAudio output_;
    int16_t scratch_[kScratchFrames * 2]{};
    uint32_t sampleRate_ = 0;
    uint32_t backpressureEvents_ = 0;
};

class CandidateCBackend final : public BenchmarkBackend {
  public:
    CandidateCBackend();

    bool begin(const char* path) override;
    void service() override;
    bool pause() override;
    bool resume() override;
    void stop() override;
    bool restart() override;
    bool seekSeconds(uint32_t seconds) override;
    void setLoop(bool enabled) override;
    BenchmarkStats stats() const override;
    const char* name() const override;

  private:
    static constexpr size_t kPathCapacity = 128;
    static constexpr size_t kFeedChunkSize = 512;
    static constexpr uint8_t kMaximumFeedsPerService = 8;

    bool startFile(uint32_t byteOffset, bool preservePause);
    void closeFile();
    void closePlayback();
    bool fail(const char* error);

    AdvEs8311Codec codec_;
    MonoBackgroundOutput output_;
    BackgroundAudioMP3 decoder_;
    File file_;
    BackendState state_ = BackendState::BOOT;
    const char* error_ = "none";
    uint32_t startedAtMs_ = 0;
    uint32_t decodeServiceCalls_ = 0;
    uint32_t bytesRead_ = 0;
    uint32_t trackLoops_ = 0;
    uint32_t serviceMaxUs_ = 0;
    uint32_t decoderErrorsBaseline_ = 0;
    uint32_t decoderUnderflowsBaseline_ = 0;
    uint32_t outputUnderflowsBaseline_ = 0;
    bool loopEnabled_ = false;
    bool eofReached_ = false;
    char path_[kPathCapacity]{};
    char fileSha256_[65] = "unavailable";
    uint8_t feedBuffer_[kFeedChunkSize]{};
};

}  // namespace adv_walkman
