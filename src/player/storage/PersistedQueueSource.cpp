#include "PersistedQueueSource.h"

#include <string.h>

namespace adv_walkman {
namespace player {

namespace {

constexpr uint32_t kRecordHeaderSize = 20;

uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

}  // namespace

size_t PersistedQueueSource::count() const {
    return count_;
}

bool PersistedQueueSource::pathAt(size_t index, char* output, size_t outputSize) const {
    if (!valid() || output == nullptr || index >= count_ ||
        outputSize <= pathLengths_[index]) {
        return false;
    }

    fs::File file = fs_->open(slotPath_, FILE_READ);
    if (!file || !file.seek(pathOffsets_[index])) {
        if (file) {
            file.close();
        }
        return false;
    }

    const size_t length = pathLengths_[index];
    const size_t bytesRead = file.read(
        reinterpret_cast<uint8_t*>(output), length);
    file.close();
    if (bytesRead != length) {
        output[0] = '\0';
        return false;
    }

    output[length] = '\0';
    return true;
}

uint32_t PersistedQueueSource::generation() const {
    return generation_;
}

bool PersistedQueueSource::valid() const {
    return fs_ != nullptr && slotPath_[0] != '\0' && generation_ != 0;
}

void PersistedQueueSource::clear() {
    fs_ = nullptr;
    slotPath_[0] = '\0';
    generation_ = 0;
    count_ = 0;
}

bool PersistedQueueSource::attach(
    fs::FS& fs,
    const char* slotPath,
    uint32_t generation) {
    clear();
    if (slotPath == nullptr || generation == 0 ||
        strlen(slotPath) >= sizeof(slotPath_)) {
        return false;
    }

    fs::File file = fs.open(slotPath, FILE_READ);
    if (!file || file.size() < kRecordHeaderSize + sizeof(uint16_t) ||
        !file.seek(kRecordHeaderSize)) {
        if (file) {
            file.close();
        }
        return false;
    }

    uint8_t countBytes[2] = {};
    if (file.read(countBytes, sizeof(countBytes)) != sizeof(countBytes)) {
        file.close();
        return false;
    }

    const uint16_t trackCount = readLe16(countBytes);
    if (trackCount > kPersistedQueueMaxTracks) {
        file.close();
        return false;
    }

    for (uint16_t index = 0; index < trackCount; ++index) {
        uint8_t lengthBytes[2] = {};
        if (file.read(lengthBytes, sizeof(lengthBytes)) != sizeof(lengthBytes)) {
            file.close();
            return false;
        }
        const uint16_t pathLength = readLe16(lengthBytes);
        if (pathLength == 0 || pathLength > kPersistedPathMaxBytes) {
            file.close();
            return false;
        }

        pathOffsets_[index] = file.position();
        pathLengths_[index] = pathLength;
        if (!file.seek(file.position() + pathLength)) {
            file.close();
            return false;
        }
    }

    file.close();
    fs_ = &fs;
    strncpy(slotPath_, slotPath, sizeof(slotPath_) - 1);
    slotPath_[sizeof(slotPath_) - 1] = '\0';
    generation_ = generation;
    count_ = trackCount;
    return true;
}

}  // namespace player
}  // namespace adv_walkman
