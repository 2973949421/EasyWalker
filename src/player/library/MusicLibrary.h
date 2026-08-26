#pragma once

#include <FS.h>

#include <cstddef>
#include <cstdint>

#include "player/core/CoreTypes.h"

namespace adv_walkman {
namespace player {

class FolderQueueSource;

enum class LibraryState : uint8_t {
    Idle,
    Open,
    Scan,
    Sort,
    Finalize,
    Ready,
    Error,
};

enum class LibraryError : uint8_t {
    None,
    InvalidArgument,
    RootUnavailable,
    NotReady,
    InvalidEntry,
    NotDirectory,
    PathTooLong,
    DirectoryTooLarge,
    QueueTooLarge,
    CacheBusy,
    IoError,
    CacheCorrupt,
};

enum class LibraryEntryType : uint8_t {
    Directory = 0,
    Track = 1,
};

enum class LibraryResult : uint8_t {
    Ok,
    Pending,
    Error,
};

struct LibraryEntry {
    LibraryEntryType type = LibraryEntryType::Track;
    size_t index = 0;
    char name[kTrackPathCapacity] = {};
};

struct LibraryStats {
    uint32_t cacheHits = 0;
    uint32_t cacheMisses = 0;
    uint32_t cacheEvictions = 0;
    uint32_t pageHits = 0;
    uint32_t pageMisses = 0;
    uint32_t filteredEntries = 0;
    uint32_t serviceMaxUs = 0;
    uint32_t overBudgetSteps = 0;
    // Includes the synchronous page lookup plus cache-record SD read performed
    // by entryAt(). Cooperative serviceMaxUs intentionally excludes it.
    uint32_t entryReadMaxUs = 0;
};

// Cooperative, directory-at-a-time view over /Music. It never recursively
// scans the library. Cache files are session-only and are rebuilt after boot.
class MusicLibrary final {
  public:
    static constexpr const char* kMusicRoot = "/Music";
    static constexpr size_t kMaxDirectoryEntries = 2048;
    static constexpr size_t kCacheSlotCount = 4;
    static constexpr size_t kPageCount = 3;
    static constexpr size_t kPageEntries = 32;
    static constexpr uint32_t kServiceBudgetUs = 750;

    MusicLibrary() = default;
    ~MusicLibrary();

    MusicLibrary(const MusicLibrary&) = delete;
    MusicLibrary& operator=(const MusicLibrary&) = delete;

    bool begin(fs::FS& fs);
    void end();

    LibraryResult openRoot();
    LibraryResult enter(size_t entryIndex);
    LibraryResult parent();
    LibraryResult refreshCurrent();
    void service();

    LibraryState state() const;
    LibraryError error() const;
    const char* currentPath() const;
    uint32_t currentGeneration() const;
    size_t entryCount() const;
    size_t directoryCount() const;
    size_t trackCount() const;

    // Pages are loaded cooperatively by service(). entryAt() automatically
    // requests the containing page and returns Pending until it is resident.
    LibraryResult requestWindow(size_t firstIndex);
    LibraryResult entryAt(size_t index, LibraryEntry& output);
    LibraryResult entryPathAt(size_t index, char* output,
                              size_t outputCapacity);

    // Attach a stable, pinned current-folder TrackSource. queueStartIndex is
    // the selected song's index within the folder-only queue. The caller must
    // keep output alive while PlayerController uses it. For queue replacement,
    // use alternating FolderQueueSource instances and release the old one only
    // after PlayerRuntime persistence is idle.
    LibraryResult selectTrack(size_t entryIndex, FolderQueueSource& output,
                              size_t& queueStartIndex);

    LibraryStats stats() const;
    void clearStats();

  private:
    friend class FolderQueueSource;

    struct CacheSlot {
        bool valid = false;
        uint8_t pinCount = 0;
        uint16_t entryCount = 0;
        uint16_t directoryCount = 0;
        uint16_t trackCount = 0;
        uint32_t generation = 0;
        uint32_t lastUse = 0;
        char directory[kTrackPathCapacity] = {};
    };

