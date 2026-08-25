#include "CandidateABackend.h"

#include <SD.h>

#include <cstring>

#include "BenchmarkSupport.h"

namespace adv_walkman {

MonoM5SpeakerOutput::MonoM5SpeakerOutput(m5::Speaker_Class* speaker)
    : speaker_(speaker) {}

bool MonoM5SpeakerOutput::begin() {
    bufferIndex_ = 0;
    activeBuffer_ = 0;
    sampleRate_ = 0;
    return speaker_ != nullptr;
}

bool MonoM5SpeakerOutput::SetRate(int hz) {
    if (hz <= 0) {
        return false;
    }
    sampleRate_ = static_cast<uint32_t>(hz);
    return AudioOutput::SetRate(hz);
}

bool MonoM5SpeakerOutput::queueBuffer() {
    if (bufferIndex_ == 0 || speaker_ == nullptr) {
        return true;
    }
    if (!speaker_->playRaw(buffers_[activeBuffer_], bufferIndex_, sampleRate_,
                           false, 1, kVirtualChannel)) {
        ++backpressureEvents_;
        return false;
    }
    activeBuffer_ = (activeBuffer_ + 1) % kBufferCount;
    bufferIndex_ = 0;
    return true;
}

bool MonoM5SpeakerOutput::ConsumeSample(int16_t sample[2]) {
    if (bufferIndex_ >= kFramesPerBuffer) {
        if (!queueBuffer()) {
            return false;
        }
        // M5.Speaker waits for a queue slot and normally returns true. Yield
        // cooperatively after each submitted buffer so decoder.loop() returns
        // control to the benchmark harness without counting backpressure.
        return false;
    }

    MakeSampleStereo16(sample);
    buffers_[activeBuffer_][bufferIndex_++] = downmixStereoToMono(
        sample[LEFTCHANNEL], sample[RIGHTCHANNEL]);
    return true;
}

void MonoM5SpeakerOutput::flush() {
    queueBuffer();
}

bool MonoM5SpeakerOutput::stop() {
    flush();
    if (speaker_ != nullptr) {
        speaker_->stop(kVirtualChannel);
    }
    bufferIndex_ = 0;
    return true;
}

void MonoM5SpeakerOutput::resetStats() {
    backpressureEvents_ = 0;
}

uint32_t MonoM5SpeakerOutput::sampleRate() const {
    return sampleRate_;
}

uint32_t MonoM5SpeakerOutput::backpressureEvents() const {
    return backpressureEvents_;
}

CandidateABackend::CandidateABackend() : output_(&M5.Speaker) {}

bool CandidateABackend::begin(const char* path) {
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
    if (!M5.Speaker.isRunning() && !M5.Speaker.begin()) {
        return fail("speaker_begin_failed");
    }
    M5.Speaker.setVolume(kInitialVolume);

    startedAtMs_ = millis();
    return startFile(0, false);
}

bool CandidateABackend::startFile(uint32_t byteOffset, bool preservePause) {
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

void CandidateABackend::updateBytesRead() {
    if (!file_.isOpen()) {
        return;
    }
    const uint32_t position = file_.getPos();
    if (position >= lastFilePosition_) {
        bytesRead_ += position - lastFilePosition_;
    }
    lastFilePosition_ = position;
}

void CandidateABackend::service() {
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

bool CandidateABackend::pause() {
    if (state_ != BackendState::PLAYING) {
        return false;
    }
    state_ = BackendState::PAUSED;
    return true;
}

bool CandidateABackend::resume() {
    if (state_ != BackendState::PAUSED) {
        return false;
    }
    state_ = BackendState::PLAYING;
    return true;
}

void CandidateABackend::stop() {
    updateBytesRead();
    closePlayback();
    state_ = BackendState::STOPPED;
    error_ = "none";
}

bool CandidateABackend::restart() {
    return startFile(0, state_ == BackendState::PAUSED);
}

bool CandidateABackend::seekSeconds(uint32_t seconds) {
    if (!file_.isOpen()) {
        return false;
    }
    const uint32_t offset =
        benchmarkByteOffsetForSeconds(seconds, file_.getSize());
    return startFile(offset, state_ == BackendState::PAUSED);
}

void CandidateABackend::setLoop(bool enabled) {
    loopEnabled_ = enabled;
}

void CandidateABackend::closePlayback() {
    if (decoder_.isRunning()) {
        decoder_.stop();
    }
    output_.stop();
    if (file_.isOpen()) {
        file_.close();
    }
}

bool CandidateABackend::fail(const char* error) {
    closePlayback();
    error_ = error;
    state_ = BackendState::ERROR;
    return false;
}

BenchmarkStats CandidateABackend::stats() const {
    return {
        BackendId::A_M5Speaker,
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

const char* CandidateABackend::name() const {
    return "A_M5SPEAKER";
}

}  // namespace adv_walkman
