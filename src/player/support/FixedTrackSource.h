#pragma once

#include <cstring>

#include "player/core/CoreTypes.h"
#include "player/core/TrackSource.h"

namespace adv_walkman {
namespace player {

// Small development-only source for the generated P1 fixtures. P2 replaces
// it with the current-folder library source; Player/Queue stay unchanged.
class FixedTrackSource final : public TrackSource {
  public:
    FixedTrackSource(const char* const* paths, size_t pathCount)
        : paths_(paths), count_(pathCount) {}

    size_t count() const override {
        return count_;
    }

    bool pathAt(size_t index, char* output,
                size_t outputCapacity) const override {
        if (paths_ == nullptr || index >= count_ || output == nullptr ||
            outputCapacity == 0 || paths_[index] == nullptr) {
            return false;
        }
        const size_t length = std::strlen(paths_[index]);
        if (length > kMaxTrackPathBytes || length + 1 > outputCapacity) {
            return false;
        }
        std::memcpy(output, paths_[index], length + 1);
        return true;
    }

  private:
    const char* const* paths_ = nullptr;
    size_t count_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
