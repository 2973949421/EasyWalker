#include "CandidateBBackend.h"

#include <SD.h>

#include <cstring>

#include "BenchmarkSupport.h"

namespace adv_walkman {

MonoDirectI2SOutput::MonoDirectI2SOutput()
    : AudioOutputI2S(1, AudioOutputI2S::EXTERNAL_I2S, 8,
                     AudioOutputI2S::APLL_DISABLE) {
    SetPinout(41, 43, 42);
    // Match Candidate A's M5.Speaker volume=128 quadratic curve:
    // (128 / 255)^2 is approximately 0.25 linear amplitude.
    SetGain(0.25f);
}

bool MonoDirectI2SOutput::SetRate(int hz) {
    if (hz <= 0) {
        return false;
    }
    sampleRate_ = static_cast<uint32_t>(hz);
    return AudioOutputI2S::SetRate(hz);
}

bool MonoDirectI2SOutput::ConsumeSample(int16_t sample[2]) {
    const int16_t mono = downmixStereoToMono(sample[LEFTCHANNEL],
                                             sample[RIGHTCHANNEL]);
    int16_t monoPair[2] = {mono, mono};
    const bool accepted = AudioOutputI2S::ConsumeSample(monoPair);
    if (!accepted) {
        ++backpressureEvents_;
    }
    return accepted;
}

void MonoDirectI2SOutput::resetStats() {
    backpressureEvents_ = 0;
}

uint32_t MonoDirectI2SOutput::sampleRate() const {
    return sampleRate_;
}

uint32_t MonoDirectI2SOutput::backpressureEvents() const {
    return backpressureEvents_;
}

CandidateBBackend::CandidateBBackend() = default;

bool CandidateBBackend::begin(const char* path) {
    closePlayback();
    state_ = BackendState::BOOT;
    error_ = "none";
    decodeServiceCalls_ = 0;
    bytesRead_ = 0;
    lastFilePosition_ = 0;
    trackLoops_ = 0;
    serviceMaxUs_ = 0;
    startedAtMs_ = 0;
    output_.resetStats();
    strncpy(path_, path, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    strcpy(fileSha256_, "unavailable");

    if (!mountBenchmarkSd()) {
        return fail("sd_mount_failed");
    }
    if (!SD.exists(path_)) {
        return fail("benchmark_mp3_missing");
    }
    if (!computeBenchmarkFileSha256(path_, fileSha256_)) {
        return fail("sha256_failed");
    }
    if (!codec_.begin()) {
        return fail("es8311_init_failed");
    }

    startedAtMs_ = millis();
    return startFile(0, false);
}

bool CandidateBBackend::startFile(uint32_t byteOffset, bool preservePause) {
    const bool wasPaused = preservePause && state_ == BackendState::PAUSED;
    closePlayback();
    if (!file_.open(path_)) {
        return fail("mp3_open_failed");
    }
    if (byteOffset > 0 && !file_.seek(static_cast<int32_t>(byteOffset), SEEK_SET)) {
        file_.close();
        return fail("mp3_seek_failed");
    }
    lastFilePosition_ = file_.getPos();
    if (!decoder_.begin(&file_, &output_)) {
        file_.close();
        return fail("decoder_begin_failed");
    }
    error_ = "none";
    state_ = wasPaused ? BackendState::PAUSED : BackendState::PLAYING;
    return true;
}

void CandidateBBackend::updateBytesRead() {
    if (!file_.isOpen()) {
        return;
    }
    const uint32_t position = file_.getPos();
    if (position >= lastFilePosition_) {
        bytesRead_ += position - lastFilePosition_;
    }
    lastFilePosition_ = position;
}

void CandidateBBackend::service() {
    if (state_ != BackendState::PLAYING) {
        return;
    }
    const uint32_t startedUs = micros();
    ++decodeServiceCalls_;
    const bool running = decoder_.loop();
    updateBytesRead();
    const uint32_t durationUs = micros() - startedUs;
    if (durationUs > serviceMaxUs_) {
        serviceMaxUs_ = durationUs;
    }

    if (!running) {
        closePlayback();
        if (loopEnabled_) {
            ++trackLoops_;
            startFile(0, false);
        } else {
            state_ = BackendState::STOPPED;
        }
    }
}

bool CandidateBBackend::pause() {
    if (state_ != BackendState::PLAYING) {
        return false;
    }
    state_ = BackendState::PAUSED;
    return true;
}

bool CandidateBBackend::resume() {
    if (state_ != BackendState::PAUSED) {
        return false;
    }
    state_ = BackendState::PLAYING;
    return true;
}

void CandidateBBackend::stop() {
    updateBytesRead();
    closePlayback();
    codec_.end();
    state_ = BackendState::STOPPED;
    error_ = "none";
}

bool CandidateBBackend::restart() {
    return startFile(0, state_ == BackendState::PAUSED);
}

bool CandidateBBackend::seekSeconds(uint32_t seconds) {
    if (!file_.isOpen()) {
        return false;
    }
    const uint32_t offset =
        benchmarkByteOffsetForSeconds(seconds, file_.getSize());
    return startFile(offset, state_ == BackendState::PAUSED);
}

void CandidateBBackend::setLoop(bool enabled) {
    loopEnabled_ = enabled;
}

void CandidateBBackend::closePlayback() {
    if (decoder_.isRunning()) {
        decoder_.stop();
    }
    output_.stop();
    if (file_.isOpen()) {
        file_.close();
    }
}

bool CandidateBBackend::fail(const char* error) {
    closePlayback();
    codec_.end();
    error_ = error;
    state_ = BackendState::ERROR;
    return false;
}

BenchmarkStats CandidateBBackend::stats() const {
    return {
        BackendId::B_DirectI2S,
        state_,
        output_.sampleRate(),
        startedAtMs_ == 0 ? 0 : millis() - startedAtMs_,
        decodeServiceCalls_,
        output_.backpressureEvents(),
        bytesRead_,
        trackLoops_,
        -1,
        -1,
        -1,
        serviceMaxUs_,
        fileSha256_,
        error_,
    };
}

const char* CandidateBBackend::name() const {
    return "B_DIRECT_I2S";
}

}  // namespace adv_walkman
