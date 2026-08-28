#pragma once

#include "player/audio/Mp3PlaybackEngine.h"
#include "player/core/PlayerController.h"
#include "player/storage/PersistedQueueSource.h"
#include "player/storage/PlayerStateStore.h"
#include "VolumePolicy.h"

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
    void setVolume(uint8_t volume) {volumeLevel_=volume;engine_.setVolume(VolumePolicy::toRaw(volume));}
    uint8_t volume() const {return volumeLevel_;}
    uint8_t rawSpeakerVolume() const {return engine_.volume();}
    void stop();
    bool next();
    bool previous();
    bool seekToMs(uint32_t targetMs);
    void setRepeatMode(RepeatMode mode);
    void setShuffleEnabled(bool enabled);
    uint8_t preferredNowPlayingView() const { return preferredNowPlayingView_; }
    void setPreferredNowPlayingView(uint8_t view);

    void service();
    void serviceAudio();
    void servicePersistence();
    uint32_t persistencePhasePeakUs(uint8_t phase)const{return stateStore_.phasePeakUs(phase);}
    uint32_t sourceReadMaxUs()const{return engine_.sourceReadMaxUs();}
    PlayerSnapshot snapshot() const;
    bool currentPath(char* output, size_t outputCapacity) const;
    void resetDiagnostics();
    PlayerController& controller();
    const PlayerController& controller() const;

    bool stateStoreAvailable() const;
    PersistenceResult lastPersistenceResult() const;
    uint32_t stateWriteCount() const;

    // Used by the P1 device runner to force a checkpoint before its controlled
    // software restart. The write is still cooperative and must be serviced.
    void requestCheckpoint(bool queueChanged = false);
    bool persistenceIdle() const;
    // A FolderQueueSource may be released once no asynchronous state-store
    // job can still read it. Dirty/blocked state alone does not retain the
    // source and must not permanently block selecting a replacement Queue.
    bool queueSourceReleaseSafe() const;
    uint32_t transportServiceMaxUs()const{return transportServiceMaxUs_;}
    uint32_t persistenceServiceMaxUs()const{return persistenceServiceMaxUs_;}
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
    uint8_t volumeLevel_ = VolumePolicy::initialLevel;
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
    uint8_t preferredNowPlayingView_ = 0;
    uint32_t nextPersistenceAttemptAtMs_ = 0;
    uint32_t activeQueueGeneration_ = 0;
    uint32_t lastCheckpointAtMs_ = 0;
    uint32_t stateWriteCount_ = 0;
    PersistenceResult lastPersistenceResult_ = PersistenceResult::NotFound;

    PlayerSnapshot previousSnapshot_{};
    bool havePreviousSnapshot_ = false;
    uint32_t transportServiceMaxUs_=0,persistenceServiceMaxUs_=0;
};

}  // namespace player
}  // namespace adv_walkman
