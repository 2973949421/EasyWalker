#include "player/library/MetadataCache.h"

#include <algorithm>
#include <cstring>

namespace adv_walkman {
namespace player {

bool MetadataCache::lookup(const char* path, Mp3Metadata& output,
                           Mp3MetadataError* readyError) {
    Entry* entry = find(path);
    if (entry == nullptr) {
        return false;
    }
    entry->accessStamp = nextStamp();
    output = entry->metadata;
    if (readyError != nullptr) {
        *readyError = entry->readyError;
    }
    return true;
}

bool MetadataCache::put(const char* path, const Mp3Metadata& metadata,
                        Mp3MetadataError readyError) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    const size_t length = std::strlen(path);
    if (length > kMaxTrackPathBytes) {
        return false;
    }

    Entry* entry = find(path);
    if (entry == nullptr) {
        entry = selectInsertionEntry();
        if (!entry->occupied) {
            ++size_;
        }
    }
    std::memcpy(entry->path, path, length + 1);
    entry->metadata = metadata;
    entry->readyError = readyError;
    entry->accessStamp = nextStamp();
    entry->occupied = true;
    return true;
}

bool MetadataCache::erase(const char* path) {
    Entry* entry = find(path);
    if (entry == nullptr) {
        return false;
    }
    *entry = Entry{};
    --size_;
    return true;
}

void MetadataCache::clear() {
    for (Entry& entry : entries_) {
        entry = Entry{};
    }
    size_ = 0;
    clock_ = 0;
}

size_t MetadataCache::size() const {
    return size_;
}

MetadataCache::Entry* MetadataCache::find(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }
    for (Entry& entry : entries_) {
        if (entry.occupied && std::strcmp(entry.path, path) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

MetadataCache::Entry* MetadataCache::selectInsertionEntry() {
    for (Entry& entry : entries_) {
        if (!entry.occupied) {
            return &entry;
        }
    }
    return &*std::min_element(
        entries_, entries_ + kCapacity,
        [](const Entry& left, const Entry& right) {
            return left.accessStamp < right.accessStamp;
        });
}

uint32_t MetadataCache::nextStamp() {
    ++clock_;
    if (clock_ != 0) {
        return clock_;
    }

    // Overflow is extremely rare, but rebasing preserves LRU ordering without
    // letting a freshly used entry appear older than every existing entry.
    uint32_t oldStamps[kCapacity] = {};
    for (size_t index = 0; index < kCapacity; ++index) {
        oldStamps[index] = entries_[index].accessStamp;
    }
    uint32_t maximumRank = 0;
    for (size_t index = 0; index < kCapacity; ++index) {
        if (!entries_[index].occupied) {
            continue;
        }
        uint32_t rank = 1;
        for (size_t other = 0; other < kCapacity; ++other) {
            if (entries_[other].occupied &&
                oldStamps[other] < oldStamps[index]) {
                ++rank;
            }
        }
        entries_[index].accessStamp = rank;
        maximumRank = std::max(maximumRank, rank);
    }
    clock_ = maximumRank + 1;
    return clock_;
}

}  // namespace player
}  // namespace adv_walkman
