#pragma once

#include "player/audio/Mp3PlaybackEngine.h"
#include "player/core/PlayerController.h"
#include "player/storage/PersistedQueueSource.h"
#include "player/storage/PlayerStateStore.h"

namespace adv_walkman {
namespace player {

// Wires the formal Player to cooperative SD persistence. It deliberately does
// not scan the music library or implement product key bindings; those belong
// to P2/P4.
class PlayerRuntime final {
  public:
    bool begin(bool restoreSavedState = true);

    bool replaceQueue(TrackSource& source, size_t startIndex, bool autoplay);
    bool play();
    bool pause();
    bool resume();
    void stop();
    bool next();
    bool previous();
    bool seekToMs(uint32_t targetMs);
    void setRepeatMode(RepeatMode mode);
    void setShuffleEnabled(bool enabled);

    void service();
    PlayerSnapshot snapshot() const;
    PlayerController& controller();
    const PlayerController& controller() const;

    bool stateStoreAvailable() const;
    PersistenceResult lastPersistenceResult() const;
    uint32_t stateWriteCount() const;

    // Used by the P1 device runner to force a checkpoint before its controlled
    // software restart. The write is still cooperative and must be serviced.
    void requestCheckpoint(bool queueChanged = false);
    bool persistenceIdle() const;
    void setPersistenceSuspended(bool suspended);

  private:
    static constexpr uint32_t kCheckpointIntervalMs = 10000;

    bool restoreFromState();
    bool restoreSession(PersistedSession& session);
    bool findRestorableCursor(PersistedSession& session,
                              bool& keptSavedPosition) const;
    bool isPlayablePath(size_t sourceIndex) const;
    void captureSession(PersistedSession& output) const;
    void detectPlayerChanges();
    void startPendingSave();

    Mp3PlaybackEngine engine_;
    PlayerController controller_{engine_};
    PlayerStateStore stateStore_;
    PersistedQueueSource restoredQueue_;
    PersistedSession sessionScratch_{};

    TrackSource* activeSource_ = nullptr;
    bool stateStoreAvailable_ = false;
    bool queueDirty_ = false;
    bool sessionDirty_ = false;
    bool queueSaveWasPending_ = false;
    bool queuePublicationBlocked_ = false;
    bool persistenceSuspended_ = false;
    uint32_t nextPersistenceAttemptAtMs_ = 0;
    uint32_t activeQueueGeneration_ = 0;
    uint32_t lastCheckpointAtMs_ = 0;
    uint32_t stateWriteCount_ = 0;
    PersistenceResult lastPersistenceResult_ = PersistenceResult::NotFound;

    PlayerSnapshot previousSnapshot_{};
    bool havePreviousSnapshot_ = false;
};

}  // namespace player
}  // namespace adv_walkman
