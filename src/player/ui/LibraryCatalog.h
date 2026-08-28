#pragma once

#include <cstddef>

#include "player/core/CoreTypes.h"
#include "player/library/MusicLibrary.h"

namespace adv_walkman {
namespace player {

struct LibraryDescriptor {
    bool uncategorized = false;
    size_t rootEntryIndex = 0;
    char name[kTrackPathCapacity] = {};
    char path[kTrackPathCapacity] = {};
};

// A zero-copy projection of the /Music root: each first-level directory is a
// library, and root-level MP3 files appear as one synthetic Uncategorized item.
class LibraryCatalog final {
  public:
    void reset();
    bool sync(MusicLibrary& library);
    size_t count() const;
    size_t selectedIndex() const;
    void move(int delta);
    LibraryResult selected(MusicLibrary& library,
                           LibraryDescriptor& output) const;
    LibraryResult at(size_t index, MusicLibrary& library, LibraryDescriptor& output) const;

  private:
    size_t directoryCount_ = 0;
    bool hasUncategorized_ = false;
    size_t selectedIndex_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
