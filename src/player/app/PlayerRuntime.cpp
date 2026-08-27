#include "player/app/PlayerRuntime.h"

#include <SD.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace adv_walkman {
namespace player {
namespace {

bool hasMp3Extension(const char* path) {
    if (path == nullptr) {
        return false;
    }
    const size_t length = std::strlen(path);
    if (length < 4) {
        return false;
    }
    const char* extension = path + length - 4;
    return extension[0] == '.' &&
           std::tolower(static_cast<unsigned char>(extension[1])) == 'm' &&
           std::tolower(static_cast<unsigned char>(extension[2])) == 'p' &&
           std::tolower(static_cast<unsigned char>(extension[3])) == '3';
}

}  // namespace

bool PlayerRuntime::begin(bool restoreSavedState) {
    if (!engine_.begin()) {
        return false;
    }

    stateStoreAvailable_ = stateStore_.begin(SD);
    lastPersistenceResult_ = stateStoreAvailable_ ? PersistenceResult::Ok
                                                  : PersistenceResult::IoError;
    lastCheckpointAtMs_ = millis();

    if (restoreSavedState && stateStoreAvailable_) {
        restoreFromState();
    }
    previousSnapshot_ = controller_.snapshot();
    havePreviousSnapshot_ = true;
    return true;
}

bool PlayerRuntime::replaceQueue(TrackSource& source, size_t startIndex,
                                 bool autoplay) {
    const size_t count = source.count();
    if (count > kMaxQueueTracks ||
        (count == 0 ? startIndex != 0 : startIndex >= count)) {
        return false;
    }
    const bool playbackReady =
        controller_.replaceQueue(source, startIndex, autoplay);
    // The Queue is accepted before an autoplay open can fail. Keep the
    // persisted source aligned with the Controller even in stable Error.
    activeSource_ = &source;
    activeQueueGeneration_ = 0;
    requestCheckpoint(true);
    return playbackReady;
}

bool PlayerRuntime::play() {
    const bool result = controller_.play();
    if (result) {
        requestCheckpoint();
    }
    return result;
}

bool PlayerRuntime::pause() {
    const bool result = controller_.pause();
    if (result) {
        requestCheckpoint();
    }
    return result;
}

bool PlayerRuntime::resume() {
    const bool result = controller_.resume();
    if (result) {
        requestCheckpoint();
    }
    return result;
}

void PlayerRuntime::stop() {
    controller_.stop();
    requestCheckpoint();
}

bool PlayerRuntime::next() {
    const bool result = controller_.next();
    requestCheckpoint();
    return result;
}

bool PlayerRuntime::previous() {
    const bool result = controller_.previous();
    requestCheckpoint();
    return result;
}

bool PlayerRuntime::seekToMs(uint32_t targetMs) {
    const bool result = controller_.seekToMs(targetMs);
    if (result) {
        requestCheckpoint();
    }
    return result;
}

void PlayerRuntime::setRepeatMode(RepeatMode mode) {
    controller_.setRepeatMode(mode);
    requestCheckpoint();
}

void PlayerRuntime::setShuffleEnabled(bool enabled) {
    controller_.setShuffleEnabled(enabled);
    requestCheckpoint();
}

void PlayerRuntime::setPreferredNowPlayingView(uint8_t view) {
    view = view == 1 ? 1 : 0;
    if (preferredNowPlayingView_ == view) return;
    preferredNowPlayingView_ = view;
    requestCheckpoint();  // No synchronous SD write or transport change.
}

void PlayerRuntime::service() {
    // Audio always receives service before a bounded storage step.
    controller_.service();
    detectPlayerChanges();

    const PlayerSnapshot current = controller_.snapshot();
    const uint32_t now = millis();
    if (current.state == PlayerState::Playing &&
        now - lastCheckpointAtMs_ >= kCheckpointIntervalMs) {
        sessionDirty_ = true;
        lastCheckpointAtMs_ = now;
    }

    if (stateStoreAvailable_ && stateStore_.pending()) {
        stateStore_.service();
        if (!stateStore_.pending()) {
            lastPersistenceResult_ = stateStore_.lastResult();
            if (lastPersistenceResult_ == PersistenceResult::Ok) {
                ++stateWriteCount_;
                if (queueSaveWasPending_) {
                    queueSaveWasPending_ = false;
                    activeQueueGeneration_ =
                        stateStore_.latestQueueGeneration();
                    sessionDirty_ = true;
                }
            } else {
                if (queueSaveWasPending_) {
                    queueSaveWasPending_ = false;
                    if (lastPersistenceResult_ ==
                        PersistenceResult::InvalidArgument) {
                        queuePublicationBlocked_ = true;
                    } else {
                        queueDirty_ = true;
                        nextPersistenceAttemptAtMs_ = millis() + 1000;
                    }
                } else {
                    sessionDirty_ = true;
                    nextPersistenceAttemptAtMs_ = millis() + 1000;
                }
            }
        }
    }
    if (stateStoreAvailable_ && !persistenceSuspended_ &&
        !stateStore_.pending()) {
        startPendingSave();
    }
}

PlayerSnapshot PlayerRuntime::snapshot() const {
    return controller_.snapshot();
}

bool PlayerRuntime::currentPath(char* output, size_t outputCapacity) const {
    return controller_.currentPath(output, outputCapacity);
}

void PlayerRuntime::resetDiagnostics() {
    controller_.resetDiagnostics();
}

PlayerController& PlayerRuntime::controller() {
    return controller_;
}

const PlayerController& PlayerRuntime::controller() const {
    return controller_;
}

bool PlayerRuntime::stateStoreAvailable() const {
    return stateStoreAvailable_;
}

PersistenceResult PlayerRuntime::lastPersistenceResult() const {
    return lastPersistenceResult_;
}

uint32_t PlayerRuntime::stateWriteCount() const {
    return stateWriteCount_;
}

void PlayerRuntime::requestCheckpoint(bool queueChanged) {
    if (persistenceSuspended_) {
        return;
    }
    sessionDirty_ = true;
    queueDirty_ = queueDirty_ || queueChanged;
    if (queueChanged) {
        queuePublicationBlocked_ = false;
    }
    nextPersistenceAttemptAtMs_ = 0;
    lastCheckpointAtMs_ = millis();
}

bool PlayerRuntime::persistenceIdle() const {
    if (stateStore_.pending()) {
        return false;
    }
    if (!stateStoreAvailable_ || persistenceSuspended_ ||
        queuePublicationBlocked_) {
        return true;
    }
    return !queueDirty_ && !sessionDirty_;
}

bool PlayerRuntime::queueSourceReleaseSafe() const {
    return !stateStore_.pending();
}

void PlayerRuntime::setPersistenceSuspended(bool suspended) {
    persistenceSuspended_ = suspended;
    if (suspended) {
        queueDirty_ = false;
        sessionDirty_ = false;
        queuePublicationBlocked_ = false;
    }
}

bool PlayerRuntime::restoreFromState() {
    lastPersistenceResult_ =
        stateStore_.loadPairedState(restoredQueue_, sessionScratch_);
    if (lastPersistenceResult_ != PersistenceResult::Ok) {
        return false;
    }
    activeSource_ = &restoredQueue_;
    activeQueueGeneration_ = restoredQueue_.generation();
    return restoreSession(sessionScratch_);
}

bool PlayerRuntime::restoreSession(PersistedSession& session) {
    preferredNowPlayingView_ = session.preferredNowPlayingView == 1 ? 1 : 0;
    const bool savedShuffleEnabled = session.shuffleEnabled;
    const uint16_t savedCurrentIndex = session.currentIndex;
    const uint32_t savedPositionMs = session.positionMs;
    const uint32_t savedSourceOffset = session.sourceOffset;
    bool keptSavedPosition = true;
    const RepeatMode repeat = static_cast<RepeatMode>(session.repeatMode);
    PlaybackQueueSnapshotView view;
    if (findRestorableCursor(session, keptSavedPosition)) {
        view.order = session.order;
        view.orderCount = session.orderCount;
        view.orderCursor = session.orderCursor;
        view.shuffleEnabled = session.shuffleEnabled;
        view.history = session.history;
        view.historyCount = session.historyCount;
        if (controller_.restorePaused(
                restoredQueue_, view, repeat,
                keptSavedPosition ? session.positionMs : 0,
                keptSavedPosition ? session.sourceOffset : 0)) {
            return true;
        }
    }

    // Semantically invalid shuffle/history data must not discard an otherwise
    // valid Queue/current track. Rebuild a sequential order around current.
    session.currentIndex = savedCurrentIndex;
    session.positionMs = savedPositionMs;
    session.sourceOffset = savedSourceOffset;
    keptSavedPosition = true;
    const size_t count = restoredQueue_.count();
    if (count == 0) {
        return false;
    }
    const size_t savedIndex = savedCurrentIndex < count
                                  ? savedCurrentIndex
                                  : 0;
    size_t fallbackIndex = count;
    for (size_t distance = 0; distance < count; ++distance) {
        const size_t candidate = (savedIndex + distance) % count;
        if (isPlayablePath(candidate)) {
            fallbackIndex = candidate;
            keptSavedPosition = candidate == savedIndex &&
                                savedCurrentIndex < count;
            break;
        }
    }
    if (fallbackIndex == count) {
        return false;
    }
    session.currentIndex = static_cast<uint16_t>(fallbackIndex);
    session.orderCount = static_cast<uint16_t>(count);
    session.historyCount = 0;
    session.shuffleEnabled = false;
    if (savedShuffleEnabled) {
        session.orderCursor = 0;
        session.order[0] = static_cast<uint16_t>(fallbackIndex);
        size_t outputIndex = 1;
        for (size_t index = 0; index < count; ++index) {
            if (index != fallbackIndex) {
                session.order[outputIndex++] = static_cast<uint16_t>(index);
            }
        }
    } else {
        session.orderCursor = static_cast<uint16_t>(fallbackIndex);
        for (size_t index = 0; index < count; ++index) {
            session.order[index] = static_cast<uint16_t>(index);
        }
    }
    view.order = session.order;
    view.orderCount = session.orderCount;
    view.orderCursor = session.orderCursor;
    view.shuffleEnabled = false;
    view.history = nullptr;
    view.historyCount = 0;
    if (!controller_.restorePaused(restoredQueue_, view, repeat,
                                   keptSavedPosition ? session.positionMs : 0,
                                   keptSavedPosition ? session.sourceOffset : 0)) {
        return false;
    }
    if (savedShuffleEnabled) {
        controller_.setShuffleEnabled(true);
    }
    return true;
}

bool PlayerRuntime::findRestorableCursor(PersistedSession& session,
                                         bool& keptSavedPosition) const {
    const size_t count = restoredQueue_.count();
    if (count == 0 || session.orderCount != count ||
        session.orderCursor >= session.orderCount) {
        return false;
    }

    for (size_t distance = 0; distance < count; ++distance) {
        const size_t cursor = (session.orderCursor + distance) % count;
        const size_t sourceIndex = session.order[cursor];
        if (sourceIndex < count && isPlayablePath(sourceIndex)) {
            session.orderCursor = static_cast<uint16_t>(cursor);
            session.currentIndex = static_cast<uint16_t>(sourceIndex);
            keptSavedPosition = distance == 0;
            return true;
        }
    }
    return false;
}

bool PlayerRuntime::isPlayablePath(size_t sourceIndex) const {
    char path[kTrackPathCapacity];
    return restoredQueue_.pathAt(sourceIndex, path, sizeof(path)) &&
           hasMp3Extension(path) && SD.exists(path);
}

void PlayerRuntime::captureSession(PersistedSession& output) const {
    std::memset(&output, 0, sizeof(output));
    output.currentIndex = kPersistedInvalidTrackIndex;
    const PlayerSnapshot player = controller_.snapshot();
    const PlaybackQueueSnapshotView queue = controller_.queueSnapshotView();

    output.queueGeneration = activeQueueGeneration_;
    output.currentIndex = player.hasCurrent
                              ? static_cast<uint16_t>(player.currentIndex)
                              : kPersistedInvalidTrackIndex;
    output.positionMs = player.positionMs;
    output.sourceOffset = player.sourceByteOffset;
    output.repeatMode = static_cast<uint8_t>(player.repeatMode);
    output.shuffleEnabled = queue.shuffleEnabled;
    output.preferredNowPlayingView = preferredNowPlayingView_;
    output.orderCount = static_cast<uint16_t>(queue.orderCount);
    output.orderCursor = static_cast<uint16_t>(queue.orderCursor);
    output.historyCount = static_cast<uint8_t>(
        std::min(queue.historyCount, kPersistedHistoryMaxTracks));
    for (size_t index = 0; index < queue.orderCount; ++index) {
        output.order[index] = queue.order[index];
    }
    for (size_t index = 0; index < output.historyCount; ++index) {
        output.history[index] = queue.history[index];
    }
}

void PlayerRuntime::detectPlayerChanges() {
    if (persistenceSuspended_) {
        previousSnapshot_ = controller_.snapshot();
        havePreviousSnapshot_ = true;
        return;
    }
    const PlayerSnapshot current = controller_.snapshot();
    if (havePreviousSnapshot_ &&
        (current.state != previousSnapshot_.state ||
         current.error != previousSnapshot_.error ||
         current.currentIndex != previousSnapshot_.currentIndex ||
         current.repeatMode != previousSnapshot_.repeatMode ||
         current.shuffleEnabled != previousSnapshot_.shuffleEnabled)) {
        sessionDirty_ = true;
    }
    previousSnapshot_ = current;
    havePreviousSnapshot_ = true;
}

void PlayerRuntime::startPendingSave() {
    if (queuePublicationBlocked_ ||
        (nextPersistenceAttemptAtMs_ != 0 &&
         static_cast<int32_t>(millis() - nextPersistenceAttemptAtMs_) < 0)) {
        return;
    }
    nextPersistenceAttemptAtMs_ = 0;
    if (queueDirty_) {
        if (activeSource_ == nullptr) {
            queueDirty_ = false;
            lastPersistenceResult_ = PersistenceResult::InvalidArgument;
            return;
        }
        const PersistenceResult result =
            stateStore_.saveQueueAsync(*activeSource_);
        lastPersistenceResult_ = result;
        if (result == PersistenceResult::Pending) {
            queueDirty_ = false;
            queueSaveWasPending_ = true;
        }
        return;
    }

    if (sessionDirty_) {
        // A Session cannot reference an unpublished Queue generation.
        if (activeQueueGeneration_ == 0) {
            return;
        }
        captureSession(sessionScratch_);
        const PersistenceResult result =
            stateStore_.saveSessionAsync(sessionScratch_);
        lastPersistenceResult_ = result;
        if (result == PersistenceResult::Pending) {
            sessionDirty_ = false;
        }
    }
}

}  // namespace player
}  // namespace adv_walkman