    struct Page {
        bool valid = false;
        uint8_t slot = 0;
        uint8_t count = 0;
        uint16_t pageNumber = 0;
        uint32_t generation = 0;
        uint32_t lastUse = 0;
        uint32_t recordOffsets[kPageEntries] = {};
    };

    bool ensureCacheDirectories();
    void clearSessionCacheFiles();
    void closeWorkFiles();
    void fail(LibraryError error);
    LibraryResult openDirectory(const char* path, bool forceRefresh);
    int findCachedSlot(const char* path) const;
    void cleanupDuplicateSlots(const char* path, size_t keepSlot);
    bool recoverCurrentCacheCorruption();
    int chooseSlot();
    void resetSlot(size_t slotIndex);
    void setCurrentReady(size_t slotIndex);

    void serviceOpen();
    void serviceScan();
    void serviceSort();
    void serviceFinalize();
    void servicePageRequest();

    bool acceptEntry(fs::File& entry, const char*& basename,
                     LibraryEntryType& type) const;
    bool appendScanRecord(LibraryEntryType type, const char* basename);
    bool readRecord(fs::File& dataFile, uint32_t offset,
                    LibraryEntryType& type, char* name,
                    size_t nameCapacity) const;
    int compareRecords(uint32_t leftOffset, uint32_t rightOffset);
    static int naturalCompare(const char* left, const char* right);
    static bool isMp3Name(const char* name);
    static const char* basenameOf(const char* path);
    static bool joinPath(const char* directory, const char* basename,
                         char* output, size_t outputCapacity);

    const uint32_t* sortSource() const;
    uint32_t* sortDestination();
    bool writeIndexHeader(fs::File& file, const CacheSlot& slot) const;
    bool readIndexHeader(size_t slotIndex, uint8_t* header) const;
    void indexPath(size_t slotIndex, char* output, size_t capacity) const;
    void dataPath(size_t slotIndex, char* output, size_t capacity) const;
    bool loadPage(uint16_t pageNumber);
    Page* findPage(size_t slotIndex, uint32_t generation,
                   uint16_t pageNumber);
    Page* choosePage();
    bool pageOffsetAt(size_t entryIndex, uint32_t& output);
    bool slotStillValid(size_t slotIndex, uint32_t generation) const;
    bool pinSlot(size_t slotIndex, uint32_t generation);
    void unpinSlot(size_t slotIndex, uint32_t generation);

    fs::FS* fs_ = nullptr;
    LibraryState state_ = LibraryState::Idle;
    LibraryError error_ = LibraryError::None;
    CacheSlot slots_[kCacheSlotCount]{};
    Page pages_[kPageCount]{};
    int8_t currentSlot_ = -1;
    int8_t workSlot_ = -1;
    uint32_t generationCounter_ = 0;
    uint32_t useCounter_ = 0;

    fs::File directoryFile_;
    fs::File scanDataFile_;
    fs::File sortDataFile_;
    fs::File indexFile_;

    uint32_t offsetsA_[kMaxDirectoryEntries]{};
    uint32_t offsetsB_[kMaxDirectoryEntries]{};
    size_t scanCount_ = 0;
    bool sortSourceIsA_ = true;
    size_t sortWidth_ = 1;
    size_t sortLeft_ = 0;
    size_t mergeLeft_ = 0;
    size_t mergeLeftEnd_ = 0;
    size_t mergeRight_ = 0;
    size_t mergeRightEnd_ = 0;
    size_t mergeOutput_ = 0;
    bool mergeActive_ = false;
    size_t finalizeIndex_ = 0;
    bool finalizeOpened_ = false;

    bool pageRequestPending_ = false;
    uint16_t requestedPage_ = 0;
    char leftName_[kTrackPathCapacity]{};
    char rightName_[kTrackPathCapacity]{};
    LibraryStats stats_{};
};

const char* libraryStateName(LibraryState state);
const char* libraryErrorName(LibraryError error);

}  // namespace player
}  // namespace adv_walkman
