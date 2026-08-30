#include "PlaybackQueue.h"

#include <cstring>

namespace adv_walkman {
namespace player {

PlaybackQueue::PlaybackQueue(uint32_t randomSeed) {
    setRandomSeed(randomSeed);
}

bool PlaybackQueue::reset(TrackSource& source, size_t startIndex) {
    const size_t sourceCount = source.count();
    if (sourceCount > kMaxQueueTracks ||
        (sourceCount == 0 ? startIndex != 0 : startIndex >= sourceCount)) {
        return false;
    }

    source_ = &source;
    count_ = static_cast<uint16_t>(sourceCount);
    cursor_ = sourceCount == 0 ? kInvalidIndex : static_cast<uint16_t>(startIndex);
    shuffleEnabled_ = false;
    historyCount_ = 0;
    initializeSequentialOrder();
    return true;
}

void PlaybackQueue::clear() {
    source_ = nullptr;
    count_ = 0;
    cursor_ = kInvalidIndex;
    shuffleEnabled_ = false;
    historyCount_ = 0;
}

bool PlaybackQueue::empty() const {
    return count_ == 0;
}

size_t PlaybackQueue::count() const {
    return count_;
}

bool PlaybackQueue::hasCurrent() const {
    return source_ != nullptr && count_ != 0 && cursor_ < count_;
}

size_t PlaybackQueue::currentSourceIndex() const {
    return hasCurrent() ? order_[cursor_] : kMaxQueueTracks;
}

size_t PlaybackQueue::orderCursor() const {
    return hasCurrent() ? cursor_ : 0;
}

bool PlaybackQueue::currentPath(char* output, size_t outputCapacity) const {
    return hasCurrent() && pathAtSourceIndex(order_[cursor_], output, outputCapacity);
}

bool PlaybackQueue::pathAtSourceIndex(size_t sourceIndex,
                                      char* output,
                                      size_t outputCapacity) const {
    if (source_ == nullptr || sourceIndex >= count_ || output == nullptr || outputCapacity == 0) {
        return false;
    }

    output[0] = '\0';
    if (!source_->pathAt(sourceIndex, output, outputCapacity)) {
        output[0] = '\0';
        return false;
    }

    const void* terminator = std::memchr(output, '\0', outputCapacity);
    if (terminator == nullptr) {
        output[0] = '\0';
        return false;
    }

    const size_t pathBytes = static_cast<const char*>(terminator) - output;
    if (pathBytes == 0 || pathBytes > kMaxTrackPathBytes) {
        output[0] = '\0';
        return false;
    }
    return true;
}

TrackSource* PlaybackQueue::source() const {
    return source_;
}

bool PlaybackQueue::advance(bool wrapAtEnd) {
    if (!hasCurrent()) {
        return false;
    }

    const uint16_t previousSourceIndex = order_[cursor_];
    if (static_cast<size_t>(cursor_) + 1 < count_) {
        pushHistory(previousSourceIndex);
        ++cursor_;
        return true;
    }

    if (!wrapAtEnd) {
        return false;
    }

    pushHistory(previousSourceIndex);
    if (shuffleEnabled_) {
        startNewShuffleRound(previousSourceIndex);
    } else {
        cursor_ = 0;
    }
    return true;
}

bool PlaybackQueue::previous() {
    if (!hasCurrent()) {
        return false;
    }

    uint16_t previousSourceIndex = kInvalidIndex;
    if (!popHistory(previousSourceIndex)) {
        return false;
    }

    const size_t position = findOrderPosition(previousSourceIndex);
    if (position >= count_) {
        return false;
    }
    cursor_ = static_cast<uint16_t>(position);
    return true;
}

bool PlaybackQueue::retreatSequential(bool wrapAtStart) {
    if (!hasCurrent()) {
        return false;
    }
    if (cursor_ != 0) {
        --cursor_;
        return true;
    }
    if (!wrapAtStart) {
        return false;
    }
    cursor_ = static_cast<uint16_t>(count_ - 1);
    return true;
}

void PlaybackQueue::setShuffleEnabled(bool enabled, bool beginNewQueueRound) {
    if (enabled == shuffleEnabled_ || !hasCurrent()) {
        shuffleEnabled_ = enabled;
        return;
    }

    const uint16_t currentSourceIndex = order_[cursor_];
    if (enabled) {
        shuffleEnabled_ = true;
        if (beginNewQueueRound) {
            // A newly selected Queue starts its shuffle round at the chosen
            // track. Every other source index must remain eligible even when
            // the Library selected a track from the middle of the folder.
            initializeSequentialOrder();
            const uint16_t temporary = order_[0];
            order_[0] = currentSourceIndex;
            order_[currentSourceIndex] = temporary;
            cursor_ = 0;
            shuffleRange(1, count_);
            return;
        }
        shuffleRange(static_cast<size_t>(cursor_) + 1, count_);
        return;
    }

    shuffleEnabled_ = false;
    initializeSequentialOrder();
    cursor_ = currentSourceIndex;
}

bool PlaybackQueue::shuffleEnabled() const {
    return shuffleEnabled_;
}

void PlaybackQueue::setRandomSeed(uint32_t seed) {
    randomState_ = seed == 0 ? 0x9E3779B9u : seed;
}

PlaybackQueueSnapshotView PlaybackQueue::snapshotView() const {
    PlaybackQueueSnapshotView result;
    result.order = order_;
    result.orderCount = count_;
    result.orderCursor = orderCursor();
    result.shuffleEnabled = shuffleEnabled_;
    result.history = history_;
    result.historyCount = historyCount_;
    return result;
}

bool PlaybackQueue::restore(TrackSource& source, const PlaybackQueueSnapshotView& snapshot) {
    const size_t sourceCount = source.count();
    if (!validateSnapshot(snapshot, sourceCount)) {
        return false;
    }

    source_ = &source;
    count_ = static_cast<uint16_t>(sourceCount);
    shuffleEnabled_ = snapshot.shuffleEnabled;
    historyCount_ = static_cast<uint8_t>(snapshot.historyCount);

    if (count_ == 0) {
        cursor_ = kInvalidIndex;
        return true;
    }

    std::memcpy(order_, snapshot.order, count_ * sizeof(order_[0]));
    cursor_ = static_cast<uint16_t>(snapshot.orderCursor);
    if (historyCount_ != 0) {
        std::memcpy(history_, snapshot.history, historyCount_ * sizeof(history_[0]));
    }
    return true;
}

const uint16_t* PlaybackQueue::orderData() const {
    return order_;
}

size_t PlaybackQueue::orderCount() const {
    return count_;
}

const uint16_t* PlaybackQueue::historyData() const {
    return history_;
}

size_t PlaybackQueue::historyCount() const {
    return historyCount_;
}

void PlaybackQueue::initializeSequentialOrder() {
    for (uint16_t index = 0; index < count_; ++index) {
        order_[index] = index;
    }
}

void PlaybackQueue::pushHistory(uint16_t sourceIndex) {
    if (historyCount_ < kPreviousHistoryCapacity) {
        history_[historyCount_++] = sourceIndex;
        return;
    }

    std::memmove(history_, history_ + 1,
                 (kPreviousHistoryCapacity - 1) * sizeof(history_[0]));
    history_[kPreviousHistoryCapacity - 1] = sourceIndex;
}

bool PlaybackQueue::popHistory(uint16_t& sourceIndex) {
    if (historyCount_ == 0) {
        return false;
    }
    sourceIndex = history_[--historyCount_];
    return true;
}

size_t PlaybackQueue::findOrderPosition(uint16_t sourceIndex) const {
    for (size_t position = 0; position < count_; ++position) {
        if (order_[position] == sourceIndex) {
            return position;
        }
    }
    return count_;
}

void PlaybackQueue::shuffleRange(size_t first, size_t end) {
    if (first >= end) {
        return;
    }

    for (size_t remaining = end - first; remaining > 1; --remaining) {
        const size_t chosen = randomBelow(remaining);
        const size_t left = first + remaining - 1;
        const size_t right = first + chosen;
        const uint16_t temporary = order_[left];
        order_[left] = order_[right];
        order_[right] = temporary;
    }
}

void PlaybackQueue::startNewShuffleRound(uint16_t justFinishedSourceIndex) {
    initializeSequentialOrder();
    shuffleRange(0, count_);
    cursor_ = 0;

    if (count_ > 1 && order_[0] == justFinishedSourceIndex) {
        const size_t replacement = 1 + randomBelow(count_ - 1);
        const uint16_t temporary = order_[0];
        order_[0] = order_[replacement];
        order_[replacement] = temporary;
    }
}

uint32_t PlaybackQueue::nextRandom() {
    uint32_t value = randomState_;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    randomState_ = value == 0 ? 0x9E3779B9u : value;
    return randomState_;
}

size_t PlaybackQueue::randomBelow(size_t bound) {
    if (bound <= 1) {
        return 0;
    }
    return static_cast<size_t>((static_cast<uint64_t>(nextRandom()) * bound) >> 32);
}

bool PlaybackQueue::validateSnapshot(const PlaybackQueueSnapshotView& snapshot,
                                     size_t sourceCount) const {
    if (sourceCount > kMaxQueueTracks || snapshot.orderCount != sourceCount ||
        snapshot.historyCount > kPreviousHistoryCapacity) {
        return false;
    }

    if (sourceCount == 0) {
        return snapshot.orderCursor == 0 && snapshot.historyCount == 0;
    }

    if (snapshot.order == nullptr || snapshot.orderCursor >= sourceCount ||
        (snapshot.historyCount != 0 && snapshot.history == nullptr)) {
        return false;
    }

    uint8_t seen[(kMaxQueueTracks + 7) / 8]{};
    for (size_t position = 0; position < sourceCount; ++position) {
        const size_t sourceIndex = snapshot.order[position];
        if (sourceIndex >= sourceCount) {
            return false;
        }
        const uint8_t mask = static_cast<uint8_t>(1u << (sourceIndex & 7u));
        uint8_t& byte = seen[sourceIndex >> 3u];
        if ((byte & mask) != 0) {
            return false;
        }
        byte = static_cast<uint8_t>(byte | mask);
    }

    for (size_t index = 0; index < snapshot.historyCount; ++index) {
        if (snapshot.history[index] >= sourceCount) {
            return false;
        }
    }
    return true;
}

}  // namespace player
}  // namespace adv_walkman
