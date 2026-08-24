#pragma once

#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include <M5Unified.h>

#include "BenchmarkBackend.h"

namespace adv_walkman {

class MonoM5SpeakerOutput final : public AudioOutput {
  public:
    explicit MonoM5SpeakerOutput(m5::Speaker_Class* speaker);

    bool begin() override;
    bool SetRate(int hz) override;
    bool ConsumeSample(int16_t sample[2]) override;
    void flush() override;
    bool stop() override;

    uint32_t sampleRate() const;
    uint32_t backpressureEvents() const;

  private:
    static constexpr size_t kFramesPerBuffer = 768;
    static constexpr uint8_t kBufferCount = 3;
    static constexpr uint8_t kVirtualChannel = 0;

    m5::Speaker_Class* speaker_;
    int16_t buffers_[kBufferCount][kFramesPerBuffer]{};
    size_t bufferIndex_ = 0;
    uint8_t activeBuffer_ = 0;
    uint32_t sampleRate_ = 0;
    uint32_t backpressureEvents_ = 0;
};

class CandidateABackend final : public BenchmarkBackend {
  public:
    CandidateABackend();

    bool begin(const char* path) override;
    void service() override;
    bool pause() override;
    bool resume() override;
    void stop() override;
    BenchmarkStats stats() const override;
    const char* name() const override;

  private:
    static constexpr int kSdSck = 40;
    static constexpr int kSdMiso = 39;
    static constexpr int kSdMosi = 14;
    static constexpr int kSdCs = 12;
    static constexpr uint32_t kSdFrequencyHz = 25000000UL;
    static constexpr uint8_t kInitialVolume = 64;

    bool mountSd();
    bool computeFileSha256(const char* path);
    void closePlayback();
    bool fail(const char* error);

    MonoM5SpeakerOutput output_;
    AudioFileSourceSD file_;
    AudioGeneratorMP3 decoder_;
    BackendState state_ = BackendState::BOOT;
    const char* error_ = "none";
    uint32_t startedAtMs_ = 0;
    uint32_t decodeServiceCalls_ = 0;
    bool sdMounted_ = false;
    char fileSha256_[65] = "unavailable";
};

}  // namespace adv_walkman
