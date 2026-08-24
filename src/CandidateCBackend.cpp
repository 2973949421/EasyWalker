#include "CandidateCBackend.h"

#include <SD.h>

#include <cstring>

#include "BenchmarkSupport.h"

namespace adv_walkman {

MonoBackgroundOutput::MonoBackgroundOutput() : output_(41, 43, 42) {}

bool MonoBackgroundOutput::setBuffers(size_t buffers, size_t bufferWords,
                                      int32_t silenceSample) {
    return output_.setBuffers(buffers, bufferWords, silenceSample);
}

bool MonoBackgroundOutput::setBitsPerSample(int bits) {
    return output_.setBitsPerSample(bits);
}

bool MonoBackgroundOutput::setFrequency(int frequency) {
    if (frequency > 0) {
        sampleRate_ = static_cast<uint32_t>(frequency);
    }
    return output_.setFrequency(frequency);
}

bool MonoBackgroundOutput::setStereo(bool stereo) {
    // BackgroundAudio emits interleaved stereo frames. Both I2S slots remain
    // enabled after the samples are downmixed and duplicated in write().
    return stereo && output_.setStereo(true);
}

bool MonoBackgroundOutput::begin() {
    return output_.begin();
}

bool MonoBackgroundOutput::end() {
    return output_.end();
}

bool MonoBackgroundOutput::getUnderflow() {
    return output_.getUnderflow();
}

void MonoBackgroundOutput::onTransmit(void (*callback)(void*), void* data) {
    output_.onTransmit(callback, data);
}

size_t MonoBackgroundOutput::write(const uint8_t* buffer, size_t size) {
    if ((size % (sizeof(int16_t) * 2)) != 0 ||
        size > sizeof(scratch_)) {
        ++backpressureEvents_;
        return 0;
    }

    const int16_t* input = reinterpret_cast<const int16_t*>(buffer);
    const size_t frames = size / (sizeof(int16_t) * 2);
    for (size_t frame = 0; frame < frames; ++frame) {
        const int16_t mono = downmixStereoToMono(input[frame * 2],
                                                 input[frame * 2 + 1]);
        scratch_[frame * 2] = mono;
        scratch_[frame * 2 + 1] = mono;
    }

    const size_t written =
        output_.write(reinterpret_cast<const uint8_t*>(scratch_), size);
    if (written != size) {
        ++backpressureEvents_;
    }
    return written;
}

size_t MonoBackgroundOutput::write(uint8_t value) {
    (void)value;
    return 0;
}

int MonoBackgroundOutput::availableForWrite() {
    return output_.availableForWrite();
}

void MonoBackgroundOutput::resetStats() {
    backpressureEvents_ = 0;
}

uint32_t MonoBackgroundOutput::sampleRate() const {
    return sampleRate_;
}

uint32_t MonoBackgroundOutput::backpressureEvents() const {
    return backpressureEvents_;
}

uint32_t MonoBackgroundOutput::outputUnderflows() const {
    return const_cast<ESP32I2SAudio&>(output_).underflows();
}

CandidateCBackend::CandidateCBackend() : decoder_(output_) {}

bool CandidateCBackend::begin(const char* path) {
    closePlayback();
    state_ = BackendState::BOOT;
    error_ = "none";
    decodeServiceCalls_ = 0;
    bytesRead_ = 0;
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
    decoder_.setGain(0.25f);
    if (!decoder_.begin()) {
        return fail("background_decoder_begin_failed");
    }
    decoderErrorsBaseline_ = decoder_.errors();
    decoderUnderflowsBaseline_ = decoder_.underflows();
    outputUnderflowsBaseline_ = output_.outputUnderflows();

    startedAtMs_ = millis();
    return startFile(0, false);
}

bool CandidateCBackend::startFile(uint32_t byteOffset, bool preservePause) {
    const bool wasPaused = preservePause && state_ == BackendState::PAUSED;
    closeFile();
    decoder_.flush();
    file_ = SD.open(path_, FILE_READ);
    if (!file_) {
        return fail("mp3_open_failed");
    }
    if (byteOffset > 0 && !file_.seek(byteOffset)) {
        closeFile();
        return fail("mp3_seek_failed");
    }
    eofReached_ = false;
    if (wasPaused) {
        decoder_.pause();
        state_ = BackendState::PAUSED;
    } else {
        decoder_.unpause();
        state_ = BackendState::PLAYING;
    }
    error_ = "none";
    return true;
}

void CandidateCBackend::service() {
    if (state_ != BackendState::PLAYING) {
        return;
    }

    const uint32_t startedUs = micros();
    ++decodeServiceCalls_;
    uint8_t feedCount = 0;
    while (!eofReached_ &&
           decoder_.availableForWrite() >= kFeedChunkSize &&
           feedCount < kMaximumFeedsPerService) {
        const size_t count = file_.read(feedBuffer_, sizeof(feedBuffer_));
        if (count == 0) {
            eofReached_ = true;
            closeFile();
            break;
        }
        const size_t written = decoder_.write(feedBuffer_, count);
        if (written != count) {
            fail("background_feed_short_write");
            return;
        }
        bytesRead_ += static_cast<uint32_t>(count);
        ++feedCount;
        if (count < sizeof(feedBuffer_)) {
            eofReached_ = true;
            closeFile();
            break;
        }
    }

    if (eofReached_ && decoder_.done()) {
        if (loopEnabled_) {
            ++trackLoops_;
            startFile(0, false);
        } else {
            decoder_.end();
            codec_.end();
            state_ = BackendState::STOPPED;
        }
    }

    const uint32_t durationUs = micros() - startedUs;
    if (durationUs > serviceMaxUs_) {
        serviceMaxUs_ = durationUs;
    }
}

bool CandidateCBackend::pause() {
    if (state_ != BackendState::PLAYING) {
        return false;
    }
    decoder_.pause();
    state_ = BackendState::PAUSED;
    return true;
}

bool CandidateCBackend::resume() {
    if (state_ != BackendState::PAUSED) {
        return false;
    }
    decoder_.unpause();
    state_ = BackendState::PLAYING;
    return true;
}

void CandidateCBackend::stop() {
    closePlayback();
    state_ = BackendState::STOPPED;
    error_ = "none";
}

bool CandidateCBackend::restart() {
    if (!decoder_.playing()) {
        if (!codec_.begin() || !decoder_.begin()) {
            return fail("background_restart_failed");
        }
    }
    return startFile(0, state_ == BackendState::PAUSED);
}

bool CandidateCBackend::seekSeconds(uint32_t seconds) {
    const uint32_t fileSize = file_ ? file_.size() : 0;
    if (fileSize == 0) {
        return false;
    }
    const uint32_t offset =
        benchmarkByteOffsetForSeconds(seconds, fileSize);
    return startFile(offset, state_ == BackendState::PAUSED);
}

void CandidateCBackend::setLoop(bool enabled) {
    loopEnabled_ = enabled;
}

void CandidateCBackend::closeFile() {
    if (file_) {
        file_.close();
    }
}

void CandidateCBackend::closePlayback() {
    closeFile();
    if (decoder_.playing()) {
        decoder_.end();
    }
    codec_.end();
}

bool CandidateCBackend::fail(const char* error) {
    closePlayback();
    error_ = error;
    state_ = BackendState::ERROR;
    return false;
}

BenchmarkStats CandidateCBackend::stats() const {
    // BackgroundAudio 1.4.4's counter accessors do not mutate state, but the
    // upstream API does not declare them const.
    auto& decoder = const_cast<BackgroundAudioMP3&>(decoder_);
    return {
        BackendId::C_BackgroundAudio,
        state_,
        output_.sampleRate(),
        startedAtMs_ == 0 ? 0 : millis() - startedAtMs_,
        decodeServiceCalls_,
        output_.backpressureEvents(),
        bytesRead_,
        trackLoops_,
        static_cast<int32_t>(decoder.errors() - decoderErrorsBaseline_),
        static_cast<int32_t>(decoder.underflows() -
                             decoderUnderflowsBaseline_),
        static_cast<int32_t>(output_.outputUnderflows() -
                             outputUnderflowsBaseline_),
        serviceMaxUs_,
        fileSha256_,
        error_,
    };
}

const char* CandidateCBackend::name() const {
    return "C_BACKGROUND_AUDIO";
}

}  // namespace adv_walkman
