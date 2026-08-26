#include "player/library/FolderQueueSource.h"

#include <cstring>

#include "player/library/LibraryCacheFormat.h"
#include "player/library/MusicLibrary.h"

namespace adv_walkman {
namespace player {

FolderQueueSource::~FolderQueueSource() {
    release();
}

size_t FolderQueueSource::count() const {
    return valid() ? trackCount_ : 0;
}

bool FolderQueueSource::pathAt(size_t index, char* output,
                               size_t outputCapacity) const {
    if (output == nullptr || outputCapacity == 0) {
        return false;
    }
    output[0] = '\0';
    if (!valid() || index >= trackCount_ ||
        !library_->slotStillValid(slotIndex_, generation_)) {
        return false;
    }

    uint8_t header[library_cache::kIndexHeaderSize] = {};
    if (!library_->readIndexHeader(slotIndex_, header)) {
        return false;
    }

    fs::File indexFile = fs_->open(indexPath_, FILE_READ);
    const size_t sortedIndex = static_cast<size_t>(directoryCount_) + index;
    const size_t indexOffset = library_cache::kIndexHeaderSize +
                               sortedIndex * sizeof(uint32_t);
    if (!indexFile || !indexFile.seek(indexOffset)) {
        if (indexFile) {
            indexFile.close();
        }
        return false;
    }
    uint8_t offsetBytes[sizeof(uint32_t)] = {};
    if (indexFile.read(offsetBytes, sizeof(offsetBytes)) !=
        sizeof(offsetBytes)) {
        indexFile.close();
        return false;
    }
    indexFile.close();

    fs::File dataFile = fs_->open(dataPath_, FILE_READ);
    if (!dataFile) {
        return false;
    }
    LibraryEntryType type = LibraryEntryType::Track;
    char basename[kTrackPathCapacity];
    const bool read = library_->readRecord(
        dataFile, library_cache::readLe32(offsetBytes), type, basename,
        sizeof(basename));
    dataFile.close();
    if (!read || type != LibraryEntryType::Track ||
        !MusicLibrary::joinPath(directory_, basename, output,
                                outputCapacity)) {
        output[0] = '\0';
        return false;
    }
    return true;
}

bool FolderQueueSource::valid() const {
    return library_ != nullptr && fs_ != nullptr && generation_ != 0 &&
           dataPath_[0] != '\0' && indexPath_[0] != '\0';
}

void FolderQueueSource::release() {
    if (library_ != nullptr && generation_ != 0) {
        library_->unpinSlot(slotIndex_, generation_);
    }
    library_ = nullptr;
    fs_ = nullptr;
    slotIndex_ = 0;
    directoryCount_ = 0;
    trackCount_ = 0;
    generation_ = 0;
    directory_[0] = '\0';
    dataPath_[0] = '\0';
    indexPath_[0] = '\0';
}

bool FolderQueueSource::attach(MusicLibrary& library, size_t slotIndex,
                               uint32_t generation) {
    release();
    if (library.fs_ == nullptr ||
        !library.slotStillValid(slotIndex, generation)) {
        return false;
    }
    const MusicLibrary::CacheSlot& slot = library.slots_[slotIndex];
    if (slot.trackCount > kMaxQueueTracks ||
        !library.pinSlot(slotIndex, generation)) {
        return false;
    }

    library_ = &library;
    fs_ = library.fs_;
    slotIndex_ = static_cast<uint8_t>(slotIndex);
    directoryCount_ = slot.directoryCount;
    trackCount_ = slot.trackCount;
    generation_ = generation;
    std::strncpy(directory_, slot.directory, sizeof(directory_) - 1);
    directory_[sizeof(directory_) - 1] = '\0';
    library.dataPath(slotIndex, dataPath_, sizeof(dataPath_));
    library.indexPath(slotIndex, indexPath_, sizeof(indexPath_));
    return true;
}

}  // namespace player
}  // namespace adv_walkman
