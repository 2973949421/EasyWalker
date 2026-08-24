#pragma once

#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#include "AdvEs8311Codec.h"
#include "BenchmarkBackend.h"

namespace adv_walkman {

class MonoDirectI2SOutput final : public AudioOutputI2S {
  public:
    MonoDirectI2SOutput();

    bool SetRate(int hz) override;
    bool ConsumeSample(int16_t sample[2]) override;
    void resetStats();
    uint32_t sampleRate() const;
    uint32_t backpressureEvents() const;

  private:
    uint32_t sampleRate_ = 0;
    uint32_t backpressureEvents_ = 0;
};

class CandidateBBackend final : public BenchmarkBackend {
  public:
    CandidateBBackend();

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

    bool startFile(uint32_t byteOffset, bool preservePause);
    void updateBytesRead();
    void closePlayback();
    bool fail(const char* error);

    AdvEs8311Codec codec_;
    MonoDirectI2SOutput output_;
    AudioFileSourceSD file_;
    AudioGeneratorMP3 decoder_;
    BackendState state_ = BackendState::BOOT;
    const char* error_ = "none";
    uint32_t startedAtMs_ = 0;
    uint32_t decodeServiceCalls_ = 0;
    uint32_t bytesRead_ = 0;
    uint32_t lastFilePosition_ = 0;
    uint32_t trackLoops_ = 0;
    uint32_t serviceMaxUs_ = 0;
    bool loopEnabled_ = false;
    char path_[kPathCapacity]{};
    char fileSha256_[65] = "unavailable";
};

}  // namespace adv_walkman
