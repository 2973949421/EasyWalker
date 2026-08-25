#pragma once

#include <cstddef>
#include <cstdint>

#include "CoreTypes.h"
#include "TrackSource.h"

namespace adv_walkman {
namespace player {

struct PlaybackQueueSnapshotView {
    const uint16_t* order = nullptr;
    size_t orderCount = 0;
    size_t orderCursor = 0;
    bool shuffleEnabled = false;
    const uint16_t* history = nullptr;
    size_t historyCount = 0;
};

class PlaybackQueue {
  public:
    explicit PlaybackQueue(uint32_t randomSeed = 0x9E3779B9u);

    bool reset(TrackSource& source, size_t startIndex);
    void clear();

    bool empty() const;
    size_t count() const;
    bool hasCurrent() const;
    size_t currentSourceIndex() const;
    size_t orderCursor() const;
    bool currentPath(char* output, size_t outputCapacity) const;
    bool pathAtSourceIndex(size_t sourceIndex, char* output, size_t outputCapacity) const;
    TrackSource* source() const;

    bool advance(bool wrapAtEnd);
    bool previous();

    void setShuffleEnabled(bool enabled, bool beginNewQueueRound = false);
    bool shuffleEnabled() const;
    void setRandomSeed(uint32_t seed);

    PlaybackQueueSnapshotView snapshotView() const;
    bool restore(TrackSource& source, const PlaybackQueueSnapshotView& snapshot);

    const uint16_t* orderData() const;
    size_t orderCount() const;
    const uint16_t* historyData() const;
    size_t historyCount() const;

  private:
    static constexpr uint16_t kInvalidIndex = 0xFFFFu;

    void initializeSequentialOrder();
    void pushHistory(uint16_t sourceIndex);
    bool popHistory(uint16_t& sourceIndex);
    size_t findOrderPosition(uint16_t sourceIndex) const;
    void shuffleRange(size_t first, size_t end);
    void startNewShuffleRound(uint16_t justFinishedSourceIndex);
    uint32_t nextRandom();
    size_t randomBelow(size_t bound);
    bool validateSnapshot(const PlaybackQueueSnapshotView& snapshot, size_t sourceCount) const;

    TrackSource* source_ = nullptr;
    uint16_t count_ = 0;
    uint16_t cursor_ = kInvalidIndex;
    bool shuffleEnabled_ = false;
    uint32_t randomState_ = 0x9E3779B9u;
    uint16_t order_[kMaxQueueTracks]{};
    uint16_t history_[kPreviousHistoryCapacity]{};
    uint8_t historyCount_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
