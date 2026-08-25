#pragma once

#include <cstddef>

namespace adv_walkman {
namespace player {

// Supplies paths on demand so a queue of up to 1024 tracks does not keep every
// UTF-8 path resident in RAM. Implementations must never truncate a path:
// pathAt() returns false when the complete NUL-terminated path does not fit.
class TrackSource {
  public:
    virtual ~TrackSource() = default;

    virtual size_t count() const = 0;
    virtual bool pathAt(size_t index, char* output, size_t outputCapacity) const = 0;
};

}  // namespace player
}  // namespace adv_walkman
