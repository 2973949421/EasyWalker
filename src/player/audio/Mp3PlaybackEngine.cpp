#include "player/audio/Mp3PlaybackEngine.h"

#include <SD.h>
#include <algorithm>
#include <cstring>

namespace adv_walkman {
namespace player {

namespace {

AudioError probeErrorToAudioError(Mp3ProbeError error,
                                  AudioError fallback) {
    switch (error) {
        case Mp3ProbeError::OpenFailed:
            return AudioError::FileOpenFailed;
        case Mp3ProbeError::IoError:
            return AudioError::ProbeFailed;
        case Mp3ProbeError::Unsupported:
            return fallback;
        case Mp3ProbeError::None:
            break;
    }
    return fallback;
}

}  // namespace

bool PlaybackMp3Decoder::begin(AudioFileSource* source, AudioOutput* output) {
    terminalStreamError_ = -1;
    terminalStreamErrorValid_ = false;
    // ESP8266Audio consumes lastSample before decoding the first new sample.
    // Clear it so seek/track changes cannot replay PCM from the old stream.
    lastSample[0] = 0;
    lastSample[1] = 0;
    return AudioGeneratorMP3::begin(source, output);
}

bool PlaybackMp3Decoder::loop() {
    const bool runningNow = AudioGeneratorMP3::loop();
    if (!runningNow) {
        latchStreamError();
    } else if (stream != nullptr) {
        // libmad does not clear stream->error after a successful frame. A
        // normal input-buffer refill therefore leaves stale BUFLEN behind.
        // Reaching our cooperative yield proves this loop invocation remains
        // healthy; clear the stale code so only a BUFLEN from the terminal
        // invocation is available for terminal classification.
        stream->error = MAD_ERROR_NONE;
    }
    return runningNow;
}

bool PlaybackMp3Decoder::stop() {
    // AudioGeneratorMP3 may call virtual stop() after repeated BUFLEN and free
    // the libmad stream before returning from loop(). Latch first so a
    // truncated final frame cannot later look like a clean EOF.
    latchStreamError();
    return AudioGeneratorMP3::stop();
}

void PlaybackMp3Decoder::latchStreamError() {
    if (stream != nullptr) {
        terminalStreamError_ = static_cast<int>(stream->error);
        terminalStreamErrorValid_ = true;
    }
}

int PlaybackMp3Decoder::streamErrorCode() const {
    if (stream != nullptr) {
        return static_cast<int>(stream->error);
    }
    return terminalStreamErrorValid_ ? terminalStreamError_ : -1;
}

Mp3PlaybackEngine::Mp3PlaybackEngine() : output_(&M5.Speaker) {}

Mp3PlaybackEngine::~Mp3PlaybackEngine() {
    closePlaybackImmediate();
}

bool Mp3PlaybackEngine::begin() {
    closePlaybackImmediate();
    resetDiagnostics();
    if (!M5.Speaker.isRunning() && !M5.Speaker.begin()) {
        initialized_ = false;
        return fail(AudioError::SpeakerBeginFailed);
    }
    M5.Speaker.setVolume(volume_);
    initialized_ = true;
    state_ = AudioState::Empty;
    error_ = AudioError::None;
    eventPending_ = false;
    return true;
}

bool Mp3PlaybackEngine::open(const char* path, uint32_t positionMs,
                             bool startPaused, uint32_t sourceOffsetHint) {
    const uint32_t startedUs = micros();
    const auto finishOpen = [this, startedUs](bool result) {
        const uint32_t elapsedUs = micros() - startedUs;
        if (elapsedUs > openMaxUs_) {
            openMaxUs_ = elapsedUs;
        }
        return result;
    };
    eventPending_ = false;
    if (!initialized_) {
        return finishOpen(fail(AudioError::NotInitialized));
    }
    if (path == nullptr || path[0] == '\0') {
        return finishOpen(fail(AudioError::InvalidArgument));
    }
    const size_t pathLength = std::strlen(path);
    if (pathLength >= sizeof(path_)) {
        return finishOpen(fail(AudioError::PathTooLong));
    }

    closePlaybackImmediate();
    std::memcpy(path_, path, pathLength + 1);
    mp3Info_ = Mp3Info{};
    state_ = AudioState::Loading;
    error_ = AudioError::None;
    positionBaseMs_ = 0;
    sourceByteOffset_ = 0;
    drainFlushed_ = false;
    eventPending_ = false;

    Mp3ProbeError probeError = Mp3ProbeError::None;
    if (!Mp3Probe::probe(SD, path_, mp3Info_, probeError)) {
        return finishOpen(fail(probeErrorToAudioError(
            probeError, AudioError::UnsupportedFormat)));
    }

    Mp3SeekPoint point;
    if (!Mp3Probe::seekPoint(SD, path_, mp3Info_, positionMs, point,
                             probeError, sourceOffsetHint)) {
        return finishOpen(fail(
            probeErrorToAudioError(probeError, AudioError::SeekFailed)));
    }
    return finishOpen(startAt(point, startPaused));
}

bool Mp3PlaybackEngine::restartCurrent(bool startPaused) {
    const uint32_t startedUs = micros();
    ++repeatRestartCount_;
    const auto finishRestart = [this, startedUs](bool result) {
        const uint32_t elapsedUs = micros() - startedUs;
        if (elapsedUs > repeatRestartMaxUs_) {
            repeatRestartMaxUs_ = elapsedUs;
        }
        return result;
    };

    eventPending_ = false;
    if (!initialized_) {
        return finishRestart(fail(AudioError::NotInitialized));
    }
    if (path_[0] == '\0' || mp3Info_.sampleRateHz == 0 ||
        mp3Info_.firstFrameOffset >= mp3Info_.audioEndOffset) {
        return finishRestart(fail(AudioError::InvalidArgument));
    }

    Mp3SeekPoint beginning;
    beginning.byteOffset = mp3Info_.firstFrameOffset;
    beginning.positionMs = 0;
    return finishRestart(startAt(beginning, startPaused));
}

bool Mp3PlaybackEngine::startAt(const Mp3SeekPoint& point,
                                bool startPaused) {
    closePlaybackImmediate();
    state_ = AudioState::Loading;
    error_ = AudioError::None;
    drainFlushed_ = false;

    if (!source_.open(path_)) {
        return fail(AudioError::FileOpenFailed);
    }
    source_.setReadLimit(mp3Info_.audioEndOffset);
    if (!source_.seek(static_cast<int32_t>(point.byteOffset), SEEK_SET)) {
        return fail(AudioError::SeekFailed);
    }
    sourceByteOffset_ = source_.getPos();
    positionBaseMs_ = point.positionMs;

    if (!decoder_.begin(&source_, &output_)) {
        return fail(AudioError::DecoderBeginFailed);
    }
    state_ = startPaused ? AudioState::Paused : AudioState::Playing;
    return true;
}

void Mp3PlaybackEngine::service() {
    const uint32_t startedUs = micros();
    if (state_ == AudioState::Playing) {
        servicePlaying();
    } else if (state_ == AudioState::Draining) {
        serviceDraining();
    } else {
        return;
    }
    const uint32_t elapsedUs = micros() - startedUs;
    if (elapsedUs > serviceMaxUs_) {
        serviceMaxUs_ = elapsedUs;
    }
}

void Mp3PlaybackEngine::servicePlaying() {
    const bool running = decoder_.loop();
    updateSourceOffset();
    if (running) {
        return;
    }

    if (source_.readError()) {
        fail(AudioError::ReadFailed);
        return;
    }
    if (!source_.eofObserved()) {
        fail(AudioError::DecoderFailed);
        return;
    }

    // ESP8266Audio/libmad leaves MAD_ERROR_BUFLEN at an ordinary physical EOF.
    // Mp3Probe rejects a stream whose Xing/Info/VBRI byte claim exceeds the
    // available audio payload, so BUFLEN is expected here for a complete file.
    // Other terminal errors still represent a decoder failure.
    const int terminalError = decoder_.streamErrorCode();
    if (terminalError != -1 && terminalError != MAD_ERROR_NONE &&
        terminalError != MAD_ERROR_BUFLEN) {
        fail(AudioError::DecoderFailed);
        return;
    }

    state_ = AudioState::Draining;
    drainFlushed_ = output_.flushForDrain();
}

void Mp3PlaybackEngine::serviceDraining() {
    if (!drainFlushed_) {
        drainFlushed_ = output_.flushForDrain();
        return;
    }
    if (!output_.isDrained()) {
        return;
    }

    updateSourceOffset();
    if (decoder_.isRunning()) {
        decoder_.stop();
    } else {
        source_.close();
        output_.stop();
    }
    sourceByteOffset_ = mp3Info_.audioEndOffset;
    positionBaseMs_ = mp3Info_.durationMs;
    state_ = AudioState::Stopped;
    error_ = AudioError::None;
    queueEvent(AudioEventType::TrackEnded, AudioError::None);
}

bool Mp3PlaybackEngine::pause() {
    if (state_ != AudioState::Playing) {
        return false;
    }
    // Position is based on submitted PCM and therefore remains frozen while
    // paused without adjusting the seek base or double-counting frames.
    output_.breakSubmitGapWindow();
    state_ = AudioState::Paused;
    return true;
}

bool Mp3PlaybackEngine::resume() {
    if (state_ != AudioState::Paused) {
        return false;
    }
    state_ = AudioState::Playing;
    return true;
}

void Mp3PlaybackEngine::stop() {
    const bool hasTrack = path_[0] != '\0';
    closePlaybackImmediate();
    positionBaseMs_ = 0;
    sourceByteOffset_ = hasTrack ? mp3Info_.firstFrameOffset : 0;
    error_ = AudioError::None;
    eventPending_ = false;
    state_ = hasTrack ? AudioState::Stopped : AudioState::Empty;
}

bool Mp3PlaybackEngine::seekToMs(uint32_t targetMs) {
    if (state_ != AudioState::Playing && state_ != AudioState::Paused) {
        return false;
    }
    const bool remainPaused = state_ == AudioState::Paused;
    Mp3SeekPoint point;
    Mp3ProbeError probeError = Mp3ProbeError::None;
    if (!Mp3Probe::seekPoint(SD, path_, mp3Info_, targetMs, point,
                             probeError)) {
        return fail(
            probeErrorToAudioError(probeError, AudioError::SeekFailed));
    }
    eventPending_ = false;
    return startAt(point, remainPaused);
}

void Mp3PlaybackEngine::setVolume(uint8_t volume) {
    volume_ = volume;
    if (initialized_) {
        M5.Speaker.setVolume(volume_);
    }
}

bool Mp3PlaybackEngine::pollEvent(AudioEvent& event) {
    if (!eventPending_) {
        return false;
    }
    event = pendingEvent_;
    eventPending_ = false;
    return true;
}

AudioStatus Mp3PlaybackEngine::status() const {
    AudioStatus result;
    result.state = state_;
    result.error = error_;
    result.positionMs = playbackPositionMs();
    result.durationMs = mp3Info_.durationMs;
    result.sourceByteOffset = sourceByteOffset_;
    result.sampleRateHz = output_.sampleRateHz() != 0
                              ? output_.sampleRateHz()
                              : mp3Info_.sampleRateHz;
    result.bitrateKbps = mp3Info_.bitrateKbps;
    result.variableBitrate = mp3Info_.variableBitrate;
    result.backpressureEvents = output_.backpressureEvents();
    result.serviceMaxUs = serviceMaxUs_;
    result.pcmFramesSinceReset = output_.pcmFramesSinceReset();
    result.pcmBuffersSinceReset = output_.pcmBuffersSinceReset();
    result.pcmSubmitGapMaxUs = output_.pcmSubmitGapMaxUs();
    result.pcmSubmitGapOver100Ms = output_.pcmSubmitGapOver100Ms();
    result.pcmLastSubmitAgeUs = output_.pcmLastSubmitAgeUs();
    result.openMaxUs = openMaxUs_;
    result.repeatRestartMaxUs = repeatRestartMaxUs_;
    result.repeatRestartCount = repeatRestartCount_;
    return result;
}

void Mp3PlaybackEngine::resetDiagnostics() {
    serviceMaxUs_ = 0;
    openMaxUs_ = 0;
    repeatRestartMaxUs_ = 0;
    repeatRestartCount_ = 0;
    output_.resetDiagnostics();
}

void Mp3PlaybackEngine::closePlaybackImmediate() {
    updateSourceOffset();
    if (decoder_.isRunning()) {
        decoder_.stop();
    } else {
        output_.stop();
        source_.close();
    }
    // A pause, seek, stop, track change or reopen is a deliberate producer
    // discontinuity. Keep cumulative diagnostics, but do not count that
    // transport interval as a continuous-playback PCM submission gap.
    output_.breakSubmitGapWindow();
    drainFlushed_ = false;
}

void Mp3PlaybackEngine::updateSourceOffset() {
    if (source_.isOpen()) {
        sourceByteOffset_ = source_.getPos();
    }
}

uint32_t Mp3PlaybackEngine::playbackPositionMs() const {
    if (state_ == AudioState::Stopped &&
        positionBaseMs_ == mp3Info_.durationMs) {
        return positionBaseMs_;
    }
    const uint32_t rate = output_.sampleRateHz();
    if (rate == 0) {
        return std::min(positionBaseMs_, mp3Info_.durationMs);
    }
    const uint64_t elapsed = output_.submittedFrames() * 1000ULL / rate;
    return static_cast<uint32_t>(std::min<uint64_t>(
        static_cast<uint64_t>(positionBaseMs_) + elapsed,
        mp3Info_.durationMs));
}

bool Mp3PlaybackEngine::fail(AudioError error) {
    const uint32_t failedAtMs = playbackPositionMs();
    closePlaybackImmediate();
    positionBaseMs_ = failedAtMs;
    state_ = AudioState::Error;
    error_ = error;
    queueEvent(AudioEventType::Error, error);
    return false;
}

void Mp3PlaybackEngine::queueEvent(AudioEventType type, AudioError error) {
    if (eventPending_) {
        return;
    }
    pendingEvent_.type = type;
    pendingEvent_.error = error;
    eventPending_ = true;
}

}  // namespace player
}  // namespace adv_walkman
