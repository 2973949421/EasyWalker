#include "player/audio/PcmDsp.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace adv_walkman {
namespace player {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSqrtHalf = 0.70710678118f;
constexpr float kRadioThreshold = 0.125892541f;  // -18 dBFS.
constexpr float kRadioRatio = 2.5f;
constexpr float kLimiterCeiling = 0.891250938f;  // -1 dBFS.

void incrementSaturated(uint16_t& value) {
    if (value != UINT16_MAX) ++value;
}

void incrementSaturated(uint8_t& value) {
    if (value != UINT8_MAX) ++value;
}

float dbToLinear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float tanhApproximation(float value) {
    const float squared = value * value;
    return value * (27.0f + squared) / (27.0f + 9.0f * squared);
}

float saturate(float sample, float drive) {
    return tanhApproximation(drive * sample) / drive;
}

}  // namespace

float PcmDsp::Biquad::process(float input) {
    const float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

void PcmDsp::Biquad::reset() {
    z1 = 0.0f;
    z2 = 0.0f;
}

float PcmDsp::Chain::process(float input, float attackCoefficient,
                             float releaseCoefficient) {
    float output = input * inputGain;
    // The production presets have at most three fixed sections. Expanding the
    // tiny loop avoids a per-section counter/branch in the PCM hot path while
    // keeping the chain state and coefficient representation unchanged.
    if (sectionCount >= 1) {
        output = sections[0].process(output);
    }
    if (sectionCount >= 2) {
        output = sections[1].process(output);
    }
    if (sectionCount >= 3) {
        output = sections[2].process(output);
    }
    if (compressor) {
        const float level = std::fabs(output);
        const float coefficient = level > compressorEnvelope
                                      ? attackCoefficient
                                      : releaseCoefficient;
        compressorEnvelope = coefficient * compressorEnvelope +
                             (1.0f - coefficient) * level;
        if (compressorEnvelope > kRadioThreshold) {
            const float compressed = kRadioThreshold +
                (compressorEnvelope - kRadioThreshold) / kRadioRatio;
            output *= compressed / compressorEnvelope;
        }
    }
    if (saturation) {
        const float drive = preset == static_cast<uint8_t>(SoundPreset::Radio)
                                ? 0.80f : 0.95f;
        output = saturate(output, drive);
    }
    return output * outputGain;
}

void PcmDsp::Chain::resetState() {
    compressorEnvelope = 0.0f;
    for (auto& section : sections) {
        section.reset();
    }
}

bool PcmDsp::validPreset(SoundPreset preset) {
    return preset == SoundPreset::Original || preset == SoundPreset::Tape ||
           preset == SoundPreset::Radio || preset == SoundPreset::VocalClear;
}

bool PcmDsp::selfCheck() {
    PcmDsp dsp;
    if (!dsp.setSampleRate(44100)) return false;
    int16_t original[] = {-32768, -30000, -1, 0, 1, 30000, 32767};
    int16_t copy[sizeof(original) / sizeof(original[0])]{};
    std::copy(std::begin(original), std::end(original), std::begin(copy));
    dsp.processBlock(copy, sizeof(copy) / sizeof(copy[0]));
    if (!std::equal(std::begin(original), std::end(original), std::begin(copy))) {
        return false;
    }
    constexpr SoundPreset presets[] = {SoundPreset::Tape, SoundPreset::Radio,
                                        SoundPreset::VocalClear};
    for (const SoundPreset preset : presets) {
        if (!dsp.setPreset(preset, false)) return false;
        int16_t signal[32]{};
        signal[0] = 32767;
        for (size_t index = 1; index < 32; ++index) {
            signal[index] = index & 1 ? -32768 : 32767;
        }
        dsp.processBlock(signal, 32);
        if (dsp.invalidFallbacks() != 0) return false;
    }
    if (!dsp.setSampleRate(48000) || !dsp.setSampleRate(11025)) return false;
    for (unsigned index = 0; index < 1000; ++index) {
        if (!dsp.setPreset(presets[index % 3], true)) return false;
    }
    const SoundPreset before = dsp.preset();
    if (dsp.setPreset(static_cast<SoundPreset>(0xFF), true) ||
        dsp.preset() != before) return false;
    return true;
}

bool PcmDsp::setSampleRate(uint32_t sampleRateHz) {
    if (sampleRateHz < 8000 || sampleRateHz > 192000) {
        return false;
    }
    sampleRateHz_ = sampleRateHz;
    compressorAttack_ = std::exp(-1.0f / (0.010f * sampleRateHz_));
    compressorRelease_ = std::exp(-1.0f / (0.120f * sampleRateHz_));
    limiterRelease_ = std::exp(-1.0f / (0.080f * sampleRateHz_));
    if (!configure(current_, targetPreset_)) {
        incrementSaturated(invalidFallbacks_);
        targetPreset_ = SoundPreset::Original;
        currentPreset_ = SoundPreset::Original;
        configure(current_, SoundPreset::Original);
        incoming_ = current_;
        return false;
    }
    currentPreset_ = targetPreset_;
    incoming_ = current_;
    transitionFrames_ = 0;
    transitionCursor_ = 0;
    limiterGain_ = 1.0f;
    limiterActive_ = false;
    return true;
}

bool PcmDsp::setPreset(SoundPreset preset, bool smooth) {
    if (!validPreset(preset)) {
        return false;
    }
    if (preset == targetPreset_) {
        return true;
    }
    if (transitionFrames_ != 0) {
        current_ = incoming_;
        currentPreset_ = targetPreset_;
    }
    targetPreset_ = preset;
    if (sampleRateHz_ == 0) {
        currentPreset_ = preset;
        return true;
    }
    bool configured = true;
    if (!configure(incoming_, preset)) {
        incrementSaturated(invalidFallbacks_);
        targetPreset_ = SoundPreset::Original;
        configure(incoming_, SoundPreset::Original);
        preset = SoundPreset::Original;
        configured = false;
    }
    if (!smooth || currentPreset_ == preset) {
        current_ = incoming_;
        currentPreset_ = preset;
        transitionFrames_ = 0;
        transitionCursor_ = 0;
        limiterGain_ = 1.0f;
        limiterActive_ = false;
        return configured;
    }
    transitionFrames_ = static_cast<uint16_t>(
        std::max<uint32_t>(1, sampleRateHz_ / 50U));
    transitionCursor_ = 0;
    incrementSaturated(crossfadeCount_);
    return configured;
}

void PcmDsp::resetStream() {
    currentPreset_ = targetPreset_;
    if (sampleRateHz_ != 0 && !configure(current_, currentPreset_)) {
        incrementSaturated(invalidFallbacks_);
        currentPreset_ = targetPreset_ = SoundPreset::Original;
        configure(current_, SoundPreset::Original);
    }
    incoming_ = current_;
    transitionFrames_ = 0;
    transitionCursor_ = 0;
    limiterGain_ = 1.0f;
    limiterActive_ = false;
}

void PcmDsp::resetDiagnostics() {
    crossfadeCount_ = 0;
    limiterEvents_ = 0;
    invalidFallbacks_ = 0;
    preLimiterPeak_ = 0;
}

void PcmDsp::processBlock(int16_t* samples, size_t frames) {
    if (samples == nullptr || frames == 0 || sampleRateHz_ == 0) {
        return;
    }
    // Original is a strict, zero-cost bypass once no crossfade is active.
    // This preserves every int16 sample bit-for-bit, including -32768.
    if (transitionFrames_ == 0 && currentPreset_ == SoundPreset::Original &&
        targetPreset_ == SoundPreset::Original) {
        limiterGain_ = 1.0f;
        limiterActive_ = false;
        return;
    }
    for (size_t index = 0; index < frames; ++index) {
        const float input = static_cast<float>(samples[index]) / 32768.0f;
        float output = current_.process(input, compressorAttack_, compressorRelease_);
        if (transitionFrames_ != 0) {
            const float next = incoming_.process(input, compressorAttack_, compressorRelease_);
            const float mix = static_cast<float>(transitionCursor_ + 1U) /
                              static_cast<float>(transitionFrames_);
            output += (next - output) * std::min(1.0f, mix);
            if (++transitionCursor_ >= transitionFrames_) {
                current_ = incoming_;
                currentPreset_ = targetPreset_;
                transitionFrames_ = 0;
                transitionCursor_ = 0;
            }
        }
        if (!std::isfinite(output)) {
            incrementSaturated(invalidFallbacks_);
            output = input;
        }
        const float peak = std::fabs(output);
        // Q15 diagnostic allows values up to 2.0 full scale so a clipped peak
        // remains distinguishable from an exactly full-scale signal.
        const uint32_t scaledPeak = static_cast<uint32_t>(
            std::min(2.0f, peak) * 32767.0f + 0.5f);
        preLimiterPeak_ = static_cast<uint16_t>(
            std::max<uint32_t>(preLimiterPeak_, scaledPeak));
        const bool processed = currentPreset_ != SoundPreset::Original ||
                               targetPreset_ != SoundPreset::Original;
        output = limited(output, processed);
        const float scaled = std::max(-32768.0f,
                                      std::min(32767.0f, output * 32767.0f));
        samples[index] = static_cast<int16_t>(
            scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
    }
}

float PcmDsp::limited(float sample, bool enabled) {
    if (!enabled) {
        limiterGain_ = 1.0f;
        limiterActive_ = false;
        return sample;
    }
    const float level = std::fabs(sample);
    const float desired = level > kLimiterCeiling ? kLimiterCeiling / level : 1.0f;
    if (desired < limiterGain_) {
        limiterGain_ = desired;
        if (!limiterActive_) {
            incrementSaturated(limiterEvents_);
            limiterActive_ = true;
        }
    } else {
        limiterGain_ = limiterRelease_ * limiterGain_ +
                       (1.0f - limiterRelease_);
        if (limiterGain_ > 0.9999f) {
            limiterGain_ = 1.0f;
            limiterActive_ = false;
        }
    }
    return sample * limiterGain_;
}

bool PcmDsp::normalize(Biquad& section, float a0) {
    if (!std::isfinite(a0) || std::fabs(a0) < 1.0e-9f) {
        return false;
    }
    section.b0 /= a0;
    section.b1 /= a0;
    section.b2 /= a0;
    section.a1 /= a0;
    section.a2 /= a0;
    section.reset();
    return std::isfinite(section.b0) && std::isfinite(section.b1) &&
           std::isfinite(section.b2) && std::isfinite(section.a1) &&
           std::isfinite(section.a2);
}

bool PcmDsp::configureLowPass(Biquad& section, float frequency, float q) {
    frequency = std::min(frequency, sampleRateHz_ * 0.45f);
    const float omega = 2.0f * kPi * frequency / sampleRateHz_;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) / (2.0f * q);
    section.b0 = (1.0f - cosine) * 0.5f;
    section.b1 = 1.0f - cosine;
    section.b2 = section.b0;
    section.a1 = -2.0f * cosine;
    section.a2 = 1.0f - alpha;
    return normalize(section, 1.0f + alpha);
}

bool PcmDsp::configureHighPass(Biquad& section, float frequency, float q) {
    frequency = std::min(frequency, sampleRateHz_ * 0.20f);
    const float omega = 2.0f * kPi * frequency / sampleRateHz_;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) / (2.0f * q);
    section.b0 = (1.0f + cosine) * 0.5f;
    section.b1 = -(1.0f + cosine);
    section.b2 = section.b0;
    section.a1 = -2.0f * cosine;
    section.a2 = 1.0f - alpha;
    return normalize(section, 1.0f + alpha);
}

