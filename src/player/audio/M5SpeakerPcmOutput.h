#pragma once

#include <AudioOutput.h>
#include <M5Unified.h>

#include "player/audio/PcmDsp.h"

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
    bool setSoundPreset(SoundPreset preset, bool smooth = true);
    SoundPreset soundPreset() const { return dsp_.preset(); }
    SoundPreset appliedSoundPreset() const { return dsp_.appliedPreset(); }
    uint32_t dspBlockMaxUs() const { return dspBlockMaxUs_; }
    uint32_t dspCrossfadeCount() const { return dsp_.crossfadeCount(); }
    uint32_t dspLimiterEvents() const { return dsp_.limiterEvents(); }
    uint16_t dspPreLimiterPeakQ15() const { return dsp_.preLimiterPeak(); }
    uint32_t dspInvalidFallbacks() const { return dsp_.invalidFallbacks(); }
    uint32_t presetApplyLatencyMaxUs() const {
        return static_cast<uint32_t>(presetApplyLatencyMaxMs_) * 1000U;
    }
    static constexpr size_t dspRuntimeStateBytes() {
        return sizeof(PcmDsp) + sizeof(uint16_t) * 3 +
               sizeof(bool) * 2;
    }

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
    PcmDsp dsp_{};
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
    // Saturating 16-bit microsecond peak still represents more than 20x the
    // 3 ms acceptance limit and avoids charging four bytes for an impossible
    // in-spec value.
    uint16_t dspBlockMaxUs_ = 0;
    // The acceptance target is 100 ms, so millisecond precision is enough.
    // A 16-bit wrapping timestamp also keeps the complete P5 DSP state within
    // its fixed RAM budget.
    uint16_t presetRequestedAtMs_ = 0;
    uint16_t presetApplyLatencyMaxMs_ = 0;
    bool activeBufferProcessed_ = false;
    bool presetApplyPending_ = false;
    bool pcmSubmitObservedSinceReset_ = false;
};

static_assert(M5SpeakerPcmOutput::dspRuntimeStateBytes() <= 256,
              "P5 complete DSP runtime state must stay within 256 bytes");

}  // namespace player
}  // namespace adv_walkman
