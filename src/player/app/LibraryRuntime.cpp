#include "player/app/LibraryRuntime.h"

#include <Arduino.h>

#include <cctype>
#include <cstring>

namespace adv_walkman {
namespace player {

LibraryRuntime::~LibraryRuntime() {
    shutdown();
}

bool LibraryRuntime::begin(fs::FS& fs, PlayerRuntime& player,
                           const char* recentSlotA,
                           const char* recentSlotB) {
    // LibraryRuntime owns pinned Queue sources. Rebinding a live instance
    // would invalidate PlayerController references, so its lifecycle is
    // deliberately one-shot. Construct a new runtime to bind another FS.
    if (fs_ != nullptr || player_ != nullptr) {
        return false;
    }
    fs_ = &fs;
    player_ = &player;
    if (!library_.begin(fs)) {
        shutdown();
        return false;
    }

    if (recent_.begin(fs, recentSlotA, recentSlotB)) {
        recentResult_ = recent_.load();
    } else {
        recentResult_ = RecentTracksResult::InvalidArgument;
    }
    recentLastTickMs_ = millis();
    return true;
}

void LibraryRuntime::shutdown() {
    metadataReader_.cancel();
    metadataCache_.clear();
    for (FolderQueueSource& source : queueSources_) {
        source.release();
    }
    library_.end();
    fs_ = nullptr;
    player_ = nullptr;
    activeQueueSource_ = -1;
    pendingSelection_ = false;
    pendingMetadataPath_ = false;
    metadataRequestActive_ = false;
    latestMetadataReady_ = false;
    latestMetadataStatus_ = Mp3MetadataStatus{};
    metadataPath_[0] = '\0';
    observedRecentPath_[0] = '\0';
    pendingRecentPath_[0] = '\0';
    recentPlayingMs_ = 0;
    recentRecorded_ = false;
    recentRecordPending_ = false;
    libraryQuotaLane_ = 0;
    backgroundLane_ = 0;
}

void LibraryRuntime::service() {
    if (player_ == nullptr || fs_ == nullptr) {
        return;
    }

    const uint32_t now = millis();
    // Playing-time observation is RAM-only and therefore safe every loop.
    // SD validation/publication is scheduled separately below.
    // A newly queued Recent record must remain observable for the rest of
    // this service call.  The storage lane may consume it from the next call
    // onward.  Besides making the lifecycle deterministic, this prevents a
    // same-loop track change from racing the publication boundary.
    const bool recentQueuedThisService = observeRecent(now);

    const bool backgroundPending =
        pendingSelection_ || metadataRequestActive_ ||
        recentStoragePending(now);
    if (library_.workPending() &&
        (!backgroundPending || libraryQuotaLane_ < 3)) {
        library_.service();
        libraryQuotaLane_ = backgroundPending
                                ? static_cast<uint8_t>(libraryQuotaLane_ + 1U)
                                : 0;
        return;
    }

    libraryQuotaLane_ = 0;
    if (!serviceBackgroundWork(now, recentQueuedThisService) &&
        library_.workPending()) {
        library_.service();
    }
}

MusicLibrary& LibraryRuntime::library() {
    return library_;
}

const MusicLibrary& LibraryRuntime::library() const {
    return library_;
}

LibraryResult LibraryRuntime::selectTrack(size_t entryIndex, bool autoplay) {
    if (player_ == nullptr || library_.state() != LibraryState::Ready) {
        return LibraryResult::Error;
    }
    const size_t length = std::strlen(library_.currentPath());
    if (length >= sizeof(pendingSelectionDirectory_)) {
        return LibraryResult::Error;
    }
    std::memcpy(pendingSelectionDirectory_, library_.currentPath(), length + 1);
    pendingSelectionEntry_ = entryIndex;
    pendingSelectionGeneration_ = library_.currentGeneration();
    pendingSelectionAutoplay_ = autoplay;
    pendingSelection_ = true;
    return tryPendingSelection();
}

bool LibraryRuntime::selectionPending() const {
    return pendingSelection_;
}

LibraryResult LibraryRuntime::tryPendingSelection() {
    if (!pendingSelection_ || player_ == nullptr) {
        return LibraryResult::Error;
    }
    if (std::strcmp(pendingSelectionDirectory_, library_.currentPath()) != 0 ||
        pendingSelectionGeneration_ != library_.currentGeneration() ||
        library_.state() == LibraryState::Error) {
        pendingSelection_ = false;
        return LibraryResult::Error;
    }
    if (library_.state() != LibraryState::Ready ||
        !player_->queueSourceReleaseSafe()) {
        return LibraryResult::Pending;
    }

    const int8_t nextSource = activeQueueSource_ == 0 ? 1 : 0;
    queueSources_[nextSource].release();
    size_t queueStart = 0;
    const LibraryResult prepared = library_.selectTrack(
        pendingSelectionEntry_, queueSources_[nextSource], queueStart);
    if (prepared != LibraryResult::Ok) {
        if (prepared == LibraryResult::Error) {
            pendingSelection_ = false;
        }
        return prepared;
    }

    const bool playbackReady = player_->replaceQueue(
        queueSources_[nextSource], queueStart, pendingSelectionAutoplay_);
    const int8_t previousSource = activeQueueSource_;
    activeQueueSource_ = nextSource;
    pendingSelection_ = false;
    if (previousSource >= 0 && previousSource != activeQueueSource_) {
        // No Player persistence job was active before replaceQueue(); the old
        // source is no longer referenced after the Controller switches queue.
        queueSources_[previousSource].release();
    }
    return playbackReady ? LibraryResult::Ok : LibraryResult::Error;
}

LibraryResult LibraryRuntime::requestMetadata(size_t entryIndex) {
    metadataReader_.cancel();
    metadataRequestActive_ = false;
    pendingMetadataPath_ = false;
    metadataFromEntry_ = true;
    metadataPath_[0] = '\0';
    if (library_.state() != LibraryState::Ready) {
        latestMetadataReady_ = false;
        latestMetadataStatus_ = Mp3MetadataStatus{};
        latestMetadataStatus_.state = Mp3MetadataState::Error;
        latestMetadataStatus_.error = Mp3MetadataError::InvalidArgument;
        return LibraryResult::Error;
    }
    const size_t length = std::strlen(library_.currentPath());
    if (length >= sizeof(pendingMetadataDirectory_)) {
        return LibraryResult::Error;
    }
    std::memcpy(pendingMetadataDirectory_, library_.currentPath(), length + 1);
    pendingMetadataEntry_ = entryIndex;
    pendingMetadataGeneration_ = library_.currentGeneration();
    pendingMetadataPath_ = true;
    metadataRequestActive_ = true;
    latestMetadataReady_ = false;
    latestMetadataStatus_ = Mp3MetadataStatus{};
    latestMetadataStatus_.state = Mp3MetadataState::Opening;
    metadataReader_.cancel();
    return tryPendingMetadata();
}

LibraryResult LibraryRuntime::tryPendingMetadata() {
    if (!pendingMetadataPath_ || fs_ == nullptr) {
        return LibraryResult::Error;
    }
    if (std::strcmp(pendingMetadataDirectory_, library_.currentPath()) != 0 ||
        pendingMetadataGeneration_ != library_.currentGeneration() ||
        library_.state() == LibraryState::Error) {
        metadataReader_.cancel();
        pendingMetadataPath_ = false;
        metadataRequestActive_ = false;
        latestMetadataReady_ = false;
        latestMetadataStatus_ = Mp3MetadataStatus{};
        latestMetadataStatus_.state = Mp3MetadataState::Error;
        latestMetadataStatus_.error = Mp3MetadataError::InvalidArgument;
        return LibraryResult::Error;
    }
    if (library_.state() != LibraryState::Ready) {
        return LibraryResult::Pending;
    }

    LibraryEntry entry;
    const LibraryResult entryResult =
        library_.entryAt(pendingMetadataEntry_, entry);
    if (entryResult != LibraryResult::Ok) {
        if (entryResult == LibraryResult::Error) {
            pendingMetadataPath_ = false;
            metadataRequestActive_ = false;
            latestMetadataStatus_.state = Mp3MetadataState::Error;
            latestMetadataStatus_.error = Mp3MetadataError::InvalidArgument;
        }
        return entryResult;
    }
    if (entry.type != LibraryEntryType::Track) {
        pendingMetadataPath_ = false;
        metadataRequestActive_ = false;
        latestMetadataStatus_.state = Mp3MetadataState::Error;
        latestMetadataStatus_.error = Mp3MetadataError::InvalidArgument;
        return LibraryResult::Error;
    }

    const LibraryResult pathResult = library_.entryPathAt(
        pendingMetadataEntry_, metadataPath_, sizeof(metadataPath_));
    if (pathResult != LibraryResult::Ok) {
        if (pathResult == LibraryResult::Error) {
            pendingMetadataPath_ = false;
            metadataRequestActive_ = false;
            latestMetadataStatus_ = Mp3MetadataStatus{};
            latestMetadataStatus_.state = Mp3MetadataState::Error;
            latestMetadataStatus_.error = Mp3MetadataError::InvalidArgument;
        }
        return pathResult;
    }
    pendingMetadataPath_ = false;

    return startMetadataReader();
}

bool LibraryRuntime::isMetadataPath(const char* path) {
    if (path == nullptr || std::strncmp(path, "/Music/", 7) != 0 ||
        std::strlen(path) > kMaxTrackPathBytes) {
        return false;
    }
    const char* component = path + 7;
    for (const char* cursor = component;; ++cursor) {
        if (*cursor == '\\' || (*cursor != '\0' &&
            static_cast<unsigned char>(*cursor) < 0x20)) {
            return false;
        }
        if (*cursor != '/' && *cursor != '\0') {
            continue;
        }
        const size_t length = static_cast<size_t>(cursor - component);
        if (length == 0 || (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (*cursor == '\0') {
            return length >= 4 && component[length - 4] == '.' &&
                   (component[length - 3] == 'm' || component[length - 3] == 'M') &&
                   (component[length - 2] == 'p' || component[length - 2] == 'P') &&
                   component[length - 1] == '3';
        }
        component = cursor + 1;
    }
}

LibraryResult LibraryRuntime::requestMetadataPath(const char* path) {
    if (fs_ == nullptr || !isMetadataPath(path)) {
        return LibraryResult::Error;
    }
    metadataReader_.cancel();
    metadataFromEntry_ = false;
    pendingMetadataPath_ = false;
    latestMetadataReady_ = false;
    latestMetadataStatus_ = Mp3MetadataStatus{};
    std::memcpy(metadataPath_, path, std::strlen(path) + 1);
    metadataRequestActive_ = true;
    return startMetadataReader();
}

LibraryResult LibraryRuntime::startMetadataReader() {
    Mp3MetadataError cachedWarning = Mp3MetadataError::None;
    if (metadataCache_.lookup(metadataPath_, latestMetadata_, &cachedWarning)) {
        metadataRequestActive_ = false;
        latestMetadataReady_ = true;
        latestMetadataStatus_ = Mp3MetadataStatus{};
        latestMetadataStatus_.state = Mp3MetadataState::Ready;
        latestMetadataStatus_.error = cachedWarning;
        return LibraryResult::Ok;
    }
    if (!metadataReader_.begin(*fs_, metadataPath_)) {
        metadataRequestActive_ = false;
        latestMetadataStatus_ = metadataReader_.status();
        return LibraryResult::Error;
    }
    latestMetadataStatus_ = metadataReader_.status();
    return LibraryResult::Pending;
}

void LibraryRuntime::serviceMetadata() {
    if (metadataRequestActive_ && metadataFromEntry_ &&
        (std::strcmp(pendingMetadataDirectory_, library_.currentPath()) != 0 ||
         pendingMetadataGeneration_ != library_.currentGeneration() ||
         library_.state() == LibraryState::Error)) {
        metadataReader_.cancel();
        pendingMetadataPath_ = false;
        metadataRequestActive_ = false;
        latestMetadataReady_ = false;
        latestMetadataStatus_ = Mp3MetadataStatus{};
        latestMetadataStatus_.state = Mp3MetadataState::Error;
        latestMetadataStatus_.error = Mp3MetadataError::InvalidArgument;
        return;
    }
    if (pendingMetadataPath_ && !metadataReader_.pending()) {
        tryPendingMetadata();
    }
    if (!metadataReader_.pending()) {
        return;
    }
    metadataReader_.service();
    latestMetadataStatus_ = metadataReader_.status();
    if (metadataReader_.ready()) {
        latestMetadata_ = metadataReader_.metadata();
        metadataCache_.put(metadataPath_, latestMetadata_,
                           latestMetadataStatus_.error);
        latestMetadataReady_ = true;
        metadataRequestActive_ = false;
    } else if (latestMetadataStatus_.state == Mp3MetadataState::Error) {
        metadataRequestActive_ = false;
    }
}

bool LibraryRuntime::metadata(Mp3Metadata& output) const {
    if (!latestMetadataReady_) {
        return false;
    }
    output = latestMetadata_;
    return true;
}

bool LibraryRuntime::metadataForPath(const char* path,
                                    Mp3Metadata& output) const {
    return path != nullptr && std::strcmp(path, metadataPath_) == 0 &&
           metadata(output);
}

const char* LibraryRuntime::metadataRequestPath() const {
    return metadataPath_;
}

Mp3MetadataStatus LibraryRuntime::metadataStatus() const {
    return latestMetadataStatus_;
}

size_t LibraryRuntime::metadataCacheSize() const {
    return metadataCache_.size();
}

RecentTracksResult LibraryRuntime::recentResult() const {
    return recentResult_;
}

size_t LibraryRuntime::recentCount() const {
    return recent_.loaded() ? recent_.count() : 0;
}

bool LibraryRuntime::recentPathAt(size_t index, char* output,
                                  size_t outputCapacity) const {
    return recent_.loaded() && recent_.pathAt(index, output, outputCapacity);
}

bool LibraryRuntime::recentRecordPending() const {
    return recentRecordPending_;
}

void LibraryRuntime::resetRecentObservation(const char* path, uint32_t now) {
    observedRecentPath_[0] = '\0';
    if (path != nullptr) {
        const size_t length = std::strlen(path);
        if (length <= kMaxTrackPathBytes) {
            std::memcpy(observedRecentPath_, path, length + 1);
        }
    }
    recentPlayingMs_ = 0;
    recentRecorded_ = false;
    recentLastTickMs_ = now;
}

bool LibraryRuntime::observeRecent(uint32_t now) {
    if (player_ == nullptr || !recent_.loaded()) {
        recentLastTickMs_ = now;
        return false;
    }

    char current[kTrackPathCapacity] = {};
    const PlayerSnapshot snapshot = player_->snapshot();
    const bool haveCurrent = snapshot.hasCurrent &&
                             player_->currentPath(current, sizeof(current));
    if (!haveCurrent || !samePathAsciiCaseInsensitive(
                            observedRecentPath_, current)) {
        resetRecentObservation(haveCurrent ? current : nullptr, now);
    }

    const uint32_t elapsed = now - recentLastTickMs_;
    recentLastTickMs_ = now;
    if (haveCurrent && snapshot.state == PlayerState::Playing &&
        !recentRecorded_) {
        const uint32_t room = UINT32_MAX - recentPlayingMs_;
        recentPlayingMs_ += elapsed > room ? room : elapsed;
        if (recentPlayingMs_ >= kRecentThresholdMs &&
            !recentRecordPending_ && observedRecentPath_[0] != '\0') {
            std::memcpy(pendingRecentPath_, observedRecentPath_,
                        std::strlen(observedRecentPath_) + 1);
            recentRecordPending_ = true;
            // Mark this observation as scheduled. The copied pending path is
            // independent of later track changes.
            recentRecorded_ = true;
            return true;
        }
    }
    return false;
}

bool LibraryRuntime::recentStoragePending(uint32_t now) const {
    if (player_ == nullptr || !recent_.loaded() ||
        !player_->persistenceIdle()) {
        return false;
    }
    return recent_.pending() || recentRecordPending_ ||
           (recent_.dirty() &&
            static_cast<int32_t>(now - recentNextRetryAtMs_) >= 0);
}

void LibraryRuntime::serviceRecentStorage(uint32_t now) {
    if (!recentStoragePending(now)) {
        return;
    }

    if (recent_.pending()) {
        recent_.service();
        recentResult_ = recent_.lastResult();
        recentNextRetryAtMs_ = now + 1000;
        return;
    }

    if (recentRecordPending_) {
        const RecentTracksResult result = recent_.record(pendingRecentPath_);
        recentResult_ = result;
        recentRecordPending_ = false;
        pendingRecentPath_[0] = '\0';
        recentNextRetryAtMs_ = now + 1000;
        return;
    }

    recentResult_ = recent_.retrySave();
    recentNextRetryAtMs_ = now + 1000;
}

bool LibraryRuntime::serviceBackgroundWork(uint32_t now,
                                           bool deferNewRecentRecord) {
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        const uint8_t lane = backgroundLane_;
        backgroundLane_ =
            static_cast<uint8_t>((backgroundLane_ + 1U) % 3U);
        if (lane == 0 && pendingSelection_) {
            tryPendingSelection();
            return true;
        }
        if (lane == 1 && metadataRequestActive_) {
            serviceMetadata();
            return true;
        }
        if (lane == 2 && !deferNewRecentRecord &&
            recentStoragePending(now)) {
            serviceRecentStorage(now);
            return true;
        }
    }
    return false;
}

bool LibraryRuntime::samePathAsciiCaseInsensitive(const char* left,
                                                  const char* right) {
    if (left == nullptr || right == nullptr) {
        return left == right;
    }
    while (*left != '\0' && *right != '\0') {
        const unsigned char l = static_cast<unsigned char>(*left++);
        const unsigned char r = static_cast<unsigned char>(*right++);
        const unsigned char foldedL = l < 0x80 ?
            static_cast<unsigned char>(std::tolower(l)) : l;
        const unsigned char foldedR = r < 0x80 ?
            static_cast<unsigned char>(std::tolower(r)) : r;
        if (foldedL != foldedR) {
            return false;
        }
    }
    return *left == *right;
}

}  // namespace player
}  // namespace adv_walkman