bool PcmDsp::configurePeak(Biquad& section, float frequency, float q,
                           float gainDb) {
    frequency = std::min(frequency, sampleRateHz_ * 0.45f);
    const float omega = 2.0f * kPi * frequency / sampleRateHz_;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) / (2.0f * q);
    const float amplitude = std::pow(10.0f, gainDb / 40.0f);
    section.b0 = 1.0f + alpha * amplitude;
    section.b1 = -2.0f * cosine;
    section.b2 = 1.0f - alpha * amplitude;
    section.a1 = section.b1;
    section.a2 = 1.0f - alpha / amplitude;
    return normalize(section, 1.0f + alpha / amplitude);
}

bool PcmDsp::configureLowShelf(Biquad& section, float frequency, float gainDb) {
    const float amplitude = std::pow(10.0f, gainDb / 40.0f);
    const float omega = 2.0f * kPi * frequency / sampleRateHz_;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) * kSqrtHalf;
    const float beta = 2.0f * std::sqrt(amplitude) * alpha;
    section.b0 = amplitude * ((amplitude + 1.0f) -
                  (amplitude - 1.0f) * cosine + beta);
    section.b1 = 2.0f * amplitude * ((amplitude - 1.0f) -
                  (amplitude + 1.0f) * cosine);
    section.b2 = amplitude * ((amplitude + 1.0f) -
                  (amplitude - 1.0f) * cosine - beta);
    section.a1 = -2.0f * ((amplitude - 1.0f) +
                  (amplitude + 1.0f) * cosine);
    section.a2 = (amplitude + 1.0f) +
                 (amplitude - 1.0f) * cosine - beta;
    const float a0 = (amplitude + 1.0f) +
                     (amplitude - 1.0f) * cosine + beta;
    return normalize(section, a0);
}

