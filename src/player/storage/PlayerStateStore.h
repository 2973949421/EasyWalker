#pragma once

#include <FS.h>

#include "../core/TrackSource.h"
#include "PersistedQueueSource.h"
#include "PersistenceTypes.h"

namespace adv_walkman {
namespace player {

class PlayerStateStore {
  public:
    static const char* const kStateDirectory;
    static const char* const kQueueSlotA;
    static const char* const kQueueSlotB;
    static const char* const kSessionSlotA;
    static const char* const kSessionSlotB;

    bool begin(fs::FS& fs);

    PersistenceResult loadQueue(PersistedQueueSource& output);
    PersistenceResult loadSession(PersistedSession& output);
    PersistenceResult loadPairedState(PersistedQueueSource& queueOutput,
                                      PersistedSession& sessionOutput);

    // The TrackSource must remain alive and unchanged until pending() becomes
    // false. Preparation, writing and readback verification are cooperative.
    PersistenceResult saveQueueAsync(const TrackSource& source);
    PersistenceResult saveSessionAsync(const PersistedSession& session);
    void service();

    bool pending() const;
    PersistenceResult lastResult() const;
    PersistenceRecordKind lastCompletedKind() const;
    uint32_t latestQueueGeneration() const;
    uint32_t latestSessionGeneration() const;

  private:
    struct SlotInfo {
        bool valid = false;
        uint32_t generation = 0;
        uint32_t payloadLength = 0;
        uint32_t payloadCrc = 0;
        const char* path = nullptr;
        char slot = 0;
    };

    enum class JobPhase : uint8_t {
        Idle,
        QueuePrepare,
        OpenTarget,
        WriteHeader,
        WritePayload,
        CloseTarget,
        OpenVerify,
        VerifyPayload,
    };

    bool ensureStateDirectory();
    void refreshQueueSlots();
    void refreshSessionSlots();
    bool inspectRecord(
        const char* path,
        uint32_t expectedMagic,
        uint32_t maximumPayload,
        SlotInfo& output) const;
    bool validateQueuePayload(const SlotInfo& slot) const;
    bool validateSessionPayload(const SlotInfo& slot, PersistedSession* output);

    void serviceQueuePrepare();
    void serviceOpenTarget();
    void serviceWriteHeader();
    void serviceWritePayload();
    void serviceCloseTarget();
    void serviceOpenVerify();
    void serviceVerifyPayload();
    void complete(PersistenceResult result);

    fs::FS* fs_ = nullptr;
    SlotInfo currentQueueSlot_;
    SlotInfo currentSessionSlot_;

    PersistenceRecordKind jobKind_ = PersistenceRecordKind::None;
    PersistenceRecordKind completedKind_ = PersistenceRecordKind::None;
    PersistenceResult lastResult_ = PersistenceResult::NotFound;
    JobPhase phase_ = JobPhase::Idle;

    const TrackSource* queueSource_ = nullptr;
    uint16_t queueCount_ = 0;
    uint16_t queuePrepareIndex_ = 0;
    uint16_t queueWriteIndex_ = 0;
    bool queueCountWritten_ = false;

    uint32_t sessionPayloadLength_ = 0;
    uint32_t payloadWriteOffset_ = 0;

    uint32_t preparedPayloadLength_ = 0;
    uint32_t preparedPayloadCrcState_ = 0xFFFFFFFF;
    uint32_t preparedPayloadCrc_ = 0;
    uint32_t jobGeneration_ = 0;
    const char* targetPath_ = nullptr;
    char targetSlot_ = 0;
    fs::File jobFile_;

    uint32_t verifyRemaining_ = 0;
    uint32_t verifyCrcState_ = 0xFFFFFFFF;
    uint8_t ioBuffer_[4096] = {};
    PersistedSession sessionDecodeScratch_;
};

}  // namespace player
}  // namespace adv_walkman
