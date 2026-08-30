#include "player/audio/M5SpeakerPcmOutput.h"

#include <algorithm>
#include <cstdint>

namespace adv_walkman {
namespace player {

M5SpeakerPcmOutput::M5SpeakerPcmOutput(m5::Speaker_Class* speaker)
    : speaker_(speaker) {}

bool M5SpeakerPcmOutput::begin() {
    bufferIndex_ = 0;
    activeBuffer_ = 0;
    sampleRateHz_ = 0;
    submittedFrames_ = 0;
    activeBufferProcessed_ = false;
    dsp_.resetStream();
    return speaker_ != nullptr;
}

bool M5SpeakerPcmOutput::SetRate(int hz) {
    if (hz <= 0) {
        return false;
    }
    sampleRateHz_ = static_cast<uint32_t>(hz);
    // Invalid DSP coefficients fall back to Original internally. Playback
    // remains available as long as the AudioOutput rate itself is valid.
    dsp_.setSampleRate(sampleRateHz_);
    return AudioOutput::SetRate(hz);
}

bool M5SpeakerPcmOutput::queueBuffer() {
    if (bufferIndex_ == 0) {
        return true;
    }
    if (speaker_ == nullptr || sampleRateHz_ == 0) {
        ++backpressureEvents_;
        return false;
    }

    const size_t frames = bufferIndex_;
    if (!activeBufferProcessed_) {
        const uint32_t startedAtUs = micros();
        dsp_.processBlock(buffers_[activeBuffer_], frames);
        dspBlockMaxUs_ = std::max<uint16_t>(dspBlockMaxUs_,
            static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX,
                                                     micros() - startedAtUs)));
        activeBufferProcessed_ = true;
    }
    if (!speaker_->playRaw(buffers_[activeBuffer_], frames, sampleRateHz_,
                           false, 1, kVirtualChannel)) {
        ++backpressureEvents_;
        return false;
    }

    // A prepared buffer is not audible until M5.Speaker accepts it.  Complete
    // the application metric only after the real PCM submission succeeds.
    if (presetApplyPending_) {
        const uint16_t elapsedMs = static_cast<uint16_t>(
            static_cast<uint16_t>(millis()) - presetRequestedAtMs_);
        presetApplyLatencyMaxMs_ =
            std::max<uint16_t>(presetApplyLatencyMaxMs_, elapsedMs);
        presetApplyPending_ = false;
    }

    submittedFrames_ += frames;
    const uint32_t submittedAtUs = micros();
    if (pcmSubmitObservedSinceReset_) {
        const uint32_t gapUs = submittedAtUs - pcmLastSubmitAtUs_;
        if (gapUs > pcmSubmitGapMaxUs_) {
            pcmSubmitGapMaxUs_ = gapUs;
        }
        if (gapUs > 100000) {
            ++pcmSubmitGapOver100Ms_;
        }
    }
    pcmLastSubmitAtUs_ = submittedAtUs;
    pcmSubmitObservedSinceReset_ = true;
    pcmFramesSinceReset_ += frames;
    ++pcmBuffersSinceReset_;
    activeBuffer_ = (activeBuffer_ + 1) % kBufferCount;
    bufferIndex_ = 0;
    activeBufferProcessed_ = false;
    return true;
}

bool M5SpeakerPcmOutput::ConsumeSample(int16_t sample[2]) {
    if (bufferIndex_ >= kFramesPerBuffer) {
        if (!queueBuffer()) {
            return false;
        }
        // playRaw normally waits for a queue slot. Yield after one submitted
        // buffer so AudioGeneratorMP3::loop() cannot monopolize loop(). The
        // unconsumed lastSample is retried by ESP8266Audio on the next call.
        return false;
    }

    MakeSampleStereo16(sample);
    const int32_t mixed = static_cast<int32_t>(sample[LEFTCHANNEL]) +
                          static_cast<int32_t>(sample[RIGHTCHANNEL]);
    buffers_[activeBuffer_][bufferIndex_++] = static_cast<int16_t>(mixed / 2);
    return true;
}

void M5SpeakerPcmOutput::flush() {
    queueBuffer();
}

bool M5SpeakerPcmOutput::flushForDrain() {
    return queueBuffer();
}

bool M5SpeakerPcmOutput::isDrained() const {
    return bufferIndex_ == 0 &&
           (speaker_ == nullptr ||
            speaker_->isPlaying(kVirtualChannel) == 0);
}

bool M5SpeakerPcmOutput::stop() {
    // Manual stop/seek intentionally discards pending PCM. Natural EOF uses
    // flushForDrain() and waits for isDrained() before reaching this path.
    if (speaker_ != nullptr) {
        speaker_->stop(kVirtualChannel);
    }
    bufferIndex_ = 0;
    activeBuffer_ = 0;
    activeBufferProcessed_ = false;
    sampleRateHz_ = 0;
    submittedFrames_ = 0;
    dsp_.resetStream();
    presetApplyPending_ = false;
    return true;
}

void M5SpeakerPcmOutput::resetDiagnostics() {
    backpressureEvents_ = 0;
    pcmFramesSinceReset_ = 0;
    pcmBuffersSinceReset_ = 0;
    pcmSubmitGapMaxUs_ = 0;
    pcmSubmitGapOver100Ms_ = 0;
    pcmLastSubmitAtUs_ = 0;
    pcmSubmitObservedSinceReset_ = false;
    dspBlockMaxUs_ = 0;
    presetApplyLatencyMaxMs_ = 0;
    dsp_.resetDiagnostics();
}

void M5SpeakerPcmOutput::breakSubmitGapWindow() {
    pcmLastSubmitAtUs_ = 0;
    pcmSubmitObservedSinceReset_ = false;
}

bool M5SpeakerPcmOutput::setSoundPreset(SoundPreset preset, bool smooth) {
    const bool changed = preset != dsp_.preset();
    const bool result = dsp_.setPreset(preset, smooth);
    if (result && changed && smooth) {
        presetRequestedAtMs_ = static_cast<uint16_t>(millis());
        presetApplyPending_ = true;
    }
    return result;
}

uint32_t M5SpeakerPcmOutput::sampleRateHz() const {
    return sampleRateHz_;
}

uint64_t M5SpeakerPcmOutput::submittedFrames() const {
    return submittedFrames_;
}

uint32_t M5SpeakerPcmOutput::backpressureEvents() const {
    return backpressureEvents_;
}

uint64_t M5SpeakerPcmOutput::pcmFramesSinceReset() const {
    return pcmFramesSinceReset_;
}

uint32_t M5SpeakerPcmOutput::pcmBuffersSinceReset() const {
    return pcmBuffersSinceReset_;
}

uint32_t M5SpeakerPcmOutput::pcmSubmitGapMaxUs() const {
    return pcmSubmitGapMaxUs_;
}

uint32_t M5SpeakerPcmOutput::pcmSubmitGapOver100Ms() const {
    return pcmSubmitGapOver100Ms_;
}

uint32_t M5SpeakerPcmOutput::pcmLastSubmitAgeUs() const {
    if (!pcmSubmitObservedSinceReset_) {
        return UINT32_MAX;
    }
    return micros() - pcmLastSubmitAtUs_;
}

}  // namespace player
}  // namespace adv_walkman
