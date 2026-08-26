#pragma once

#include <cstddef>
#include <cstdint>

#include "../audio/Mp3PlaybackEngine.h"
#include "CoreTypes.h"
#include "PlaybackQueue.h"
#include "TrackSource.h"

namespace adv_walkman {
namespace player {

struct PlayerSnapshot {
    PlayerState state = PlayerState::Empty;
    PlayerError error = PlayerError::None;
    AudioError audioError = AudioError::None;
    RepeatMode repeatMode = RepeatMode::Off;
    bool shuffleEnabled = false;
    bool hasCurrent = false;
    size_t queueCount = 0;
    size_t currentIndex = 0;
    size_t orderCursor = 0;
    uint32_t positionMs = 0;
    uint32_t durationMs = 0;
    uint32_t sourceByteOffset = 0;
    uint32_t sampleRateHz = 0;
    uint16_t bitrateKbps = 0;
    bool variableBitrate = false;
    uint32_t backpressureEvents = 0;
    uint32_t serviceMaxUs = 0;
    uint64_t pcmFramesSinceReset = 0;
    uint32_t pcmBuffersSinceReset = 0;
    uint32_t pcmSubmitGapMaxUs = 0;
    uint32_t pcmSubmitGapOver100Ms = 0;
    uint32_t pcmLastSubmitAgeUs = UINT32_MAX;
    uint32_t openMaxUs = 0;
    uint32_t repeatRestartMaxUs = 0;
    uint32_t repeatRestartCount = 0;
    uint32_t trackEndedEvents = 0;
    uint32_t audioErrorEvents = 0;
};

class PlayerController {
  public:
    explicit PlayerController(Mp3PlaybackEngine& engine,
                              uint32_t shuffleSeed = 0x9E3779B9u);

    bool replaceQueue(TrackSource& source, size_t startIndex, bool autoplay);
    bool restorePaused(TrackSource& source,
                       const PlaybackQueueSnapshotView& queueSnapshot,
                       RepeatMode repeatMode,
                       uint32_t positionMs,
                       uint32_t sourceByteOffset = 0);

    bool play();
    bool pause();
    bool resume();
    bool togglePlayPause();
    void stop();
    bool next();
    bool previous();
    bool seekToMs(uint32_t targetMs);

    void setRepeatMode(RepeatMode mode);
    RepeatMode repeatMode() const;
    void setShuffleEnabled(bool enabled);
    bool shuffleEnabled() const;
    void setShuffleSeed(uint32_t seed);

    void service();
    PlayerSnapshot snapshot() const;
    PlaybackQueueSnapshotView queueSnapshotView() const;
    const PlaybackQueue& queue() const;
    bool currentPath(char* output, size_t outputCapacity) const;
    void resetDiagnostics();

  private:
    bool openCurrent(uint32_t positionMs,
                     bool startPaused,
                     uint32_t sourceByteOffsetHint = 0);
    bool selectAdvancedTrack();
    bool handleTrackEnded();
    bool refreshCurrentPath();
    bool fail(PlayerError error, AudioError audioError = AudioError::None);
    void clearError();
    uint32_t currentPositionMs() const;
    bool validRepeatMode(RepeatMode mode) const;

    Mp3PlaybackEngine& engine_;
    PlaybackQueue queue_;
    PlayerState state_ = PlayerState::Empty;
    PlayerError error_ = PlayerError::None;
    AudioError audioError_ = AudioError::None;
    RepeatMode repeatMode_ = RepeatMode::Off;
    bool engineLoaded_ = false;
    uint32_t deferredPositionMs_ = 0;
    uint32_t deferredSourceByteOffset_ = 0;
    char currentPath_[kTrackPathCapacity]{};
    uint32_t trackEndedEvents_ = 0;
    uint32_t audioErrorEvents_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
