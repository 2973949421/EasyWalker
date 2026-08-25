#pragma once

#include <AudioOutput.h>
#include <M5Unified.h>

namespace adv_walkman {
namespace player {

class M5SpeakerPcmOutput final : public AudioOutput {
  public:
    explicit M5SpeakerPcmOutput(m5::Speaker_Class* speaker);

    bool begin() override;
    bool SetRate(int hz) override;
    bool ConsumeSample(int16_t sample[2]) override;
    void flush() override;
    bool stop() override;

    bool flushForDrain();
    bool isDrained() const;
    void resetDiagnostics();

    uint32_t sampleRateHz() const;
    uint64_t submittedFrames() const;
    uint32_t backpressureEvents() const;

  private:
    static constexpr size_t kFramesPerBuffer = 768;
    static constexpr uint8_t kBufferCount = 3;
    static constexpr uint8_t kVirtualChannel = 0;

    bool queueBuffer();

    m5::Speaker_Class* speaker_ = nullptr;
    int16_t buffers_[kBufferCount][kFramesPerBuffer]{};
    size_t bufferIndex_ = 0;
    uint8_t activeBuffer_ = 0;
    uint32_t sampleRateHz_ = 0;
    uint64_t submittedFrames_ = 0;
    uint32_t backpressureEvents_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
