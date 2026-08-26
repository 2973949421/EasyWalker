#include "PlayerController.h"

#include <cstring>

namespace adv_walkman {
namespace player {

PlayerController::PlayerController(Mp3PlaybackEngine& engine, uint32_t shuffleSeed)
    : engine_(engine), queue_(shuffleSeed) {}

bool PlayerController::replaceQueue(TrackSource& source, size_t startIndex, bool autoplay) {
    const size_t sourceCount = source.count();
    if (sourceCount > kMaxQueueTracks) {
        return false;
    }
    if ((sourceCount == 0 && startIndex != 0) ||
        (sourceCount != 0 && startIndex >= sourceCount)) {
        return false;
    }

    const bool preserveShuffle = queue_.shuffleEnabled();
    engine_.stop();
    engineLoaded_ = false;
    deferredPositionMs_ = 0;
    deferredSourceByteOffset_ = 0;

    if (!queue_.reset(source, startIndex)) {
        return fail(sourceCount > kMaxQueueTracks ? PlayerError::QueueTooLarge
                                                  : PlayerError::InvalidArgument);
    }
    queue_.setShuffleEnabled(preserveShuffle, preserveShuffle);
    if (!refreshCurrentPath()) {
        return fail(PlayerError::TrackPathUnavailable);
    }
    clearError();

    if (queue_.empty()) {
        state_ = PlayerState::Empty;
        return true;
    }

    state_ = PlayerState::Stopped;
    return !autoplay || openCurrent(0, false);
}

bool PlayerController::restorePaused(TrackSource& source,
                                     const PlaybackQueueSnapshotView& queueSnapshot,
                                     RepeatMode repeatMode,
                                     uint32_t positionMs,
                                     uint32_t sourceByteOffset) {
    if (!validRepeatMode(repeatMode)) {
        return false;
    }

    if (!queue_.restore(source, queueSnapshot)) {
        return false;
    }
    if (!refreshCurrentPath()) {
        return false;
    }

    engine_.stop();
    engineLoaded_ = false;
    repeatMode_ = repeatMode;
    deferredPositionMs_ = positionMs;
    deferredSourceByteOffset_ = sourceByteOffset;
    clearError();
    state_ = queue_.empty() ? PlayerState::Empty : PlayerState::Paused;
    return true;
}

bool PlayerController::play() {
    switch (state_) {
        case PlayerState::Playing:
            return true;
        case PlayerState::Paused:
            return resume();
        case PlayerState::Stopped:
            return openCurrent(0, false);
        case PlayerState::Error:
            return openCurrent(0, false);
        case PlayerState::Empty:
            return false;
    }
    return false;
}

bool PlayerController::pause() {
    if (state_ == PlayerState::Paused) {
        return true;
    }
    if (state_ != PlayerState::Playing || !engineLoaded_) {
        return false;
    }
    if (engine_.status().state == AudioState::Draining) {
        // EOF tail audio is already committed. A transport command racing
        // this short window is simply unavailable; it must not turn a clean
        // natural ending into Error or cut the drain short.
        return false;
    }
    if (!engine_.pause()) {
        return fail(PlayerError::AudioOperationFailed, engine_.status().error);
    }
    deferredPositionMs_ = engine_.status().positionMs;
    deferredSourceByteOffset_ = engine_.status().sourceByteOffset;
    state_ = PlayerState::Paused;
    return true;
}

bool PlayerController::resume() {
    if (state_ == PlayerState::Playing) {
        return true;
    }
    if (state_ != PlayerState::Paused) {
        return false;
    }

    if (!engineLoaded_) {
        return openCurrent(deferredPositionMs_, false, deferredSourceByteOffset_);
    }
    if (!engine_.resume()) {
        return fail(PlayerError::AudioOperationFailed, engine_.status().error);
    }
    state_ = PlayerState::Playing;
    return true;
}

bool PlayerController::togglePlayPause() {
    return state_ == PlayerState::Playing ? pause() : play();
}

void PlayerController::stop() {
    engine_.stop();
    engineLoaded_ = false;
    deferredPositionMs_ = 0;
    deferredSourceByteOffset_ = 0;
    clearError();
    state_ = queue_.empty() ? PlayerState::Empty : PlayerState::Stopped;
}

bool PlayerController::next() {
    if (queue_.empty()) {
        return false;
    }

    const bool wrapAtEnd = repeatMode_ == RepeatMode::All;
    if (!queue_.advance(wrapAtEnd)) {
        return false;
    }
    return selectAdvancedTrack();
}

bool PlayerController::previous() {
    if (queue_.empty()) {
        return false;
    }

    if ((state_ == PlayerState::Playing || state_ == PlayerState::Paused) &&
        currentPositionMs() > kPreviousRestartThresholdMs) {
        if (state_ == PlayerState::Paused && !engineLoaded_) {
            deferredPositionMs_ = 0;
            deferredSourceByteOffset_ = 0;
            return true;
        }
        return seekToMs(0);
    }

    if (!queue_.previous()) {
        return false;
    }
    return selectAdvancedTrack();
}

bool PlayerController::seekToMs(uint32_t targetMs) {
    if (state_ != PlayerState::Playing && state_ != PlayerState::Paused) {
        return false;
    }

    const bool remainPaused = state_ == PlayerState::Paused;
    if (!engineLoaded_) {
        return openCurrent(targetMs, true);
    }
    if (engine_.status().state == AudioState::Draining) {
        return false;
    }
    if (!engine_.seekToMs(targetMs)) {
        return fail(PlayerError::AudioOperationFailed, engine_.status().error);
    }

    const AudioStatus status = engine_.status();
    deferredPositionMs_ = status.positionMs;
    deferredSourceByteOffset_ = status.sourceByteOffset;
    state_ = remainPaused ? PlayerState::Paused : PlayerState::Playing;
    return true;
}

void PlayerController::setRepeatMode(RepeatMode mode) {
    if (validRepeatMode(mode)) {
        repeatMode_ = mode;
    }
}

RepeatMode PlayerController::repeatMode() const {
    return repeatMode_;
}

void PlayerController::setShuffleEnabled(bool enabled) {
    queue_.setShuffleEnabled(enabled);
}

bool PlayerController::shuffleEnabled() const {
    return queue_.shuffleEnabled();
}

void PlayerController::setShuffleSeed(uint32_t seed) {
    queue_.setRandomSeed(seed);
}

void PlayerController::service() {
    if (!engineLoaded_) {
        return;
    }

    engine_.service();

    AudioEvent event;
    while (engine_.pollEvent(event)) {
        if (event.type == AudioEventType::Error) {
            ++audioErrorEvents_;
            fail(PlayerError::AudioEngineError, event.error);
            return;
        }
        if (event.type == AudioEventType::TrackEnded) {
            ++trackEndedEvents_;
            if (!handleTrackEnded()) {
                return;
            }
        }
    }

    const AudioStatus status = engine_.status();
    if (status.state == AudioState::Error) {
        fail(PlayerError::AudioEngineError, status.error);
    }
}

PlayerSnapshot PlayerController::snapshot() const {
    PlayerSnapshot result;
    const AudioStatus audio = engine_.status();

    result.state = state_;
    result.error = error_;
    result.audioError = audioError_;
    result.repeatMode = repeatMode_;
    result.shuffleEnabled = queue_.shuffleEnabled();
    result.hasCurrent = queue_.hasCurrent();
    result.queueCount = queue_.count();
    result.currentIndex = queue_.hasCurrent() ? queue_.currentSourceIndex() : 0;
    result.orderCursor = queue_.orderCursor();
    result.positionMs = engineLoaded_ ? audio.positionMs : deferredPositionMs_;
    result.durationMs = engineLoaded_ ? audio.durationMs : 0;
    result.sourceByteOffset = engineLoaded_ ? audio.sourceByteOffset : deferredSourceByteOffset_;
    result.sampleRateHz = engineLoaded_ ? audio.sampleRateHz : 0;
    result.bitrateKbps = engineLoaded_ ? audio.bitrateKbps : 0;
    result.variableBitrate = engineLoaded_ && audio.variableBitrate;
    result.backpressureEvents = audio.backpressureEvents;
    result.serviceMaxUs = audio.serviceMaxUs;
    result.trackEndedEvents = trackEndedEvents_;
    result.audioErrorEvents = audioErrorEvents_;
    return result;
}

PlaybackQueueSnapshotView PlayerController::queueSnapshotView() const {
    return queue_.snapshotView();
}

const PlaybackQueue& PlayerController::queue() const {
    return queue_;
}

bool PlayerController::currentPath(char* output, size_t outputCapacity) const {
    if (output == nullptr || outputCapacity == 0 || !queue_.hasCurrent() ||
        currentPath_[0] == '\0') {
        return false;
    }
    const size_t length = std::strlen(currentPath_);
    if (length + 1 > outputCapacity) {
        return false;
    }
    std::memcpy(output, currentPath_, length + 1);
    return true;
}

void PlayerController::resetDiagnostics() {
    trackEndedEvents_ = 0;
    audioErrorEvents_ = 0;
}

bool PlayerController::openCurrent(uint32_t positionMs,
                                   bool startPaused,
                                   uint32_t sourceByteOffsetHint) {
    engine_.stop();
    engineLoaded_ = false;
    if (!queue_.currentPath(currentPath_, sizeof(currentPath_))) {
        return fail(PlayerError::TrackPathUnavailable);
    }

    if (!engine_.open(currentPath_, positionMs, startPaused, sourceByteOffsetHint)) {
        // Synchronous probe/file/decoder-start failures are consumed by
        // fail()->stop(), so count them here instead of pretending that only
        // asynchronous AudioEvent errors occurred.
        ++audioErrorEvents_;
        return fail(PlayerError::AudioOpenFailed, engine_.status().error);
    }

    engineLoaded_ = true;
    deferredPositionMs_ = positionMs;
    deferredSourceByteOffset_ = engine_.status().sourceByteOffset;
    clearError();
    state_ = startPaused ? PlayerState::Paused : PlayerState::Playing;
    return true;
}

bool PlayerController::selectAdvancedTrack() {
    deferredPositionMs_ = 0;
    deferredSourceByteOffset_ = 0;

    if (state_ == PlayerState::Stopped) {
        engine_.stop();
        engineLoaded_ = false;
        if (!refreshCurrentPath()) {
            return fail(PlayerError::TrackPathUnavailable);
        }
        clearError();
        return true;
    }

    const bool startPaused = state_ == PlayerState::Paused;
    return openCurrent(0, startPaused);
}

bool PlayerController::refreshCurrentPath() {
    if (!queue_.hasCurrent()) {
        currentPath_[0] = '\0';
        return true;
    }
    if (!queue_.currentPath(currentPath_, sizeof(currentPath_))) {
        currentPath_[0] = '\0';
        return false;
    }
    return true;
}

bool PlayerController::handleTrackEnded() {
    engineLoaded_ = false;
    deferredPositionMs_ = 0;
    deferredSourceByteOffset_ = 0;

    if (repeatMode_ == RepeatMode::One) {
        return openCurrent(0, false);
    }

    const bool wrapAtEnd = repeatMode_ == RepeatMode::All;
    if (!queue_.advance(wrapAtEnd)) {
        engine_.stop();
        state_ = queue_.empty() ? PlayerState::Empty : PlayerState::Stopped;
        clearError();
        return true;
    }
    return openCurrent(0, false);
}

bool PlayerController::fail(PlayerError error, AudioError audioError) {
    error_ = error;
    audioError_ = audioError;
    const AudioStatus audio = engine_.status();
    deferredPositionMs_ = audio.positionMs;
    deferredSourceByteOffset_ = audio.sourceByteOffset;
    engine_.stop();
    engineLoaded_ = false;
    state_ = PlayerState::Error;
    return false;
}

void PlayerController::clearError() {
    error_ = PlayerError::None;
    audioError_ = AudioError::None;
}

uint32_t PlayerController::currentPositionMs() const {
    return engineLoaded_ ? engine_.status().positionMs : deferredPositionMs_;
}

bool PlayerController::validRepeatMode(RepeatMode mode) const {
    return mode == RepeatMode::Off || mode == RepeatMode::All || mode == RepeatMode::One;
}

}  // namespace player
}  // namespace adv_walkman
