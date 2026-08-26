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
    void breakSubmitGapWindow();

    uint32_t sampleRateHz() const;
    uint64_t submittedFrames() const;
    uint32_t backpressureEvents() const;
    uint64_t pcmFramesSinceReset() const;
    uint32_t pcmBuffersSinceReset() const;
    uint32_t pcmSubmitGapMaxUs() const;
    uint32_t pcmSubmitGapOver100Ms() const;
    uint32_t pcmLastSubmitAgeUs() const;

  private:
    // P2 device validation showed that the previous 768-frame chunk left only
    // about 52 ms across the three rotating M5.Speaker buffers.  A normal
    // directory/cache SD transaction could consume that entire margin.  Keep
    // the proven three-buffer ownership model, but double each chunk so the
    // Player can ride through bounded foreground SD latency without changing
    // the decoder, downmix, volume, or audio backend.
    static constexpr size_t kFramesPerBuffer = 1536;
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
    uint64_t pcmFramesSinceReset_ = 0;
    uint32_t pcmBuffersSinceReset_ = 0;
    uint32_t pcmSubmitGapMaxUs_ = 0;
    uint32_t pcmSubmitGapOver100Ms_ = 0;
    uint32_t pcmLastSubmitAtUs_ = 0;
    bool pcmSubmitObservedSinceReset_ = false;
};

}  // namespace player
}  // namespace adv_walkman
