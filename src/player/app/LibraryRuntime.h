#pragma once

#include <FS.h>

#include <cstddef>
#include <cstdint>

#include "player/app/PlayerRuntime.h"
#include "player/library/FolderQueueSource.h"
#include "player/library/MetadataCache.h"
#include "player/library/Mp3MetadataReader.h"
#include "player/library/MusicLibrary.h"
#include "player/library/RecentTracksStore.h"

namespace adv_walkman {
namespace player {

// Coordinates Library work after PlayerRuntime has received service. It keeps
// browser/cache lifetime separate from the pinned TrackSource used by Player.
class LibraryRuntime final {
  public:
    static constexpr uint32_t kRecentThresholdMs = 5000;

    LibraryRuntime() = default;
    ~LibraryRuntime();

    LibraryRuntime(const LibraryRuntime&) = delete;
    LibraryRuntime& operator=(const LibraryRuntime&) = delete;

    bool begin(fs::FS& fs, PlayerRuntime& player,
               const char* recentSlotA = RecentTracksStore::kRecentSlotA,
               const char* recentSlotB = RecentTracksStore::kRecentSlotB);
    // The application must call PlayerRuntime::service() first, then this.
    void service();

    MusicLibrary& library();
    const MusicLibrary& library() const;

    // A Pending result is retained and retried while the browser remains in
    // the same directory. Navigating elsewhere safely cancels the selection.
    LibraryResult selectTrack(size_t entryIndex, bool autoplay = true);
    bool selectionPending() const;

    // Metadata requests are on demand. The latest successful result is copied
    // out by metadata(); warnings remain visible through metadataStatus().
    LibraryResult requestMetadata(size_t entryIndex);
    bool metadata(Mp3Metadata& output) const;
    Mp3MetadataStatus metadataStatus() const;
    size_t metadataCacheSize() const;

    RecentTracksResult recentResult() const;
    size_t recentCount() const;
    bool recentPathAt(size_t index, char* output,
                      size_t outputCapacity) const;

  private:
    void shutdown();
    LibraryResult tryPendingSelection();
    void serviceMetadata();
    LibraryResult tryPendingMetadata();
    void serviceRecent(uint32_t now);
    void resetRecentObservation(const char* path, uint32_t now);
    static bool samePathAsciiCaseInsensitive(const char* left,
                                             const char* right);

    fs::FS* fs_ = nullptr;
    PlayerRuntime* player_ = nullptr;
    MusicLibrary library_;
    FolderQueueSource queueSources_[2];
    int8_t activeQueueSource_ = -1;

    bool pendingSelection_ = false;
    bool pendingSelectionAutoplay_ = true;
    size_t pendingSelectionEntry_ = 0;
    uint32_t pendingSelectionGeneration_ = 0;
    char pendingSelectionDirectory_[kTrackPathCapacity] = {};

    Mp3MetadataReader metadataReader_;
    MetadataCache metadataCache_;
    Mp3Metadata latestMetadata_;
    Mp3MetadataStatus latestMetadataStatus_;
    bool latestMetadataReady_ = false;
    bool pendingMetadataPath_ = false;
    bool metadataRequestActive_ = false;
    size_t pendingMetadataEntry_ = 0;
    uint32_t pendingMetadataGeneration_ = 0;
    char pendingMetadataDirectory_[kTrackPathCapacity] = {};
    char metadataPath_[kTrackPathCapacity] = {};

    RecentTracksStore recent_;
    RecentTracksResult recentResult_ = RecentTracksResult::NotFound;
    char observedRecentPath_[kTrackPathCapacity] = {};
    uint32_t recentPlayingMs_ = 0;
    uint32_t recentLastTickMs_ = 0;
    uint32_t recentNextRetryAtMs_ = 0;
    bool recentRecorded_ = false;
    bool recentRecordFailed_ = false;
    uint8_t serviceLane_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
