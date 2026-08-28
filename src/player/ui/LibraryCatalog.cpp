#include "LibraryCatalog.h"

#include <cstdio>
#include <cstring>

namespace adv_walkman {
namespace player {

void LibraryCatalog::reset() {
    directoryCount_ = 0;
    hasUncategorized_ = false;
    selectedIndex_ = 0;
}

bool LibraryCatalog::sync(MusicLibrary& library) {
    if (library.state() != LibraryState::Ready ||
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) != 0) {
        return false;
    }
    directoryCount_ = library.directoryCount();
    hasUncategorized_ = library.trackCount() != 0;
    if (count() == 0) {
        selectedIndex_ = 0;
    } else if (selectedIndex_ >= count()) {
        selectedIndex_ = count() - 1;
    }
    return true;
}

size_t LibraryCatalog::count() const {
    return directoryCount_ + (hasUncategorized_ ? 1U : 0U);
}

size_t LibraryCatalog::selectedIndex() const {
    return selectedIndex_;
}

void LibraryCatalog::move(int delta) {
    const size_t itemCount = count();
    if (itemCount == 0 || delta == 0) {
        return;
    }
    if (delta > 0) {
        selectedIndex_ = (selectedIndex_ + 1U) % itemCount;
    } else {
        selectedIndex_ = selectedIndex_ == 0 ? itemCount - 1U
                                             : selectedIndex_ - 1U;
    }
}

LibraryResult LibraryCatalog::selected(MusicLibrary& library,
                                       LibraryDescriptor& output) const {
    output = LibraryDescriptor{};
    if (library.state() != LibraryState::Ready ||
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) != 0 ||
        selectedIndex_ >= count()) {
        return LibraryResult::Pending;
    }
    if (selectedIndex_ == directoryCount_) {
        output.uncategorized = true;
        output.rootEntryIndex = directoryCount_;
        std::strcpy(output.name, "未分类");
        std::strcpy(output.path, MusicLibrary::kMusicRoot);
        return LibraryResult::Ok;
    }

    LibraryEntry entry;
    const LibraryResult result = library.entryAt(selectedIndex_, entry);
    if (result != LibraryResult::Ok) {
        return result;
    }
    if (entry.type != LibraryEntryType::Directory) {
        return LibraryResult::Error;
    }
    const int written = std::snprintf(output.path, sizeof(output.path),
                                      "%s/%s", MusicLibrary::kMusicRoot,
                                      entry.name);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(output.path)) {
        return LibraryResult::Error;
    }
    output.rootEntryIndex = selectedIndex_;
    std::strncpy(output.name, entry.name, sizeof(output.name) - 1);
    return LibraryResult::Ok;
}

}  // namespace player
}  // namespace adv_walkman
