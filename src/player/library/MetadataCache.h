#pragma once

#include <cstddef>
#include <cstdint>

#include "Mp3MetadataReader.h"

namespace adv_walkman {
namespace player {

class MetadataCache final {
  public:
    static constexpr size_t kCapacity = 12;

    // Paths are exact, normalized UTF-8 keys supplied by MusicLibrary.
    // lookup() promotes a hit to most-recently-used. readyError preserves the
    // non-fatal warning carried by a Ready reader result across cache hits.
    bool lookup(const char* path, Mp3Metadata& output,
                Mp3MetadataError* readyError = nullptr);
    bool put(const char* path, const Mp3Metadata& metadata,
             Mp3MetadataError readyError = Mp3MetadataError::None);
    bool erase(const char* path);
    void clear();

    size_t size() const;
    static constexpr size_t capacity() { return kCapacity; }

  private:
    struct Entry {
        char path[kTrackPathCapacity] = {};
        Mp3Metadata metadata;
        Mp3MetadataError readyError = Mp3MetadataError::None;
        uint32_t accessStamp = 0;
        bool occupied = false;
    };

    Entry* find(const char* path);
    Entry* selectInsertionEntry();
    uint32_t nextStamp();

    Entry entries_[kCapacity];
    size_t size_ = 0;
    uint32_t clock_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
