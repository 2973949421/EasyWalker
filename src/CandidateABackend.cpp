#include "CandidateABackend.h"

#include <SD.h>
#include <SPI.h>
#include <mbedtls/sha256.h>

#include <cstring>

namespace adv_walkman {

namespace {

constexpr int16_t downmixStereoToMono(int16_t left, int16_t right) {
    return static_cast<int16_t>(
        (static_cast<int32_t>(left) + static_cast<int32_t>(right)) / 2);
}

static_assert(downmixStereoToMono(32767, 32767) == 32767,
              "positive full-scale downmix must not overflow");
static_assert(downmixStereoToMono(-32768, -32768) == -32768,
              "negative full-scale downmix must not overflow");
static_assert(downmixStereoToMono(32767, -32768) == 0,
              "opposite-polarity channels should cancel around zero");

}  // namespace

MonoM5SpeakerOutput::MonoM5SpeakerOutput(m5::Speaker_Class* speaker)
    : speaker_(speaker) {}

bool MonoM5SpeakerOutput::begin() {
    bufferIndex_ = 0;
    activeBuffer_ = 0;
    sampleRate_ = 0;
    backpressureEvents_ = 0;
    return speaker_ != nullptr;
}

bool MonoM5SpeakerOutput::SetRate(int hz) {
    if (hz <= 0) {
        return false;
    }
    sampleRate_ = static_cast<uint32_t>(hz);
    return AudioOutput::SetRate(hz);
}

bool MonoM5SpeakerOutput::ConsumeSample(int16_t sample[2]) {
    if (bufferIndex_ >= kFramesPerBuffer) {
        flush();
        ++backpressureEvents_;
        return false;
    }

    MakeSampleStereo16(sample);
    buffers_[activeBuffer_][bufferIndex_++] =
        downmixStereoToMono(sample[LEFTCHANNEL], sample[RIGHTCHANNEL]);
    return true;
}

void MonoM5SpeakerOutput::flush() {
    if (bufferIndex_ == 0 || speaker_ == nullptr) {
        return;
    }
    speaker_->playRaw(
        buffers_[activeBuffer_], bufferIndex_, sampleRate_, false, 1,
        kVirtualChannel);
    activeBuffer_ = (activeBuffer_ + 1) % kBufferCount;
    bufferIndex_ = 0;
}

bool MonoM5SpeakerOutput::stop() {
    flush();
    if (speaker_ != nullptr) {
        speaker_->stop(kVirtualChannel);
    }
    return true;
}

uint32_t MonoM5SpeakerOutput::sampleRate() const {
    return sampleRate_;
}

uint32_t MonoM5SpeakerOutput::backpressureEvents() const {
    return backpressureEvents_;
}

CandidateABackend::CandidateABackend() : output_(&M5.Speaker) {}

bool CandidateABackend::mountSd() {
    if (sdMounted_) {
        return true;
    }

    // Cardputer ADV official M5Cardputer examples use this SPI mapping.
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    sdMounted_ = SD.begin(kSdCs, SPI, kSdFrequencyHz);
    return sdMounted_;
}

bool CandidateABackend::computeFileSha256(const char* path) {
    File input = SD.open(path, FILE_READ);
    if (!input) {
        return false;
    }

    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    if (mbedtls_sha256_starts_ret(&context, 0) != 0) {
        mbedtls_sha256_free(&context);
        input.close();
        return false;
    }

    uint8_t buffer[1024];
    while (input.available()) {
        const size_t count = input.read(buffer, sizeof(buffer));
        if (count == 0 ||
            mbedtls_sha256_update_ret(&context, buffer, count) != 0) {
            mbedtls_sha256_free(&context);
            input.close();
            return false;
        }
    }

    uint8_t digest[32];
    const int result = mbedtls_sha256_finish_ret(&context, digest);
    mbedtls_sha256_free(&context);
    input.close();
    if (result != 0) {
        return false;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t index = 0; index < sizeof(digest); ++index) {
        fileSha256_[index * 2] = kHex[digest[index] >> 4];
        fileSha256_[index * 2 + 1] = kHex[digest[index] & 0x0F];
    }
    fileSha256_[64] = '\0';
    return true;
}

bool CandidateABackend::begin(const char* path) {
    closePlayback();
    state_ = BackendState::BOOT;
    error_ = "none";
    decodeServiceCalls_ = 0;
    startedAtMs_ = 0;
    strcpy(fileSha256_, "unavailable");

    if (!mountSd()) {
        return fail("sd_mount_failed");
    }
    if (!SD.exists(path)) {
        return fail("benchmark_mp3_missing");
    }
    if (!computeFileSha256(path)) {
        return fail("sha256_failed");
    }

    if (!M5.Speaker.isRunning() && !M5.Speaker.begin()) {
        return fail("speaker_begin_failed");
    }
    M5.Speaker.setVolume(kInitialVolume);

    if (!file_.open(path)) {
        return fail("mp3_open_failed");
    }
    if (!decoder_.begin(&file_, &output_)) {
        return fail("decoder_begin_failed");
    }

    startedAtMs_ = millis();
    state_ = BackendState::PLAYING;
    return true;
}

void CandidateABackend::service() {
    if (state_ != BackendState::PLAYING) {
        return;
    }
    ++decodeServiceCalls_;
    if (!decoder_.loop()) {
        closePlayback();
        state_ = BackendState::STOPPED;
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
    closePlayback();
    state_ = BackendState::STOPPED;
    error_ = "none";
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
        -1,
        fileSha256_,
        error_,
    };
}

const char* CandidateABackend::name() const {
    return "A_M5SPEAKER";
}

}  // namespace adv_walkman
