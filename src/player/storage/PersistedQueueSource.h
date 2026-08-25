#pragma once

#include <FS.h>

#include "../core/TrackSource.h"
#include "PersistenceTypes.h"

namespace adv_walkman {
namespace player {

class PlayerStateStore;

// A queue view backed by one verified A/B snapshot on SD. Only offsets and
// lengths stay in RAM; paths are fetched from the snapshot on demand.
class PersistedQueueSource final : public TrackSource {
  public:
    PersistedQueueSource() = default;

    size_t count() const override;
    bool pathAt(size_t index, char* output, size_t outputSize) const override;

    uint32_t generation() const;
    bool valid() const;
    void clear();

  private:
    friend class PlayerStateStore;

    bool attach(fs::FS& fs, const char* slotPath, uint32_t generation);

    fs::FS* fs_ = nullptr;
    char slotPath_[40] = {};
    uint32_t generation_ = 0;
    uint16_t count_ = 0;
    uint32_t pathOffsets_[kPersistedQueueMaxTracks] = {};
    uint16_t pathLengths_[kPersistedQueueMaxTracks] = {};
};

}  // namespace player
}  // namespace adv_walkman
