#pragma once

#include <FS.h>

#include <stddef.h>
#include <stdint.h>

#include "../core/CoreTypes.h"

namespace adv_walkman {
namespace player {

enum class RecentTracksResult : uint8_t {
    Ok,
    Pending,
    NotFound,
    NotLoaded,
    Busy,
    InvalidArgument,
    UnsupportedVersion,
    Corrupt,
    IoError,
};

const char* recentTracksResultName(RecentTracksResult result);

// A small MRU list kept separately from the Player queue/session state.
//
// record() only updates RAM and schedules an A/B-slot publication. The owner
// must call service() only while PlayerStateStore is idle, so Recent writes do
// not compete with queue/session checkpoints for the SD card.
class RecentTracksStore {
  public:
    static const char* const kRecentSlotA;
    static const char* const kRecentSlotB;
    static constexpr size_t kMaximumTracks = 32;

    bool begin(
        fs::FS& fs,
        const char* slotA = kRecentSlotA,
        const char* slotB = kRecentSlotB);
    RecentTracksResult load();
    // Re-arms a failed or deferred publication without changing MRU order.
    RecentTracksResult retrySave();
    // The caller owns the MP3-only policy. This Store only accepts an
    // existing, non-directory file at a canonical absolute path.
    RecentTracksResult record(const char* path);
    void service();

    bool pending() const;
    bool loaded() const;
    bool dirty() const;
    RecentTracksResult lastResult() const;
    uint32_t generation() const;

    size_t count() const;
    bool pathAt(size_t index, char* output, size_t outputCapacity) const;

  private:
    static constexpr size_t kRecordHeaderSize = 20;
    static constexpr size_t kStorageStepBytes = 1024;
    static constexpr size_t kMaximumEntryBytes =
        kMaximumTracks * (sizeof(uint16_t) + kMaxTrackPathBytes);
    static constexpr size_t kMaximumPayloadBytes =
        sizeof(uint16_t) + kMaximumEntryBytes;

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
        PrepareCrc,
        OpenTarget,
        WriteHeader,
        WritePayload,
        CloseTarget,
        OpenVerify,
        VerifyPayload,
    };

    bool ensureStorageDirectories();
    bool ensureParentDirectory(const char* filePath);
    bool inspectRecord(
        const char* path,
        SlotInfo& output,
        bool& unsupportedVersion);
    bool validatePayload(const SlotInfo& slot);
    bool loadPayload(const SlotInfo& slot, bool& removedMissingOrDuplicate);

    bool findPath(
        const char* path,
        size_t pathLength,
        size_t& recordOffset,
        size_t& recordLength,
        size_t& recordIndex) const;
    bool locateRecord(
        size_t index,
        size_t& recordOffset,
        uint16_t& pathLength) const;
    void startSave();
    size_t copyPayloadBytes(
        uint32_t offset,
        uint8_t* output,
        size_t length) const;

    void servicePrepareCrc();
    void serviceOpenTarget();
    void serviceWriteHeader();
    void serviceWritePayload();
    void serviceCloseTarget();
    void serviceOpenVerify();
    void serviceVerifyPayload();
    void complete(RecentTracksResult result);

    fs::FS* fs_ = nullptr;
    SlotInfo currentSlot_;
    char slotPathA_[kTrackPathCapacity] = {};
    char slotPathB_[kTrackPathCapacity] = {};

    uint8_t entries_[kMaximumEntryBytes] = {};
    uint16_t entryBytes_ = 0;
    uint8_t count_ = 0;
    bool loaded_ = false;
    bool dirty_ = false;

    JobPhase phase_ = JobPhase::Idle;
    RecentTracksResult lastResult_ = RecentTracksResult::NotFound;
    uint32_t jobGeneration_ = 0;
    uint32_t preparedPayloadLength_ = 0;
    uint32_t preparedPayloadCrc_ = 0;
    uint32_t prepareOffset_ = 0;
    uint32_t prepareCrcState_ = 0xFFFFFFFF;
    uint32_t writeOffset_ = 0;
    uint32_t verifyRemaining_ = 0;
    uint32_t verifyCrcState_ = 0xFFFFFFFF;
    const char* targetPath_ = nullptr;
    char targetSlot_ = 0;
    fs::File jobFile_;
    uint8_t ioBuffer_[kStorageStepBytes] = {};
};

}  // namespace player
}  // namespace adv_walkman
