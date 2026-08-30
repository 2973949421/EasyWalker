#pragma once

#include <cstddef>
#include <cstdint>

#include "player/core/CoreTypes.h"

namespace adv_walkman {
namespace player {

class PcmDsp final {
  public:
    static bool selfCheck();
    bool setSampleRate(uint32_t sampleRateHz);
    bool setPreset(SoundPreset preset, bool smooth = true);
    void resetStream();
    void resetDiagnostics();
    void processBlock(int16_t* samples, size_t frames);

    SoundPreset preset() const { return targetPreset_; }
    SoundPreset appliedPreset() const { return currentPreset_; }
    uint32_t crossfadeCount() const { return crossfadeCount_; }
    uint32_t limiterEvents() const { return limiterEvents_; }
    uint16_t preLimiterPeak() const { return preLimiterPeak_; }
    uint32_t invalidFallbacks() const { return invalidFallbacks_; }

  private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process(float input);
        void reset();
    };

    struct Chain {
        Biquad sections[3]{};
        float inputGain = 1.0f;
        float outputGain = 1.0f;
        float compressorEnvelope = 0.0f;
        uint8_t sectionCount = 0;
        uint8_t preset = 0;
        bool compressor = false;
        bool saturation = false;

        float process(float input, float attackCoefficient,
                      float releaseCoefficient);
        void resetState();
    };

    bool configure(Chain& chain, SoundPreset preset);
    bool configureLowPass(Biquad& section, float frequency, float q);
    bool configureHighPass(Biquad& section, float frequency, float q);
    bool configurePeak(Biquad& section, float frequency, float q, float gainDb);
    bool configureLowShelf(Biquad& section, float frequency, float gainDb);
    bool configureHighShelf(Biquad& section, float frequency, float gainDb);
    bool normalize(Biquad& section, float a0);
    float limited(float sample, bool enabled);
    static bool validPreset(SoundPreset preset);

    Chain current_{};
    Chain incoming_{};
    uint32_t sampleRateHz_ = 0;
    uint16_t transitionFrames_ = 0;
    uint16_t transitionCursor_ = 0;
    float compressorAttack_ = 0.0f;
    float compressorRelease_ = 0.0f;
    float limiterRelease_ = 0.0f;
    float limiterGain_ = 1.0f;
    // Diagnostic event counters saturate; 255 is sufficient to prove a
    // stress run exceeded the expected range without spending audio RAM.
    uint8_t crossfadeCount_ = 0;
    uint8_t limiterEvents_ = 0;
    uint8_t invalidFallbacks_ = 0;
    uint16_t preLimiterPeak_ = 0;
    SoundPreset currentPreset_ = SoundPreset::Original;
    SoundPreset targetPreset_ = SoundPreset::Original;
    bool limiterActive_ = false;
};

static_assert(sizeof(PcmDsp) <= 256, "P5 DSP state must stay within 256 bytes");

}  // namespace player
}  // namespace adv_walkman
