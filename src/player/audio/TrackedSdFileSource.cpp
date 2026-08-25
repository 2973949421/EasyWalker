#include "player/audio/TrackedSdFileSource.h"

#include <algorithm>

namespace adv_walkman {
namespace player {

TrackedSdFileSource::~TrackedSdFileSource() {
    close();
}

bool TrackedSdFileSource::open(const char* filename) {
    close();
    file_ = SD.open(filename, FILE_READ);
    if (!file_) {
        return false;
    }
    physicalSize_ = static_cast<uint32_t>(file_.size());
    readLimit_ = physicalSize_;
    bytesRead_ = 0;
    eofObserved_ = false;
    readError_ = false;
    return true;
}

uint32_t TrackedSdFileSource::read(void* data, uint32_t len) {
    if (len == 0) {
        return 0;
    }
    if (!file_ || data == nullptr) {
        readError_ = true;
        return 0;
    }

    const uint32_t position = static_cast<uint32_t>(file_.position());
    if (position >= readLimit_) {
        eofObserved_ = true;
        return 0;
    }

    const uint32_t allowed = std::min(len, readLimit_ - position);
    const size_t received =
        file_.read(static_cast<uint8_t*>(data), static_cast<size_t>(allowed));
    if (received > allowed) {
        readError_ = true;
        return 0;
    }
    if (received == 0) {
        const uint32_t current = static_cast<uint32_t>(file_.position());
        if (current >= readLimit_) {
            eofObserved_ = true;
        } else {
            readError_ = true;
        }
        return 0;
    }

    bytesRead_ += received;
    return static_cast<uint32_t>(received);
}

uint32_t TrackedSdFileSource::readNonBlock(void* data, uint32_t len) {
    return read(data, len);
}

bool TrackedSdFileSource::seek(int32_t pos, int dir) {
    if (!file_) {
        readError_ = true;
        return false;
    }

    int64_t target = 0;
    switch (dir) {
        case SEEK_SET:
            target = pos;
            break;
        case SEEK_CUR:
            target = static_cast<int64_t>(file_.position()) + pos;
            break;
        case SEEK_END:
            target = static_cast<int64_t>(readLimit_) + pos;
            break;
        default:
            return false;
    }
    if (target < 0 || target > readLimit_) {
        return false;
    }
    if (!file_.seek(static_cast<uint32_t>(target), SeekSet)) {
        return false;
    }
    eofObserved_ = false;
    readError_ = false;
    return true;
}

bool TrackedSdFileSource::close() {
    if (file_) {
        file_.close();
    }
    return true;
}

bool TrackedSdFileSource::isOpen() {
    return static_cast<bool>(file_);
}

uint32_t TrackedSdFileSource::getSize() {
    return readLimit_;
}

uint32_t TrackedSdFileSource::getPos() {
    return file_ ? static_cast<uint32_t>(file_.position()) : 0;
}

void TrackedSdFileSource::setReadLimit(uint32_t exclusiveEnd) {
    readLimit_ = std::min(exclusiveEnd, physicalSize_);
}

bool TrackedSdFileSource::eofObserved() const {
    return eofObserved_;
}

bool TrackedSdFileSource::readError() const {
    return readError_;
}

uint64_t TrackedSdFileSource::bytesRead() const {
    return bytesRead_;
}

}  // namespace player
}  // namespace adv_walkman