bool PcmDsp::configureHighShelf(Biquad& section, float frequency, float gainDb) {
    frequency = std::min(frequency, sampleRateHz_ * 0.45f);
    const float amplitude = std::pow(10.0f, gainDb / 40.0f);
    const float omega = 2.0f * kPi * frequency / sampleRateHz_;
    const float cosine = std::cos(omega);
    const float alpha = std::sin(omega) * kSqrtHalf;
    const float beta = 2.0f * std::sqrt(amplitude) * alpha;
    section.b0 = amplitude * ((amplitude + 1.0f) +
                  (amplitude - 1.0f) * cosine + beta);
    section.b1 = -2.0f * amplitude * ((amplitude - 1.0f) +
                  (amplitude + 1.0f) * cosine);
    section.b2 = amplitude * ((amplitude + 1.0f) +
                  (amplitude - 1.0f) * cosine - beta);
    section.a1 = 2.0f * ((amplitude - 1.0f) -
                 (amplitude + 1.0f) * cosine);
    section.a2 = (amplitude + 1.0f) -
                 (amplitude - 1.0f) * cosine - beta;
    const float a0 = (amplitude + 1.0f) -
                     (amplitude - 1.0f) * cosine + beta;
    return normalize(section, a0);
}

bool PcmDsp::configure(Chain& chain, SoundPreset preset) {
    chain = Chain{};
    chain.preset = static_cast<uint8_t>(preset);
    if (preset == SoundPreset::Original) {
        return true;
    }
    if (preset == SoundPreset::Tape) {
        chain.inputGain = dbToLinear(-2.5f);
        chain.outputGain = 1.15f;
        chain.saturation = true;
        chain.sectionCount = 2;
        return configureLowShelf(chain.sections[0], 180.0f, 1.5f) &&
               configureHighShelf(chain.sections[1], 4200.0f, -4.5f);
    }
    if (preset == SoundPreset::Radio) {
        chain.inputGain = 0.90f;
        chain.outputGain = 1.15f;
        chain.compressor = true;
        chain.saturation = true;
        chain.sectionCount = 2;
        return configureHighPass(chain.sections[0], 200.0f, kSqrtHalf) &&
               configureLowPass(chain.sections[1], 5000.0f, kSqrtHalf);
    }
    if (preset == SoundPreset::VocalClear) {
        chain.inputGain = dbToLinear(-2.5f);
        chain.outputGain = 1.15f;
        chain.sectionCount = 3;
        return configureLowShelf(chain.sections[0], 180.0f, -1.5f) &&
               configurePeak(chain.sections[1], 1200.0f, 0.8f, 2.0f) &&
               configurePeak(chain.sections[2], 3000.0f, 1.0f, 3.5f);
    }
    return false;
}

}  // namespace player
}  // namespace adv_walkman
