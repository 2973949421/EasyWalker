#include "player/library/MusicLibrary.h"

#include <Arduino.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

#include "player/library/FolderQueueSource.h"
#include "player/library/LibraryCacheFormat.h"

namespace adv_walkman {
namespace player {
namespace {

constexpr const char* kCacheRoot = "/ADVWalkman/cache/library";

bool writeExact(fs::File& file, const uint8_t* data, size_t length) {
    return file.write(data, length) == length;
}

uint8_t foldAscii(uint8_t value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<uint8_t>(value + ('a' - 'A'));
    }
    return value;
}

}  // namespace

MusicLibrary::~MusicLibrary() {
    end();
}

bool MusicLibrary::begin(fs::FS& fs) {
    end();
    fs_ = &fs;
    if (!ensureCacheDirectories()) {
        fs_ = nullptr;
        state_ = LibraryState::Error;
        error_ = LibraryError::IoError;
        return false;
    }
    clearSessionCacheFiles();
    state_ = LibraryState::Idle;
    error_ = LibraryError::None;
    return true;
}

void MusicLibrary::end() {
    closeWorkFiles();
    for (Page& page : pages_) {
        page = Page{};
    }
    for (CacheSlot& slot : slots_) {
        slot = CacheSlot{};
    }
    fs_ = nullptr;
    state_ = LibraryState::Idle;
    error_ = LibraryError::None;
    currentSlot_ = -1;
    workSlot_ = -1;
    pageRequestPending_ = false;
}

LibraryResult MusicLibrary::openRoot() {
    return openDirectory(kMusicRoot, false);
}

LibraryResult MusicLibrary::enter(size_t entryIndex) {
    if (state_ != LibraryState::Ready) {
        error_ = LibraryError::NotReady;
        return LibraryResult::Error;
    }

    LibraryEntry entry;
    const LibraryResult result = entryAt(entryIndex, entry);
    if (result != LibraryResult::Ok) {
        return result;
    }
    if (entry.type != LibraryEntryType::Directory) {
        error_ = LibraryError::NotDirectory;
        return LibraryResult::Error;
    }

    char path[kTrackPathCapacity];
    if (!joinPath(currentPath(), entry.name, path, sizeof(path))) {
        error_ = LibraryError::PathTooLong;
        return LibraryResult::Error;
    }
    return openDirectory(path, false);
}

LibraryResult MusicLibrary::parent() {
    if (state_ != LibraryState::Ready) {
        error_ = LibraryError::NotReady;
        return LibraryResult::Error;
    }

    const char* current = currentPath();
    if (std::strcmp(current, kMusicRoot) == 0) {
        error_ = LibraryError::None;
        return LibraryResult::Ok;
    }

    char path[kTrackPathCapacity];
    std::strncpy(path, current, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    char* separator = std::strrchr(path, '/');
    if (separator == nullptr || separator < path + std::strlen(kMusicRoot)) {
        error_ = LibraryError::InvalidArgument;
        return LibraryResult::Error;
    }
    *separator = '\0';
    if (std::strlen(path) < std::strlen(kMusicRoot)) {
        std::strcpy(path, kMusicRoot);
    }
    return openDirectory(path, false);
}

LibraryResult MusicLibrary::refreshCurrent() {
    if ((state_ != LibraryState::Ready && state_ != LibraryState::Error) ||
        currentSlot_ < 0) {
        error_ = LibraryError::NotReady;
        return LibraryResult::Error;
    }
    char path[kTrackPathCapacity];
    std::strncpy(path, currentPath(), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    return openDirectory(path, true);
}

void MusicLibrary::service() {
    const LibraryState servicedState = state_;
    const uint32_t startedAt = micros();
    switch (state_) {
        case LibraryState::Open:
            serviceOpen();
            break;
        case LibraryState::Scan:
            serviceScan();
            break;
        case LibraryState::Sort:
            serviceSort();
            break;
        case LibraryState::Finalize:
            serviceFinalize();
            break;
        case LibraryState::Ready:
            servicePageRequest();
            break;
        default:
            break;
    }
    const uint32_t elapsed = micros() - startedAt;
    stats_.serviceMaxUs = std::max(stats_.serviceMaxUs, elapsed);
    switch (servicedState) {
        case LibraryState::Scan:
            stats_.scanMaxUs = std::max(stats_.scanMaxUs, elapsed);
            break;
        case LibraryState::Sort:
            stats_.sortSliceMaxUs =
                std::max(stats_.sortSliceMaxUs, elapsed);
            break;
        case LibraryState::Finalize:
            stats_.finalizeMaxUs = std::max(stats_.finalizeMaxUs, elapsed);
            break;
        default:
            break;
    }
    if (elapsed > kServiceBudgetUs) {
        ++stats_.overBudgetSteps;
    }
}

LibraryState MusicLibrary::state() const {
    return state_;
}

LibraryError MusicLibrary::error() const {
    return error_;
}

const char* MusicLibrary::currentPath() const {
    if (state_ != LibraryState::Ready && workSlot_ >= 0) {
        return slots_[workSlot_].directory;
    }
    if (currentSlot_ >= 0) {
        return slots_[currentSlot_].directory;
    }
    return kMusicRoot;
}

uint32_t MusicLibrary::currentGeneration() const {
    if (state_ != LibraryState::Ready && workSlot_ >= 0) {
        return slots_[workSlot_].generation;
    }
    if (currentSlot_ >= 0) {
        return slots_[currentSlot_].generation;
    }
    return 0;
}

size_t MusicLibrary::entryCount() const {
    return state_ == LibraryState::Ready && currentSlot_ >= 0
               ? slots_[currentSlot_].entryCount
               : 0;
}

size_t MusicLibrary::directoryCount() const {
    return state_ == LibraryState::Ready && currentSlot_ >= 0
               ? slots_[currentSlot_].directoryCount
               : 0;
}

size_t MusicLibrary::trackCount() const {
    return state_ == LibraryState::Ready && currentSlot_ >= 0
               ? slots_[currentSlot_].trackCount
               : 0;
}

bool MusicLibrary::workPending() const {
    return state_ == LibraryState::Open || state_ == LibraryState::Scan ||
           state_ == LibraryState::Sort ||
           state_ == LibraryState::Finalize || pageRequestPending_;
}

LibraryResult MusicLibrary::requestWindow(size_t firstIndex) {
    if (state_ != LibraryState::Ready || currentSlot_ < 0) {
        error_ = LibraryError::NotReady;
        return LibraryResult::Error;
    }
    if (entryCount() == 0) {
        if (firstIndex == 0) {
            error_ = LibraryError::None;
            return LibraryResult::Ok;
        }
        error_ = LibraryError::InvalidEntry;
        return LibraryResult::Error;
    }
    if (firstIndex >= entryCount()) {
        error_ = LibraryError::InvalidEntry;
        return LibraryResult::Error;
    }

    const uint16_t pageNumber = static_cast<uint16_t>(firstIndex / kPageEntries);
    CacheSlot& slot = slots_[currentSlot_];
    Page* page = findPage(currentSlot_, slot.generation, pageNumber);
    if (page != nullptr) {
        page->lastUse = ++useCounter_;
        ++stats_.pageHits;
        error_ = LibraryError::None;
        return LibraryResult::Ok;
    }

    if (!pageRequestPending_ || requestedPage_ != pageNumber) {
        requestedPage_ = pageNumber;
        pageRequestPending_ = true;
        ++stats_.pageMisses;
    }
    error_ = LibraryError::None;
    return LibraryResult::Pending;
}

LibraryResult MusicLibrary::entryAt(size_t index, LibraryEntry& output) {
    const uint32_t startedAt = micros();
    const auto finish = [this, startedAt](LibraryResult result) {
        const uint32_t elapsed = static_cast<uint32_t>(micros() - startedAt);
        stats_.entryReadMaxUs = std::max(stats_.entryReadMaxUs, elapsed);
        return result;
    };
    if (state_ != LibraryState::Ready || currentSlot_ < 0) {
        error_ = LibraryError::NotReady;
        return finish(LibraryResult::Error);
    }
    if (index >= entryCount()) {
        error_ = LibraryError::InvalidEntry;
        return finish(LibraryResult::Error);
    }
    error_ = LibraryError::None;

    uint32_t offset = 0;
    if (!pageOffsetAt(index, offset)) {
        return finish(state_ == LibraryState::Error ? LibraryResult::Error
                                                    : LibraryResult::Pending);
    }

    char path[48];
    dataPath(currentSlot_, path, sizeof(path));
    fs::File file = fs_->open(path, FILE_READ);
    if (!file) {
        return finish(recoverCurrentCacheCorruption()
                          ? LibraryResult::Pending
                          : LibraryResult::Error);
    }

    LibraryEntryType type = LibraryEntryType::Track;
    const bool read = readRecord(file, offset, type, output.name,
                                 sizeof(output.name));
    file.close();
    if (!read) {
        return finish(recoverCurrentCacheCorruption()
                          ? LibraryResult::Pending
                          : LibraryResult::Error);
    }
    output.type = type;
    output.index = index;
    error_ = LibraryError::None;
    return finish(LibraryResult::Ok);
}

LibraryResult MusicLibrary::entryPathAt(size_t index, char* output,
                                        size_t outputCapacity) {
    if (output == nullptr || outputCapacity == 0) {
        error_ = LibraryError::InvalidArgument;
        return LibraryResult::Error;
    }
    output[0] = '\0';
    LibraryEntry entry;
    const LibraryResult result = entryAt(index, entry);
    if (result != LibraryResult::Ok) {
        return result;
    }
    if (!joinPath(currentPath(), entry.name, output, outputCapacity)) {
        error_ = LibraryError::PathTooLong;
        return LibraryResult::Error;
    }
    error_ = LibraryError::None;
    return LibraryResult::Ok;
}

LibraryResult MusicLibrary::selectTrack(size_t entryIndex,
                                        FolderQueueSource& output,
                                        size_t& queueStartIndex) {
    if (state_ != LibraryState::Ready || currentSlot_ < 0) {
        error_ = LibraryError::NotReady;
        return LibraryResult::Error;
    }
    if (entryIndex < directoryCount() || entryIndex >= entryCount()) {
        error_ = LibraryError::InvalidEntry;
        return LibraryResult::Error;
    }
    if (trackCount() > kMaxQueueTracks) {
        error_ = LibraryError::QueueTooLarge;
        return LibraryResult::Error;
    }
    if (!output.attach(*this, currentSlot_, slots_[currentSlot_].generation)) {
        error_ = LibraryError::CacheCorrupt;
        return LibraryResult::Error;
    }
    queueStartIndex = entryIndex - directoryCount();
    error_ = LibraryError::None;
    return LibraryResult::Ok;
}

LibraryStats MusicLibrary::stats() const {
    return stats_;
}

void MusicLibrary::clearStats() {
    stats_ = LibraryStats{};
    if (sortScratch_ != nullptr) {
        stats_.scratchBytes = sizeof(SortScratch);
        if (state_ == LibraryState::Scan) {
            stats_.scanWriteBufferBytes = kScanWriteBufferBytes;
        }
    }
}

bool MusicLibrary::ensureCacheDirectories() {
    if (fs_ == nullptr) {
        return false;
    }
    const char* directories[] = {
        "/ADVWalkman", "/ADVWalkman/cache", kCacheRoot,
    };
    for (const char* directory : directories) {
        if (!fs_->exists(directory) && !fs_->mkdir(directory)) {
            return false;
        }
    }
    return true;
}

void MusicLibrary::clearSessionCacheFiles() {
    if (fs_ == nullptr) {
        return;
    }
    char path[48];
    for (size_t index = 0; index < kCacheSlotCount; ++index) {
        dataPath(index, path, sizeof(path));
        fs_->remove(path);
        indexPath(index, path, sizeof(path));
        fs_->remove(path);
    }
}

void MusicLibrary::closeWorkFiles() {
    if (directoryFile_) {
        directoryFile_.close();
    }
    if (scanDataFile_) {
        scanDataFile_.close();
    }
    if (sortDataFile_) {
        sortDataFile_.close();
    }
    if (indexFile_) {
        indexFile_.close();
    }
    scanWriteBufferedBytes_ = 0;
    scanDataLogicalPosition_ = 0;
    releaseSortScratch();
}

void MusicLibrary::fail(LibraryError error) {
    closeWorkFiles();
    if (workSlot_ >= 0 && slots_[workSlot_].pinCount == 0) {
        resetSlot(workSlot_);
    }
    workSlot_ = -1;
    pageRequestPending_ = false;
    error_ = error;
    state_ = LibraryState::Error;
}

LibraryResult MusicLibrary::openDirectory(const char* path,
                                          bool forceRefresh) {
    if (fs_ == nullptr || path == nullptr) {
        error_ = LibraryError::InvalidArgument;
        return LibraryResult::Error;
    }
    const size_t pathLength = std::strlen(path);
    const size_t rootLength = std::strlen(kMusicRoot);
    if (pathLength == 0 || pathLength > kMaxTrackPathBytes ||
        std::strncmp(path, kMusicRoot, rootLength) != 0 ||
        (pathLength > rootLength && path[rootLength] != '/') ||
        std::strstr(path, "/../") != nullptr ||
        (pathLength >= 3 && std::strcmp(path + pathLength - 3, "/..") == 0)) {
        error_ = pathLength > kMaxTrackPathBytes
                     ? LibraryError::PathTooLong
                     : LibraryError::InvalidArgument;
        return LibraryResult::Error;
    }

    closeWorkFiles();
    workSlot_ = -1;
    pageRequestPending_ = false;
    if (!forceRefresh) {
        const int cached = findCachedSlot(path);
        if (cached >= 0) {
            ++stats_.cacheHits;
            setCurrentReady(cached);
            return LibraryResult::Ok;
        }
    }

    ++stats_.cacheMisses;
    if (forceRefresh) {
        // A pinned slot may still back the active Player Queue and must remain
        // immutable. All other same-path generations are stale and can be
        // discarded before choosing the rebuild target.
        cleanupDuplicateSlots(path, kCacheSlotCount);
    }
    const int slotIndex = chooseSlot();
    if (slotIndex < 0) {
        error_ = LibraryError::CacheBusy;
        state_ = LibraryState::Error;
        return LibraryResult::Error;
    }
    if (slots_[slotIndex].valid) {
        ++stats_.cacheEvictions;
    }
    resetSlot(slotIndex);
    CacheSlot& slot = slots_[slotIndex];
    std::strncpy(slot.directory, path, sizeof(slot.directory) - 1);
    slot.directory[sizeof(slot.directory) - 1] = '\0';
    slot.generation = ++generationCounter_;
    slot.lastUse = ++useCounter_;
    workSlot_ = static_cast<int8_t>(slotIndex);
    scanCount_ = 0;
    sortSourceIsA_ = true;
    sortWidth_ = 1;
    sortLeft_ = 0;
    mergeActive_ = false;
    finalizeIndex_ = 0;
    finalizeOpened_ = false;
    scanWriteBufferedBytes_ = 0;
    scanDataLogicalPosition_ = 0;
    error_ = LibraryError::None;
    state_ = LibraryState::Open;
    return LibraryResult::Pending;
}

int MusicLibrary::findCachedSlot(const char* path) const {
    int selected = -1;
    uint32_t newestGeneration = 0;
    for (size_t index = 0; index < kCacheSlotCount; ++index) {
        if (slots_[index].valid &&
            std::strcmp(slots_[index].directory, path) == 0 &&
            (selected < 0 || slots_[index].generation > newestGeneration)) {
            selected = static_cast<int>(index);
            newestGeneration = slots_[index].generation;
        }
    }
    return selected;
}

void MusicLibrary::cleanupDuplicateSlots(const char* path, size_t keepSlot) {
    if (path == nullptr) {
        return;
    }
    for (size_t index = 0; index < kCacheSlotCount; ++index) {
        if (index != keepSlot && slots_[index].valid &&
            slots_[index].pinCount == 0 &&
            std::strcmp(slots_[index].directory, path) == 0) {
            resetSlot(index);
        }
    }
}

bool MusicLibrary::recoverCurrentCacheCorruption() {
    if (state_ != LibraryState::Ready || currentSlot_ < 0) {
        fail(LibraryError::CacheCorrupt);
        return false;
    }
    if (slots_[currentSlot_].pinCount != 0) {
        // The cache is also the active Queue source. Never remove or overwrite
        // it underneath PlayerController; surface a stable explicit error.
        fail(LibraryError::CacheCorrupt);
        return false;
    }

    char path[kTrackPathCapacity];
    std::strncpy(path, slots_[currentSlot_].directory, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    return openDirectory(path, true) != LibraryResult::Error;
}

int MusicLibrary::chooseSlot() {
    for (size_t index = 0; index < kCacheSlotCount; ++index) {
        if (!slots_[index].valid && slots_[index].pinCount == 0) {
            return static_cast<int>(index);
        }
    }

    int selected = -1;
    uint32_t oldestUse = UINT32_MAX;
    for (size_t index = 0; index < kCacheSlotCount; ++index) {
        if (slots_[index].pinCount == 0 && slots_[index].lastUse < oldestUse) {
            selected = static_cast<int>(index);
            oldestUse = slots_[index].lastUse;
        }
    }
    return selected;
}

void MusicLibrary::resetSlot(size_t slotIndex) {
    if (slotIndex >= kCacheSlotCount || slots_[slotIndex].pinCount != 0) {
        return;
    }
    char path[48];
    if (fs_ != nullptr) {
        dataPath(slotIndex, path, sizeof(path));
        fs_->remove(path);
        indexPath(slotIndex, path, sizeof(path));
        fs_->remove(path);
    }
    for (Page& page : pages_) {
        if (page.valid && page.slot == slotIndex) {
            page = Page{};
        }
    }
    slots_[slotIndex] = CacheSlot{};
    if (currentSlot_ == static_cast<int8_t>(slotIndex)) {
        currentSlot_ = -1;
    }
}

void MusicLibrary::setCurrentReady(size_t slotIndex) {
    currentSlot_ = static_cast<int8_t>(slotIndex);
    workSlot_ = -1;
    slots_[slotIndex].lastUse = ++useCounter_;
    cleanupDuplicateSlots(slots_[slotIndex].directory, slotIndex);
    state_ = LibraryState::Ready;
    error_ = LibraryError::None;
    pageRequestPending_ = false;
    if (slots_[slotIndex].entryCount != 0) {
        requestWindow(0);
    }
}

void MusicLibrary::serviceOpen() {
    if (workSlot_ < 0 || fs_ == nullptr) {
        fail(LibraryError::InvalidArgument);
        return;
    }
    CacheSlot& slot = slots_[workSlot_];
    directoryFile_ = fs_->open(slot.directory, FILE_READ);
    if (!directoryFile_ || !directoryFile_.isDirectory()) {
        fail(std::strcmp(slot.directory, kMusicRoot) == 0
                 ? LibraryError::RootUnavailable
                 : LibraryError::IoError);
        return;
    }

    char path[48];
    dataPath(workSlot_, path, sizeof(path));
    fs_->remove(path);
    scanDataFile_ = fs_->open(path, FILE_WRITE);
    if (!scanDataFile_) {
        fail(LibraryError::IoError);
        return;
    }
    if (!allocateSortScratch()) {
        fail(LibraryError::OutOfMemory);
        return;
    }
    // indicesB is not needed until merge sort starts. Reuse its exact 4096
    // bytes as the scan write buffer, so batching adds no heap/static peak.
    scanWriteBufferedBytes_ = 0;
    scanDataLogicalPosition_ = 0;
    stats_.scanWriteBufferBytes = kScanWriteBufferBytes;
    state_ = LibraryState::Scan;
}

void MusicLibrary::serviceScan() {
    if (workSlot_ < 0 || !directoryFile_ || !scanDataFile_) {
        fail(LibraryError::IoError);
        return;
    }

    bool isDirectory = false;
    const uint32_t readDirStartedAt = micros();
    const String entryPath = directoryFile_.getNextFileName(&isDirectory);
    stats_.scanReadDirMaxUs = std::max(
        stats_.scanReadDirMaxUs,
        static_cast<uint32_t>(micros() - readDirStartedAt));
    if (entryPath.isEmpty()) {
        const uint32_t eofStartedAt = micros();
        const uint32_t closeStartedAt = micros();
        directoryFile_.close();
        stats_.scanCloseMaxUs = std::max(
            stats_.scanCloseMaxUs,
            static_cast<uint32_t>(micros() - closeStartedAt));
        const bool bufferedWriteOk = flushScanWriteBuffer();
        // This cache is session-only and is reopened only after close(). An
        // explicit fsync here adds latency without improving its guarantees.
        scanDataFile_.close();
        if (!bufferedWriteOk) {
            stats_.scanEofFlushMaxUs = std::max(
                stats_.scanEofFlushMaxUs,
                static_cast<uint32_t>(micros() - eofStartedAt));
            fail(LibraryError::IoError);
            return;
        }
        if (scanCount_ <= 1) {
            state_ = LibraryState::Finalize;
            stats_.scanEofFlushMaxUs = std::max(
                stats_.scanEofFlushMaxUs,
                static_cast<uint32_t>(micros() - eofStartedAt));
            return;
        }
        char path[48];
        dataPath(workSlot_, path, sizeof(path));
        sortDataFile_ = fs_->open(path, FILE_READ);
        if (!sortDataFile_) {
            stats_.scanEofFlushMaxUs = std::max(
                stats_.scanEofFlushMaxUs,
                static_cast<uint32_t>(micros() - eofStartedAt));
            fail(LibraryError::IoError);
            return;
        }
        state_ = LibraryState::Sort;
        // This metric intentionally includes the indivisible final flush,
        // close, and read-handle open needed to leave Scan. It identifies an
        // EOF transition spike without pretending those FS calls are sliced.
        stats_.scanEofFlushMaxUs = std::max(
            stats_.scanEofFlushMaxUs,
            static_cast<uint32_t>(micros() - eofStartedAt));
        return;
    }

    const char* basename = nullptr;
    LibraryEntryType type = LibraryEntryType::Track;
    const uint32_t acceptStartedAt = micros();
    const bool accepted =
        acceptEntry(entryPath.c_str(), isDirectory, basename, type);
    stats_.scanAcceptMaxUs = std::max(
        stats_.scanAcceptMaxUs,
        static_cast<uint32_t>(micros() - acceptStartedAt));
    if (accepted) {
        if (scanCount_ >= kMaxDirectoryEntries) {
            fail(LibraryError::DirectoryTooLarge);
            return;
        }
        const uint32_t appendStartedAt = micros();
        if (!appendScanRecord(type, basename)) {
            stats_.scanAppendMaxUs = std::max(
                stats_.scanAppendMaxUs,
                static_cast<uint32_t>(micros() - appendStartedAt));
            if (state_ != LibraryState::Error) {
                fail(LibraryError::IoError);
            }
            return;
        }
        stats_.scanAppendMaxUs = std::max(
            stats_.scanAppendMaxUs,
            static_cast<uint32_t>(micros() - appendStartedAt));
    } else {
        ++stats_.filteredEntries;
    }
}

void MusicLibrary::serviceSort() {
    if (workSlot_ < 0 || !sortDataFile_ || sortScratch_ == nullptr) {
        fail(LibraryError::IoError);
        return;
    }

    const size_t count = scanCount_;
    const uint32_t sliceStartedAt = micros();
    size_t moves = 0;
    while (moves < kSortMovesPerService) {
        if (!mergeActive_) {
            if (sortLeft_ >= count) {
                sortSourceIsA_ = !sortSourceIsA_;
                sortWidth_ *= 2;
                sortLeft_ = 0;
                if (sortWidth_ >= count) {
                    sortDataFile_.close();
                    state_ = LibraryState::Finalize;
                    return;
                }
            }

            mergeLeft_ = sortLeft_;
            mergeLeftEnd_ = std::min(sortLeft_ + sortWidth_, count);
            mergeRight_ = mergeLeftEnd_;
            mergeRightEnd_ = std::min(sortLeft_ + sortWidth_ * 2, count);
            mergeOutput_ = sortLeft_;
            mergeActive_ = true;
        }

        const uint16_t* source = sortSource();
        uint16_t* destination = sortDestination();
        if (mergeLeft_ < mergeLeftEnd_ && mergeRight_ < mergeRightEnd_) {
            const int comparison =
                compareRecords(source[mergeLeft_], source[mergeRight_]);
            if (state_ == LibraryState::Error) {
                return;
            }
            destination[mergeOutput_++] = comparison <= 0
                                                ? source[mergeLeft_++]
                                                : source[mergeRight_++];
        } else if (mergeLeft_ < mergeLeftEnd_) {
            destination[mergeOutput_++] = source[mergeLeft_++];
        } else if (mergeRight_ < mergeRightEnd_) {
            destination[mergeOutput_++] = source[mergeRight_++];
        }
        ++moves;
        ++stats_.sortMoves;

        if (mergeOutput_ >= mergeRightEnd_) {
            sortLeft_ = mergeRightEnd_;
            mergeActive_ = false;
        }
        if (micros() - sliceStartedAt >= kServiceBudgetUs) {
            break;
        }
    }
}

void MusicLibrary::serviceFinalize() {
    if (workSlot_ < 0 || fs_ == nullptr) {
        fail(LibraryError::IoError);
        return;
    }
    CacheSlot& slot = slots_[workSlot_];
    if (!finalizeOpened_) {
        char path[48];
        indexPath(workSlot_, path, sizeof(path));
        fs_->remove(path);
        indexFile_ = fs_->open(path, FILE_WRITE);
        if (!indexFile_ || !writeIndexHeader(indexFile_, slot)) {
            fail(LibraryError::IoError);
            return;
        }
        finalizeOpened_ = true;
        finalizeIndex_ = 0;
        return;
    }

    uint8_t bytes[kFinalizeBytesPerService];
    const size_t remaining = scanCount_ - finalizeIndex_;
    const size_t count = std::min(
        remaining, kFinalizeBytesPerService / sizeof(uint32_t));
    for (size_t index = 0; index < count; ++index) {
        library_cache::writeLe32(bytes + index * sizeof(uint32_t),
                                 sortedRecordOffset(finalizeIndex_ + index));
    }
    if (count != 0 &&
        !writeExact(indexFile_, bytes, count * sizeof(uint32_t))) {
        fail(LibraryError::IoError);
        return;
    }
    finalizeIndex_ += count;
    if (finalizeIndex_ < scanCount_) {
        return;
    }

    indexFile_.flush();
    indexFile_.close();
    slot.entryCount = static_cast<uint16_t>(scanCount_);
    slot.valid = true;
    slot.lastUse = ++useCounter_;
    const size_t completedSlot = workSlot_;
    finalizeOpened_ = false;
    releaseSortScratch();
    setCurrentReady(completedSlot);
}

bool MusicLibrary::allocateSortScratch() {
    if (sortScratch_ != nullptr) {
        return true;
    }
    sortScratch_ = new (std::nothrow) SortScratch;
    if (sortScratch_ == nullptr) {
        ++stats_.scratchAllocationFailures;
        return false;
    }
    stats_.scratchBytes =
        std::max(stats_.scratchBytes,
                 static_cast<uint32_t>(sizeof(SortScratch)));
    return true;
}

void MusicLibrary::releaseSortScratch() {
    delete sortScratch_;
    sortScratch_ = nullptr;
    scanWriteBufferedBytes_ = 0;
    scanDataLogicalPosition_ = 0;
}

bool MusicLibrary::flushScanWriteBuffer() {
    if (scanWriteBufferedBytes_ == 0) {
        return true;
    }
    if (!scanDataFile_ || sortScratch_ == nullptr ||
        scanWriteBufferedBytes_ > kScanWriteBufferBytes) {
        return false;
    }

    // During Scan, indicesB is deliberately unused. It becomes the merge-sort
    // destination only after this buffer has been completely flushed.
    const uint8_t* buffer =
        reinterpret_cast<const uint8_t*>(sortScratch_->indicesB);
    const size_t bytes = scanWriteBufferedBytes_;
    const uint32_t writeStartedAt = micros();
    ++stats_.scanWriteFlushes;
    const bool success = writeExact(scanDataFile_, buffer, bytes);
    stats_.scanWriteMaxUs = std::max(
        stats_.scanWriteMaxUs,
        static_cast<uint32_t>(micros() - writeStartedAt));
    if (!success) {
        return false;
    }
    stats_.scanBytesWritten += static_cast<uint32_t>(bytes);
    scanWriteBufferedBytes_ = 0;
    return true;
}

void MusicLibrary::servicePageRequest() {
    if (!pageRequestPending_) {
        return;
    }
    const uint16_t page = requestedPage_;
    pageRequestPending_ = false;
    if (!loadPage(page)) {
        recoverCurrentCacheCorruption();
    }
}

bool MusicLibrary::acceptEntry(const char* entryPath, bool isDirectory,
                               const char*& basename,
                               LibraryEntryType& type) const {
    basename = basenameOf(entryPath);
    if (basename == nullptr || basename[0] == '\0' || basename[0] == '.') {
        return false;
    }
    if (isDirectory) {
        type = LibraryEntryType::Directory;
        return true;
    }
    if (isMp3Name(basename)) {
        type = LibraryEntryType::Track;
        return true;
    }
    return false;
}

bool MusicLibrary::appendScanRecord(LibraryEntryType type,
                                    const char* basename) {
    if (workSlot_ < 0 || basename == nullptr || sortScratch_ == nullptr) {
        return false;
    }
    const size_t nameLength = std::strlen(basename);
    if (nameLength == 0 || nameLength > kMaxTrackPathBytes) {
        fail(LibraryError::PathTooLong);
        return false;
    }
    char fullPath[kTrackPathCapacity];
    if (!joinPath(slots_[workSlot_].directory, basename, fullPath,
                  sizeof(fullPath))) {
        fail(LibraryError::PathTooLong);
        return false;
    }

    const size_t recordBytes =
        library_cache::kDataRecordHeaderSize + nameLength;
    if (recordBytes > kScanWriteBufferBytes ||
        scanDataLogicalPosition_ > UINT32_MAX - recordBytes) {
        return false;
    }
    if (scanWriteBufferedBytes_ + recordBytes > kScanWriteBufferBytes &&
        !flushScanWriteBuffer()) {
        return false;
    }

    const uint32_t position = scanDataLogicalPosition_;
    uint8_t* const buffer =
        reinterpret_cast<uint8_t*>(sortScratch_->indicesB);
    uint8_t* const record = buffer + scanWriteBufferedBytes_;
    library_cache::writeLe16(record, static_cast<uint16_t>(nameLength));
    record[2] = static_cast<uint8_t>(type);
    std::memcpy(record + library_cache::kDataRecordHeaderSize, basename,
                nameLength);
    scanWriteBufferedBytes_ += recordBytes;
    scanDataLogicalPosition_ += static_cast<uint32_t>(recordBytes);

    SortKey& key = sortScratch_->keys[scanCount_];
    key.recordOffset = position;
    key.nameLength = static_cast<uint16_t>(nameLength);
    key.type = static_cast<uint8_t>(type);
    key.prefixLength = static_cast<uint8_t>(
        std::min(nameLength, static_cast<size_t>(kSortPrefixBytes)));
    std::memcpy(key.prefix, basename, key.prefixLength);
    sortScratch_->indicesA[scanCount_] =
        static_cast<uint16_t>(scanCount_);
    ++scanCount_;
    CacheSlot& slot = slots_[workSlot_];
    if (type == LibraryEntryType::Directory) {
        ++slot.directoryCount;
    } else {
        ++slot.trackCount;
    }
    return true;
}

bool MusicLibrary::readRecord(fs::File& dataFile, uint32_t offset,
                              LibraryEntryType& type, char* name,
                              size_t nameCapacity) const {
    if (!dataFile || name == nullptr || nameCapacity == 0 ||
        !dataFile.seek(offset)) {
        return false;
    }
    uint8_t header[library_cache::kDataRecordHeaderSize] = {};
    if (dataFile.read(header, sizeof(header)) != sizeof(header)) {
        return false;
    }
    const uint16_t length = library_cache::readLe16(header);
    if (length == 0 || length > kMaxTrackPathBytes ||
        nameCapacity <= length ||
        header[2] > static_cast<uint8_t>(LibraryEntryType::Track)) {
        return false;
    }
    if (dataFile.read(reinterpret_cast<uint8_t*>(name), length) != length) {
        return false;
    }
    name[length] = '\0';
    type = static_cast<LibraryEntryType>(header[2]);
    return true;
}

int MusicLibrary::compareRecords(uint16_t leftIndex, uint16_t rightIndex) {
    ++stats_.sortComparisons;
    if (sortScratch_ == nullptr || leftIndex >= scanCount_ ||
        rightIndex >= scanCount_) {
        fail(LibraryError::CacheCorrupt);
        return 0;
    }

    const SortKey& leftKey = sortScratch_->keys[leftIndex];
    const SortKey& rightKey = sortScratch_->keys[rightIndex];
    int cachedResult = 0;
    if (compareCachedKeys(leftKey, rightKey, cachedResult)) {
        return cachedResult;
    }

    ++stats_.sortFallbackComparisons;
    const uint32_t fallbackStartedAt = micros();
    LibraryEntryType leftType = LibraryEntryType::Track;
    LibraryEntryType rightType = LibraryEntryType::Track;
    if (!readRecord(sortDataFile_, leftKey.recordOffset, leftType, leftName_,
                    sizeof(leftName_)) ||
        !readRecord(sortDataFile_, rightKey.recordOffset, rightType, rightName_,
                    sizeof(rightName_))) {
        const uint32_t failedElapsed =
            static_cast<uint32_t>(micros() - fallbackStartedAt);
        stats_.sortFallbackMaxUs = std::max(
            stats_.sortFallbackMaxUs, failedElapsed);
        fail(LibraryError::CacheCorrupt);
        return 0;
    }
    const int result = leftType != rightType
                           ? leftType == LibraryEntryType::Directory ? -1 : 1
                           : naturalCompare(leftName_, rightName_);
    const uint32_t elapsed = micros() - fallbackStartedAt;
    stats_.sortFallbackMaxUs =
        std::max(stats_.sortFallbackMaxUs, elapsed);
    return result;
}

bool MusicLibrary::compareCachedKeys(const SortKey& left,
                                     const SortKey& right,
                                     int& result) const {
    if (left.type != right.type) {
        result = left.type == static_cast<uint8_t>(LibraryEntryType::Directory)
                     ? -1
                     : 1;
        return true;
    }

    size_t leftPosition = 0;
    size_t rightPosition = 0;
    while (leftPosition < left.prefixLength &&
           rightPosition < right.prefixLength) {
        const uint8_t leftByte = left.prefix[leftPosition];
        const uint8_t rightByte = right.prefix[rightPosition];
        if (leftByte >= '0' && leftByte <= '9' &&
            rightByte >= '0' && rightByte <= '9') {
            size_t leftEnd = leftPosition;
            size_t rightEnd = rightPosition;
            while (leftEnd < left.prefixLength &&
                   left.prefix[leftEnd] >= '0' &&
                   left.prefix[leftEnd] <= '9') {
                ++leftEnd;
            }
            while (rightEnd < right.prefixLength &&
                   right.prefix[rightEnd] >= '0' &&
                   right.prefix[rightEnd] <= '9') {
                ++rightEnd;
            }

            const bool leftRunComplete =
                leftEnd < left.prefixLength || leftEnd >= left.nameLength;
            const bool rightRunComplete =
                rightEnd < right.prefixLength || rightEnd >= right.nameLength;
            if (!leftRunComplete || !rightRunComplete) {
                return false;
            }

            size_t leftSignificant = leftPosition;
            size_t rightSignificant = rightPosition;
            while (leftSignificant < leftEnd &&
                   left.prefix[leftSignificant] == '0') {
                ++leftSignificant;
            }
            while (rightSignificant < rightEnd &&
                   right.prefix[rightSignificant] == '0') {
                ++rightSignificant;
            }
            const size_t leftDigits = leftEnd - leftSignificant;
            const size_t rightDigits = rightEnd - rightSignificant;
            if (leftDigits != rightDigits) {
                result = leftDigits < rightDigits ? -1 : 1;
                return true;
            }
            const int numeric = leftDigits == 0
                                    ? 0
                                    : std::memcmp(left.prefix + leftSignificant,
                                                  right.prefix + rightSignificant,
                                                  leftDigits);
            if (numeric != 0) {
                result = numeric < 0 ? -1 : 1;
                return true;
            }
            leftPosition = leftEnd;
            rightPosition = rightEnd;
            continue;
        }

        const uint8_t foldedLeft = foldAscii(leftByte);
        const uint8_t foldedRight = foldAscii(rightByte);
        if (foldedLeft != foldedRight) {
            result = foldedLeft < foldedRight ? -1 : 1;
            return true;
        }
        ++leftPosition;
        ++rightPosition;
    }

    const bool leftEnded = leftPosition >= left.nameLength;
    const bool rightEnded = rightPosition >= right.nameLength;
    if (leftEnded || rightEnded) {
        if (leftEnded == rightEnded) {
            result = 0;
        } else {
            result = leftEnded ? -1 : 1;
        }
        return true;
    }

    // A shared prefix with at least one truncated name cannot be ordered
    // exactly without consulting the complete cache record. This also covers
    // a UTF-8 sequence or a numeric run crossing the 24-byte boundary.
    return false;
}

int MusicLibrary::naturalCompare(const char* left, const char* right) {
    const uint8_t* lhs = reinterpret_cast<const uint8_t*>(left);
    const uint8_t* rhs = reinterpret_cast<const uint8_t*>(right);
    while (*lhs != 0 && *rhs != 0) {
        if (*lhs >= '0' && *lhs <= '9' && *rhs >= '0' && *rhs <= '9') {
            const uint8_t* lhsEnd = lhs;
            const uint8_t* rhsEnd = rhs;
            while (*lhsEnd >= '0' && *lhsEnd <= '9') {
                ++lhsEnd;
            }
            while (*rhsEnd >= '0' && *rhsEnd <= '9') {
                ++rhsEnd;
            }
            const uint8_t* lhsSignificant = lhs;
            const uint8_t* rhsSignificant = rhs;
            while (lhsSignificant < lhsEnd && *lhsSignificant == '0') {
                ++lhsSignificant;
            }
            while (rhsSignificant < rhsEnd && *rhsSignificant == '0') {
                ++rhsSignificant;
            }
            const size_t lhsDigits = lhsEnd - lhsSignificant;
            const size_t rhsDigits = rhsEnd - rhsSignificant;
            if (lhsDigits != rhsDigits) {
                return lhsDigits < rhsDigits ? -1 : 1;
            }
            const int numeric = lhsDigits == 0
                                    ? 0
                                    : std::memcmp(lhsSignificant,
                                                  rhsSignificant, lhsDigits);
            if (numeric != 0) {
                return numeric < 0 ? -1 : 1;
            }
            lhs = lhsEnd;
            rhs = rhsEnd;
            continue;
        }

        const uint8_t foldedLeft = foldAscii(*lhs);
        const uint8_t foldedRight = foldAscii(*rhs);
        if (foldedLeft != foldedRight) {
            return foldedLeft < foldedRight ? -1 : 1;
        }
        ++lhs;
        ++rhs;
    }
    if (*lhs == *rhs) {
        return 0;
    }
    return *lhs == 0 ? -1 : 1;
}

bool MusicLibrary::isMp3Name(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const size_t length = std::strlen(name);
    if (length < 4 || name[length - 4] != '.') {
        return false;
    }
    return foldAscii(static_cast<uint8_t>(name[length - 3])) == 'm' &&
           foldAscii(static_cast<uint8_t>(name[length - 2])) == 'p' &&
           foldAscii(static_cast<uint8_t>(name[length - 1])) == '3';
}

const char* MusicLibrary::basenameOf(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }
    const char* slash = std::strrchr(path, '/');
    const char* backslash = std::strrchr(path, '\\');
    const char* separator = slash;
    if (backslash != nullptr && (separator == nullptr || backslash > separator)) {
        separator = backslash;
    }
    return separator == nullptr ? path : separator + 1;
}

bool MusicLibrary::joinPath(const char* directory, const char* basename,
                            char* output, size_t outputCapacity) {
    if (directory == nullptr || basename == nullptr || output == nullptr ||
        outputCapacity == 0 || basename[0] == '\0' ||
        std::strchr(basename, '/') != nullptr ||
        std::strchr(basename, '\\') != nullptr) {
        return false;
    }
    const size_t directoryLength = std::strlen(directory);
    const size_t nameLength = std::strlen(basename);
    const bool needsSlash = directoryLength == 0 || directory[directoryLength - 1] != '/';
    const size_t total = directoryLength + (needsSlash ? 1 : 0) + nameLength;
    if (total > kMaxTrackPathBytes || outputCapacity <= total) {
        return false;
    }
    std::memcpy(output, directory, directoryLength);
    size_t position = directoryLength;
    if (needsSlash) {
        output[position++] = '/';
    }
    std::memcpy(output + position, basename, nameLength);
    output[total] = '\0';
    return true;
}

const uint16_t* MusicLibrary::sortSource() const {
    if (sortScratch_ == nullptr) {
        return nullptr;
    }
    return sortSourceIsA_ ? sortScratch_->indicesA
                          : sortScratch_->indicesB;
}

uint16_t* MusicLibrary::sortDestination() {
    if (sortScratch_ == nullptr) {
        return nullptr;
    }
    return sortSourceIsA_ ? sortScratch_->indicesB
                          : sortScratch_->indicesA;
}

uint32_t MusicLibrary::sortedRecordOffset(size_t sortedIndex) const {
    if (sortScratch_ == nullptr || sortedIndex >= scanCount_) {
        return 0;
    }
    const uint16_t keyIndex = sortSource()[sortedIndex];
    return keyIndex < scanCount_
               ? sortScratch_->keys[keyIndex].recordOffset
               : 0;
}

bool MusicLibrary::writeIndexHeader(fs::File& file,
                                    const CacheSlot& slot) const {
    uint8_t header[library_cache::kIndexHeaderSize] = {};
    library_cache::writeLe32(header, library_cache::kIndexMagic);
    library_cache::writeLe16(header + 4, library_cache::kIndexVersion);
    library_cache::writeLe16(header + 6,
                             library_cache::kIndexHeaderSize);
    library_cache::writeLe32(header + 8, slot.generation);
    library_cache::writeLe16(header + 12,
                             static_cast<uint16_t>(scanCount_));
    library_cache::writeLe16(header + 14, slot.directoryCount);
    library_cache::writeLe16(header + 16, slot.trackCount);
    return writeExact(file, header, sizeof(header));
}

bool MusicLibrary::readIndexHeader(size_t slotIndex, uint8_t* header) const {
    if (fs_ == nullptr || slotIndex >= kCacheSlotCount || header == nullptr) {
        return false;
    }
    char path[48];
    indexPath(slotIndex, path, sizeof(path));
    fs::File file = fs_->open(path, FILE_READ);
    if (!file || file.read(header, library_cache::kIndexHeaderSize) !=
                     library_cache::kIndexHeaderSize) {
        if (file) {
            file.close();
        }
        return false;
    }
    file.close();
    const CacheSlot& slot = slots_[slotIndex];
    return library_cache::readLe32(header) == library_cache::kIndexMagic &&
           library_cache::readLe16(header + 4) ==
               library_cache::kIndexVersion &&
           library_cache::readLe16(header + 6) ==
               library_cache::kIndexHeaderSize &&
           library_cache::readLe32(header + 8) == slot.generation &&
           library_cache::readLe16(header + 12) == slot.entryCount &&
           library_cache::readLe16(header + 14) == slot.directoryCount &&
           library_cache::readLe16(header + 16) == slot.trackCount;
}

void MusicLibrary::indexPath(size_t slotIndex, char* output,
                             size_t capacity) const {
    std::snprintf(output, capacity, "%s/slot%u.idx", kCacheRoot,
                  static_cast<unsigned>(slotIndex));
}

void MusicLibrary::dataPath(size_t slotIndex, char* output,
                            size_t capacity) const {
    std::snprintf(output, capacity, "%s/slot%u.dat", kCacheRoot,
                  static_cast<unsigned>(slotIndex));
}

bool MusicLibrary::loadPage(uint16_t pageNumber) {
    if (currentSlot_ < 0) {
        return false;
    }
    CacheSlot& slot = slots_[currentSlot_];
    const size_t first = static_cast<size_t>(pageNumber) * kPageEntries;
    if (first >= slot.entryCount) {
        return false;
    }
    uint8_t header[library_cache::kIndexHeaderSize];
    if (!readIndexHeader(currentSlot_, header)) {
        return false;
    }

    Page* page = choosePage();
    if (page == nullptr) {
        return false;
    }
    const size_t count = std::min(kPageEntries,
                                  static_cast<size_t>(slot.entryCount) - first);
    char path[48];
    indexPath(currentSlot_, path, sizeof(path));
    fs::File file = fs_->open(path, FILE_READ);
    const size_t offset = library_cache::kIndexHeaderSize +
                          first * sizeof(uint32_t);
    if (!file || !file.seek(offset)) {
        if (file) {
            file.close();
        }
        return false;
    }
    uint8_t bytes[kPageEntries * sizeof(uint32_t)];
    const size_t byteCount = count * sizeof(uint32_t);
    if (file.read(bytes, byteCount) != byteCount) {
        file.close();
        return false;
    }
    file.close();

    *page = Page{};
    page->valid = true;
    page->slot = static_cast<uint8_t>(currentSlot_);
    page->count = static_cast<uint8_t>(count);
    page->pageNumber = pageNumber;
    page->generation = slot.generation;
    page->lastUse = ++useCounter_;
    for (size_t index = 0; index < count; ++index) {
        page->recordOffsets[index] =
            library_cache::readLe32(bytes + index * sizeof(uint32_t));
    }
    slot.lastUse = useCounter_;
    return true;
}

MusicLibrary::Page* MusicLibrary::findPage(size_t slotIndex,
                                           uint32_t generation,
                                           uint16_t pageNumber) {
    for (Page& page : pages_) {
        if (page.valid && page.slot == slotIndex &&
            page.generation == generation && page.pageNumber == pageNumber) {
            return &page;
        }
    }
    return nullptr;
}

MusicLibrary::Page* MusicLibrary::choosePage() {
    for (Page& page : pages_) {
        if (!page.valid) {
            return &page;
        }
    }
    Page* oldest = &pages_[0];
    for (Page& page : pages_) {
        if (page.lastUse < oldest->lastUse) {
            oldest = &page;
        }
    }
    return oldest;
}

bool MusicLibrary::pageOffsetAt(size_t entryIndex, uint32_t& output) {
    if (currentSlot_ < 0 || entryIndex >= entryCount()) {
        return false;
    }
    const uint16_t pageNumber =
        static_cast<uint16_t>(entryIndex / kPageEntries);
    Page* page = findPage(currentSlot_, slots_[currentSlot_].generation,
                          pageNumber);
    if (page == nullptr) {
        requestWindow(entryIndex);
        return false;
    }
    const size_t withinPage = entryIndex % kPageEntries;
    if (withinPage >= page->count) {
        recoverCurrentCacheCorruption();
        return false;
    }
    page->lastUse = ++useCounter_;
    output = page->recordOffsets[withinPage];
    return true;
}

bool MusicLibrary::slotStillValid(size_t slotIndex,
                                  uint32_t generation) const {
    return slotIndex < kCacheSlotCount && slots_[slotIndex].valid &&
           slots_[slotIndex].generation == generation;
}

bool MusicLibrary::pinSlot(size_t slotIndex, uint32_t generation) {
    if (!slotStillValid(slotIndex, generation) ||
        slots_[slotIndex].pinCount == UINT8_MAX) {
        return false;
    }
    ++slots_[slotIndex].pinCount;
    slots_[slotIndex].lastUse = ++useCounter_;
    return true;
}

void MusicLibrary::unpinSlot(size_t slotIndex, uint32_t generation) {
    if (slotIndex < kCacheSlotCount &&
        slots_[slotIndex].generation == generation &&
        slots_[slotIndex].pinCount != 0) {
        --slots_[slotIndex].pinCount;
        if (slots_[slotIndex].pinCount == 0) {
            const int newest = findCachedSlot(slots_[slotIndex].directory);
            if (newest >= 0 && static_cast<size_t>(newest) != slotIndex &&
                slots_[newest].generation > generation) {
                resetSlot(slotIndex);
            }
        }
    }
}

const char* libraryStateName(LibraryState state) {
    switch (state) {
        case LibraryState::Idle:
            return "IDLE";
        case LibraryState::Open:
            return "OPEN";
        case LibraryState::Scan:
            return "SCAN";
        case LibraryState::Sort:
            return "SORT";
        case LibraryState::Finalize:
            return "FINALIZE";
        case LibraryState::Ready:
            return "READY";
        case LibraryState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* libraryErrorName(LibraryError error) {
    switch (error) {
        case LibraryError::None:
            return "NONE";
        case LibraryError::InvalidArgument:
            return "INVALID_ARGUMENT";
        case LibraryError::RootUnavailable:
            return "ROOT_UNAVAILABLE";
        case LibraryError::NotReady:
            return "NOT_READY";
        case LibraryError::InvalidEntry:
            return "INVALID_ENTRY";
        case LibraryError::NotDirectory:
            return "NOT_DIRECTORY";
        case LibraryError::PathTooLong:
            return "PATH_TOO_LONG";
        case LibraryError::DirectoryTooLarge:
            return "DIRECTORY_TOO_LARGE";
        case LibraryError::QueueTooLarge:
            return "QUEUE_TOO_LARGE";
        case LibraryError::CacheBusy:
            return "CACHE_BUSY";
        case LibraryError::OutOfMemory:
            return "OUT_OF_MEMORY";
        case LibraryError::IoError:
            return "IO_ERROR";
        case LibraryError::CacheCorrupt:
            return "CACHE_CORRUPT";
    }
    return "UNKNOWN";
}

}  // namespace player
}  // namespace adv_walkman
