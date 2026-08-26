#pragma once

#include <FS.h>

#include <cstddef>
#include <cstdint>

#include "player/core/CoreTypes.h"
#include "player/core/TrackSource.h"

namespace adv_walkman {
namespace player {

class MusicLibrary;

// A current-folder queue backed by one pinned MusicLibrary cache slot. The
// MusicLibrary must outlive this source. This type is intentionally non-copyable
// because its lifetime owns a cache pin.
class FolderQueueSource final : public TrackSource {
  public:
    FolderQueueSource() = default;
    ~FolderQueueSource();

    FolderQueueSource(const FolderQueueSource&) = delete;
    FolderQueueSource& operator=(const FolderQueueSource&) = delete;

    size_t count() const override;
    bool pathAt(size_t index, char* output,
                size_t outputCapacity) const override;

    bool valid() const;
    void release();

  private:
    friend class MusicLibrary;

    bool attach(MusicLibrary& library, size_t slotIndex,
                uint32_t generation);

    MusicLibrary* library_ = nullptr;
    fs::FS* fs_ = nullptr;
    uint8_t slotIndex_ = 0;
    uint16_t directoryCount_ = 0;
    uint16_t trackCount_ = 0;
    uint32_t generation_ = 0;
    char directory_[kTrackPathCapacity] = {};
    char dataPath_[48] = {};
    char indexPath_[48] = {};
};

}  // namespace player
}  // namespace adv_walkman
