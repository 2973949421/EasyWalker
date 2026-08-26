#include "player/p2gate/P2DeviceTestRunner.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace adv_walkman {
namespace player {
namespace {

constexpr const char* kFixtureRoot = "/Music/ADVWalkmanP2Test";
constexpr const char* kFixtureMarker =
    "/Music/ADVWalkmanP2Test/.adv-walkman-p2-fixture.json";
constexpr const char* kFixtureManifest =
    "/Music/ADVWalkmanP2Test/manifest.json";
constexpr const char* kPlaybackPath =
    "/Music/ADVWalkmanP2Test/playback.MP3";
constexpr const char* kRecentSlotA =
    "/ADVWalkman/test/p2-state/recent-a.bin";
constexpr const char* kRecentSlotB =
    "/ADVWalkman/test/p2-state/recent-b.bin";
constexpr const char* kMissingRecentPath =
    "/ADVWalkman/test/p2-state/missing.mp3";

constexpr const char* kLogPaths[] = {
    "/ADVWalkman/logs/p2-01-last.txt",
    "/ADVWalkman/logs/p2-02-last.txt",
    "/ADVWalkman/logs/p2-03-last.txt",
    "/ADVWalkman/logs/p2-04-last.txt",
};

constexpr const char* kFixtureEntries[] = {
    "Album 2", "Album 10", "Empty", "Large", "Metadata", "中文目录",
    "playback.MP3", "song 2.mp3", "song 10.mp3",
};

constexpr LibraryEntryType kFixtureEntryTypes[] = {
    LibraryEntryType::Directory, LibraryEntryType::Directory,
    LibraryEntryType::Directory, LibraryEntryType::Directory,
    LibraryEntryType::Directory, LibraryEntryType::Directory,
    LibraryEntryType::Track, LibraryEntryType::Track,
    LibraryEntryType::Track,
};

constexpr const char* kCacheVisitDirectories[] = {
    "Album 2", "Album 10", "Empty", "Metadata", "中文目录",
};

struct MetadataCase {
    const char* filename;
    const char* title;
    const char* artist;
    const char* album;
    const char* track;
    Mp3MetadataError warning;
    bool titlePrefixOnly;
    bool titleFallback;
    bool titleTruncated;
};

constexpr MetadataCase kMetadataCases[] = {
    {"id3-v23-utf16.mp3", "星の歌", "测试歌手", "蓝色专辑", "2/12",
     Mp3MetadataError::None, false, false, false},
    {"id3-v24-utf8.mp3", "夜の図書館", "東京テスト", "四季", "10",
     Mp3MetadataError::None, false, false, false},
    {"id3-v23-latin1.mp3", "Cafe ÿ Track", "Latin Artist", "Latin Album",
     "1/9", Mp3MetadataError::None, false, false, false},
    {"id3-v24-utf16be.mp3", "夜空BE", "北京", "星河", nullptr,
     Mp3MetadataError::None, false, false, false},
    {"id3-v23-unsync.mp3", "Unsync ", "Unsync Artist", "Unsync Album",
     "5/8", Mp3MetadataError::None, true, false, true},
    {"id3-v23-extended.mp3", "Extended 23", nullptr, nullptr, nullptr,
     Mp3MetadataError::None, false, false, false},
    {"id3-v24-extended.mp3", "Extended 24", nullptr, nullptr, nullptr,
     Mp3MetadataError::None, false, false, false},
    {"id3-v24-long-title.mp3", "长", "Long Field Artist",
     "Long Field Album", "11", Mp3MetadataError::None, true, false, true},
    {"无标签中文歌曲.mp3", "无标签中文歌曲", nullptr, nullptr, nullptr,
     Mp3MetadataError::None, false, true, false},
    {"损坏标签回退.mp3", "损坏标签回退", nullptr, nullptr, nullptr,
     Mp3MetadataError::MalformedTag, false, true, false},
};

constexpr size_t kFixtureEntryCount =
    sizeof(kFixtureEntries) / sizeof(kFixtureEntries[0]);
constexpr size_t kCacheVisitCount =
    sizeof(kCacheVisitDirectories) / sizeof(kCacheVisitDirectories[0]);
constexpr size_t kMetadataCaseCount =
    sizeof(kMetadataCases) / sizeof(kMetadataCases[0]);
constexpr uint32_t kDirectoryTimeoutMs = 45000;
constexpr uint32_t kPlaybackTimeoutMs = 12000;
constexpr uint32_t kMetadataTimeoutMs = 10000;
constexpr uint32_t kRecentTimeoutMs = 15000;

bool pathEquals(const char* left, const char* right) {
    return left != nullptr && right != nullptr && std::strcmp(left, right) == 0;
}

void largeTrackPath(size_t index, char* output, size_t capacity) {
    std::snprintf(output, capacity,
                  "/Music/ADVWalkmanP2Test/Large/track-%04u.mp3",
                  static_cast<unsigned>(index));
}

}  // namespace

void P2DeviceTestRunner::begin(PlayerRuntime& player,
                               LibraryRuntime& libraryRuntime) {
    player_ = &player;
    libraryRuntime_ = &libraryRuntime;
    SD.mkdir("/ADVWalkman");
    SD.mkdir("/ADVWalkman/logs");
    SD.mkdir("/ADVWalkman/test");
    SD.mkdir("/ADVWalkman/test/p2-state");
}

bool P2DeviceTestRunner::start() {
    if (player_ == nullptr || libraryRuntime_ == nullptr ||
        phase_ != Phase::Idle) {
        return false;
    }

    resetRun();
    phase_ = Phase::OpenRoot;
    phaseStartedAtMs_ = millis();
    render(true);
    for (size_t index = 0; index < 4; ++index) {
        if (!writeLog(index, "RUNNING", "waiting_for_gate")) {
            fail("running_log_write_failed");
            return false;
        }
    }
    if (!verifyFixtureMarker()) {
        fail("fixture_marker_or_manifest_invalid");
        return false;
    }

    const LibraryResult result = libraryRuntime_->library().openRoot();
    if (result == LibraryResult::Error) {
        fail("music_root_open_failed");
        return false;
    }
    return true;
}

void P2DeviceTestRunner::service() {
    if (phase_ == Phase::Idle || phase_ == Phase::Passed ||
        phase_ == Phase::Failed || player_ == nullptr ||
        libraryRuntime_ == nullptr) {
        return;
    }
    const uint32_t now = millis();
    monitorPlayback(now);
    if (phase_ != Phase::Failed) {
        servicePhase(now);
    }
}

void P2DeviceTestRunner::recordLoopTimings(
    uint32_t inputUpdateUs, uint32_t playerServiceStartGapUs,
    bool speakerChannelPlayingAtServiceStart,
    uint32_t playerRuntimeServiceUs, uint32_t libraryRuntimeServiceUs,
    uint32_t preGateLoopBodyUs) {
    // Startup includes four log transactions and fixture verification before
    // audio exists. Exclude that setup work so these maxima only describe the
    // playback-under-load portion exercised by P2.
    if (!active() || !playbackSelected_) return;
    inputUpdateMaxUs_ = std::max(inputUpdateMaxUs_, inputUpdateUs);
    playerRuntimeServiceMaxUs_ =
        std::max(playerRuntimeServiceMaxUs_, playerRuntimeServiceUs);
    libraryRuntimeServiceMaxUs_ =
        std::max(libraryRuntimeServiceMaxUs_, libraryRuntimeServiceUs);
    loopBodyMaxUs_ = std::max(loopBodyMaxUs_, preGateLoopBodyUs);

    // The interval ending at this Player service start belongs primarily to
    // the previous completed loop. Stage the current loop for completion in
    // recordGateServiceTiming(), but freeze the previous loop when this gap is
    // the new maximum so phase transitions cannot misattribute the delay.
    pendingLoopPlayerRuntimeUs_ = playerRuntimeServiceUs;
    pendingLoopLibraryRuntimeUs_ = libraryRuntimeServiceUs;
    std::strncpy(pendingLoopPhaseFrom_, phaseName(),
                 sizeof(pendingLoopPhaseFrom_) - 1);
    pendingLoopPhaseFrom_[sizeof(pendingLoopPhaseFrom_) - 1] = '\0';
    pendingLoopTimingValid_ = true;

    const PlayerSnapshot snapshot = player_->snapshot();
    if (snapshot.state != PlayerState::Playing) {
        playerServiceGapArmed_ = false;
        return;
    }
    if (!playerServiceGapArmed_) {
        playerServiceGapArmed_ = true;
        return;
    }
    // The first Playing loop after a planned Pause/Resume is allowed to refill
    // an empty channel. Only count emptiness during an already continuous
    // Playing interval.
    if (!speakerChannelPlayingAtServiceStart) {
        ++speakerChannelEmptyAtServiceStart_;
    }
    if (playerServiceStartGapUs > playerServiceStartGapMaxUs_) {
        playerServiceStartGapMaxUs_ = playerServiceStartGapUs;
        playerGapCurrentInputUs_ = inputUpdateUs;
        playerGapPreviousPlayerRuntimeUs_ =
            lastLoopTimingValid_ ? lastLoopPlayerRuntimeUs_ : 0;
        playerGapPreviousLibraryRuntimeUs_ =
            lastLoopTimingValid_ ? lastLoopLibraryRuntimeUs_ : 0;
        playerGapPreviousGateUs_ =
            lastLoopTimingValid_ ? lastLoopGateUs_ : 0;
        playerGapPreviousLoopBodyUs_ =
            lastLoopTimingValid_ ? lastLoopBodyUs_ : 0;
        const char* fromPhase =
            lastLoopTimingValid_ ? lastLoopPhaseFrom_ : "none";
        const char* toPhase =
            lastLoopTimingValid_ ? lastLoopPhaseTo_ : phaseName();
        std::strncpy(playerServiceStartGapMaxFromPhase_, fromPhase,
                     sizeof(playerServiceStartGapMaxFromPhase_) - 1);
        playerServiceStartGapMaxFromPhase_[
            sizeof(playerServiceStartGapMaxFromPhase_) - 1] = '\0';
        std::strncpy(playerServiceStartGapMaxToPhase_, toPhase,
                     sizeof(playerServiceStartGapMaxToPhase_) - 1);
        playerServiceStartGapMaxToPhase_[
            sizeof(playerServiceStartGapMaxToPhase_) - 1] = '\0';
    }
    if (playerServiceStartGapUs > 100000U) {
        ++playerServiceStartGapOver100Ms_;
    }
}

void P2DeviceTestRunner::recordGateServiceTiming(uint32_t gateServiceUs,
                                                 uint32_t loopBodyUs) {
    if (!active() || !playbackSelected_) return;
    gateServiceMaxUs_ = std::max(gateServiceMaxUs_, gateServiceUs);
    loopBodyMaxUs_ = std::max(loopBodyMaxUs_, loopBodyUs);
    if (!pendingLoopTimingValid_) return;

    lastLoopPlayerRuntimeUs_ = pendingLoopPlayerRuntimeUs_;
    lastLoopLibraryRuntimeUs_ = pendingLoopLibraryRuntimeUs_;
    lastLoopGateUs_ = gateServiceUs;
    lastLoopBodyUs_ = loopBodyUs;
    std::strncpy(lastLoopPhaseFrom_, pendingLoopPhaseFrom_,
                 sizeof(lastLoopPhaseFrom_) - 1);
    lastLoopPhaseFrom_[sizeof(lastLoopPhaseFrom_) - 1] = '\0';
    std::strncpy(lastLoopPhaseTo_, phaseName(),
                 sizeof(lastLoopPhaseTo_) - 1);
    lastLoopPhaseTo_[sizeof(lastLoopPhaseTo_) - 1] = '\0';
    lastLoopTimingValid_ = true;
    pendingLoopTimingValid_ = false;
}

bool P2DeviceTestRunner::active() const {
    return phase_ != Phase::Idle && phase_ != Phase::Passed &&
           phase_ != Phase::Failed;
}

bool P2DeviceTestRunner::ownsDisplay() const {
    return phase_ != Phase::Idle;
}

const char* P2DeviceTestRunner::phaseName() const {
    switch (phase_) {
        case Phase::Idle: return "IDLE";
        case Phase::OpenRoot: return "P2-01 OPEN /Music";
        case Phase::FindFixture: return "P2-01 FIND FIXTURE";
        case Phase::FixtureReady: return "P2-01 SCAN FIXTURE";
        case Phase::ValidateFixture: return "P2-01 SORT/FILTER";
        case Phase::SelectPlayback: return "P2-01 BUILD QUEUE";
        case Phase::WaitPlayback: return "P2-01 START AUDIO";
        case Phase::RecentPrePause: return "P2-04 RECENT BEFORE 5S";
        case Phase::RecentPaused: return "P2-04 PAUSE EXCLUDED";
        case Phase::RecentResumeWait: return "P2-04 PLAYING 5S";
        case Phase::FindChinese: return "P2-01 CHINESE PATH";
        case Phase::ChineseReady: return "P2-01 LEVEL 1";
        case Phase::FindJapanese: return "P2-01 JAPANESE PATH";
        case Phase::JapaneseReady: return "P2-01 LEVEL 2";
        case Phase::FindLevelThree: return "P2-01 LEVEL 3";
        case Phase::LevelThreeReady: return "P2-01 DEEP SCAN";
        case Phase::ValidateDeepTrack: return "P2-01 DEEP TRACK";
        case Phase::ReturnFromDeep: return "P2-01 QUEUE PIN";
        case Phase::FindLarge: return "P2-02 OPEN LARGE";
        case Phase::LargeReady: return "P2-02 LAZY SCAN";
        case Phase::ValidateLarge: return "P2-02 1000/PAGES";
        case Phase::ReturnFromLarge: return "P2-02 RETURN";
        case Phase::CacheVisits: return "P2-02 LRU/PIN";
        case Phase::FindMetadata: return "P2-03 OPEN METADATA";
        case Phase::MetadataReady: return "P2-03 READY";
        case Phase::ReadMetadata: return "P2-03 ID3/UTF";
        case Phase::FormalMetadataRequest: return "P2-03 RUNTIME REQUEST";
        case Phase::FormalMetadataWait: return "P2-03 RUNTIME WAIT";
        case Phase::ReturnFromMetadata: return "P2-03 CACHE";
        case Phase::WaitAutoRecent: return "P2-04 FIVE SECOND";
        case Phase::RecentAba: return "P2-04 A-B-A";
        case Phase::RecentBulk: return "P2-04 LIMIT 32";
        case Phase::RecentMissing: return "P2-04 MISSING";
        case Phase::RecentReload: return "P2-04 RELOAD";
        case Phase::RecentReloadClean: return "P2-04 CRC CLEAN";
        case Phase::Passed: return "PASS";
        case Phase::Failed: return "FAIL";
    }
    return "UNKNOWN";
}

void P2DeviceTestRunner::resetRun() {
    player_->setPersistenceSuspended(true);
    player_->stop();
    player_->setShuffleEnabled(false);
    player_->setRepeatMode(RepeatMode::One);
    player_->controller().resetDiagnostics();
    libraryRuntime_->library().clearStats();
    metadataReader_.cancel();
    metadataCache_.clear();

    runStartedAtMs_ = millis();
    initialHeap_ = ESP.getFreeHeap();
    minimumHeap_ = initialHeap_;
    playbackBackpressureStart_ = 0;
    playbackAudioErrorsStart_ = 0;
    metadataServiceMaxUs_ = 0;
    metadataBytesRead_ = 0;
    metadataCasesPassed_ = 0;
    speakerSilentSinceMs_ = 0;
    speakerStarvationCount_ = 0;
    speakerChannelEmptyAtServiceStart_ = 0;
    unexpectedPlaybackStateOver100Ms_ = 0;
    playerServiceStartGapMaxUs_ = 0;
    playerServiceStartGapOver100Ms_ = 0;
    inputUpdateMaxUs_ = 0;
    playerRuntimeServiceMaxUs_ = 0;
    libraryRuntimeServiceMaxUs_ = 0;
    gateServiceMaxUs_ = 0;
    loopBodyMaxUs_ = 0;
    pendingLoopPlayerRuntimeUs_ = 0;
    pendingLoopLibraryRuntimeUs_ = 0;
    lastLoopPlayerRuntimeUs_ = 0;
    lastLoopLibraryRuntimeUs_ = 0;
    lastLoopGateUs_ = 0;
    lastLoopBodyUs_ = 0;
    playerGapPreviousPlayerRuntimeUs_ = 0;
    playerGapPreviousLibraryRuntimeUs_ = 0;
    playerGapPreviousGateUs_ = 0;
    playerGapPreviousLoopBodyUs_ = 0;
    playerGapCurrentInputUs_ = 0;
    unexpectedPlaybackStateSinceMs_ = 0;
    speakerStarvationReported_ = false;
    playerServiceGapArmed_ = false;
    pendingLoopTimingValid_ = false;
    lastLoopTimingValid_ = false;
    playbackSelected_ = false;
    playbackPath_[0] = '\0';
    std::strcpy(pendingLoopPhaseFrom_, "none");
    std::strcpy(lastLoopPhaseFrom_, "none");
    std::strcpy(lastLoopPhaseTo_, "none");
    std::strcpy(playerServiceStartGapMaxFromPhase_, "none");
    std::strcpy(playerServiceStartGapMaxToPhase_, "none");
    std::strcpy(manifestSha256_, "unverified");
    std::strcpy(failure_, "none");

    searchActive_ = false;
    validationIndex_ = 0;
    playbackEntryIndex_ = 0;
    deepReturnCount_ = 0;
    largeIndex_ = 0;
    cacheVisitIndex_ = 0;
    cacheVisitInside_ = false;
    metadataCaseIndex_ = 0;
    metadataCaseStarted_ = false;
    recentSequenceIndex_ = 0;
    recentBulkIndex_ = 1;
    recentMissingStep_ = 0;
    recentWriteStarted_ = false;
    finalPlayerSnapshot_ = PlayerSnapshot{};
    finalPlayerSnapshotValid_ = false;
    autoRecentTimingPassed_ = false;
    for (bool& passed : taskPassed_) {
        passed = false;
    }
    std::memset(taskDetail_, 0, sizeof(taskDetail_));
    SD.remove(kRecentSlotA);
    SD.remove(kRecentSlotB);
    SD.remove(kMissingRecentPath);
}

void P2DeviceTestRunner::servicePhase(uint32_t now) {
    MusicLibrary& library = libraryRuntime_->library();
    size_t found = 0;
    SearchResult search = SearchResult::Pending;

    switch (phase_) {
        case Phase::OpenRoot:
            if (!waitForReady()) {
                return;
            }
            if (!pathEquals(library.currentPath(), MusicLibrary::kMusicRoot) ||
                library.parent() != LibraryResult::Ok ||
                !pathEquals(library.currentPath(), MusicLibrary::kMusicRoot)) {
                fail("music_root_escape_guard_failed");
                return;
            }
            enter(Phase::FindFixture);
            return;

        case Phase::FindFixture:
            search = findEntryStep("ADVWalkmanP2Test",
                                   LibraryEntryType::Directory, found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found) {
                fail("fixture_directory_missing");
                return;
            }
            if (library.enter(found) == LibraryResult::Error) {
                fail("fixture_directory_enter_failed");
                return;
            }
            enter(Phase::FixtureReady);
            return;

        case Phase::FixtureReady:
            if (!waitForReady()) return;
            if (!pathEquals(library.currentPath(), kFixtureRoot) ||
                library.entryCount() != kFixtureEntryCount ||
                library.directoryCount() != 6 || library.trackCount() != 3) {
                fail("fixture_filter_or_count_mismatch");
                return;
            }
            validationIndex_ = 0;
            enter(Phase::ValidateFixture);
            return;

        case Phase::ValidateFixture: {
            if (validationIndex_ >= kFixtureEntryCount) {
                playbackEntryIndex_ = 6;
                enter(Phase::SelectPlayback);
                return;
            }
            LibraryEntry entry;
            const LibraryResult result =
                library.entryAt(validationIndex_, entry);
            if (result == LibraryResult::Pending) return;
            if (result != LibraryResult::Ok ||
                entry.type != kFixtureEntryTypes[validationIndex_] ||
                std::strcmp(entry.name,
                            kFixtureEntries[validationIndex_]) != 0) {
                fail("fixture_natural_sort_mismatch");
                return;
            }
            ++validationIndex_;
            return;
        }

        case Phase::SelectPlayback: {
            const LibraryResult result =
                libraryRuntime_->selectTrack(playbackEntryIndex_, true);
            if (result == LibraryResult::Error) {
                fail("current_folder_queue_failed");
                return;
            }
            enter(Phase::WaitPlayback);
            return;
        }

        case Phase::WaitPlayback: {
            const PlayerSnapshot snapshot = player_->snapshot();
            if (snapshot.state == PlayerState::Error) {
                fail("fixture_playback_error");
                return;
            }
            if (libraryRuntime_->selectionPending() ||
                snapshot.state != PlayerState::Playing ||
                snapshot.sampleRateHz == 0) {
                if (now - phaseStartedAtMs_ > kPlaybackTimeoutMs) {
                    fail("fixture_playback_timeout");
                }
                return;
            }
            char path[kTrackPathCapacity] = {};
            if (!player_->currentPath(path, sizeof(path)) ||
                !pathEquals(path, kPlaybackPath) || snapshot.queueCount != 3 ||
                snapshot.currentIndex != 0 || snapshot.sampleRateHz != 44100) {
                fail("current_folder_queue_semantics_failed");
                return;
            }
            std::strcpy(playbackPath_, path);
            playbackSelected_ = true;
            playbackBackpressureStart_ = snapshot.backpressureEvents;
            playbackAudioErrorsStart_ = snapshot.audioErrorEvents;
            playerServiceGapArmed_ = false;
            enter(Phase::RecentPrePause);
            return;
        }

        case Phase::RecentPrePause:
            if (libraryRuntime_->recentCount() != 0) {
                fail("recent_recorded_before_five_seconds");
                return;
            }
            if (!player_->pause()) {
                fail("recent_pause_failed");
                return;
            }
            enter(Phase::RecentPaused);
            return;

        case Phase::RecentPaused:
            if (now - phaseStartedAtMs_ < 2000) {
                if (libraryRuntime_->recentCount() != 0) {
                    fail("recent_pause_time_was_counted");
                }
                return;
            }
            if (libraryRuntime_->recentCount() != 0 || !player_->resume()) {
                fail("recent_pause_exclusion_failed");
                return;
            }
            enter(Phase::RecentResumeWait);
            return;

        case Phase::RecentResumeWait: {
            char recentPath[kTrackPathCapacity] = {};
            const bool recorded = libraryRuntime_->recentCount() == 1 &&
                libraryRuntime_->recentPathAt(0, recentPath,
                                              sizeof(recentPath)) &&
                pathEquals(recentPath, kPlaybackPath) &&
                libraryRuntime_->recentResult() == RecentTracksResult::Ok;
            const uint32_t elapsed = now - phaseStartedAtMs_;
            if (elapsed < 4000) {
                if (recorded) {
                    fail("recent_recorded_too_early_after_resume");
                }
                return;
            }
            if (!recorded) {
                if (elapsed > 7000) {
                    fail("recent_five_second_rule_timeout");
                }
                return;
            }
            autoRecentTimingPassed_ = true;
            enter(Phase::FindChinese);
            return;
        }

        case Phase::FindChinese:
            search = findEntryStep("中文目录", LibraryEntryType::Directory,
                                   found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found ||
                library.enter(found) == LibraryResult::Error) {
                fail("chinese_directory_failed");
                return;
            }
            enter(Phase::ChineseReady);
            return;

        case Phase::ChineseReady:
            if (!waitForReady()) return;
            if (!pathEquals(library.currentPath(),
                            "/Music/ADVWalkmanP2Test/中文目录")) {
                fail("chinese_path_mismatch");
                return;
            }
            enter(Phase::FindJapanese);
            return;

        case Phase::FindJapanese:
            search = findEntryStep("日本語", LibraryEntryType::Directory,
                                   found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found ||
                library.enter(found) == LibraryResult::Error) {
                fail("japanese_directory_failed");
                return;
            }
            enter(Phase::JapaneseReady);
            return;

        case Phase::JapaneseReady:
            if (!waitForReady()) return;
            enter(Phase::FindLevelThree);
            return;

        case Phase::FindLevelThree:
            search = findEntryStep("三级", LibraryEntryType::Directory,
                                   found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found ||
                library.enter(found) == LibraryResult::Error) {
                fail("third_level_directory_failed");
                return;
            }
            enter(Phase::LevelThreeReady);
            return;

        case Phase::LevelThreeReady:
            if (!waitForReady()) return;
            if (library.entryCount() != 1 || library.directoryCount() != 0 ||
                library.trackCount() != 1) {
                fail("deep_directory_count_mismatch");
                return;
            }
            enter(Phase::ValidateDeepTrack);
            return;

        case Phase::ValidateDeepTrack: {
            LibraryEntry entry;
            const LibraryResult result = library.entryAt(0, entry);
            if (result == LibraryResult::Pending) return;
            if (result != LibraryResult::Ok ||
                entry.type != LibraryEntryType::Track ||
                std::strcmp(entry.name, "深层歌曲.mp3") != 0) {
                fail("deep_track_read_failed");
                return;
            }
            deepReturnCount_ = 0;
            enter(Phase::ReturnFromDeep);
            return;
        }

        case Phase::ReturnFromDeep:
            if (!returnToFixtureStep()) return;
            if (player_->snapshot().state != PlayerState::Playing ||
                !player_->currentPath(playbackPath_, sizeof(playbackPath_)) ||
                !pathEquals(playbackPath_, kPlaybackPath)) {
                fail("browsing_changed_playback");
                return;
            }
            passTask(0,
                     "root_guard=1 nested_utf8=1 hidden_filtered=1 non_mp3_filtered=1 natural_sort=1 current_folder_queue=3 browsing_kept_playing=1");
            if (phase_ == Phase::Failed) return;
            library.clearStats();
            playbackBackpressureStart_ =
                player_->snapshot().backpressureEvents;
            playbackAudioErrorsStart_ =
                player_->snapshot().audioErrorEvents;
            enter(Phase::FindLarge);
            return;

        case Phase::FindLarge:
            search = findEntryStep("Large", LibraryEntryType::Directory,
                                   found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found ||
                library.enter(found) == LibraryResult::Error) {
                fail("large_directory_enter_failed");
                return;
            }
            enter(Phase::LargeReady);
            return;

        case Phase::LargeReady:
            if (!waitForReady(kDirectoryTimeoutMs)) return;
            if (library.entryCount() != 1000 ||
                library.directoryCount() != 0 || library.trackCount() != 1000) {
                fail("large_directory_count_mismatch");
                return;
            }
            largeIndex_ = 0;
            enter(Phase::ValidateLarge);
            return;

        case Phase::ValidateLarge: {
            if (largeIndex_ >= 1000) {
                enter(Phase::ReturnFromLarge);
                return;
            }
            LibraryEntry entry;
            const LibraryResult result = library.entryAt(largeIndex_, entry);
            if (result == LibraryResult::Pending) return;
            char expected[32] = {};
            std::snprintf(expected, sizeof(expected), "track-%04u.mp3",
                          static_cast<unsigned>(largeIndex_ + 1));
            if (result != LibraryResult::Ok ||
                entry.type != LibraryEntryType::Track ||
                std::strcmp(entry.name, expected) != 0) {
                fail("large_page_or_sort_mismatch");
                return;
            }
            ++largeIndex_;
            return;
        }

        case Phase::ReturnFromLarge:
            if (!returnToFixtureStep()) return;
            cacheVisitIndex_ = 0;
            cacheVisitInside_ = false;
            enter(Phase::CacheVisits);
            return;

        case Phase::CacheVisits: {
            if (cacheVisitIndex_ >= kCacheVisitCount) {
                if (!waitForReady()) return;
                if (!pathEquals(library.currentPath(), kFixtureRoot)) {
                    fail("cache_visit_did_not_return_to_fixture");
                    return;
                }
                const LibraryStats stats = library.stats();
                const PlayerSnapshot snapshot = player_->snapshot();
                if (stats.cacheHits == 0 || stats.cacheMisses == 0 ||
                    stats.cacheEvictions == 0 || stats.pageMisses == 0 ||
                    snapshot.state != PlayerState::Playing ||
                    snapshot.sampleRateHz != 44100 ||
                    snapshot.audioErrorEvents != playbackAudioErrorsStart_ ||
                    snapshot.backpressureEvents != playbackBackpressureStart_ ||
                    speakerStarvationCount_ != 0 ||
                    ESP.getFreeHeap() + 16384U < initialHeap_) {
                    fail("lazy_cache_or_audio_stability_failed");
                    return;
                }
                char detail[224] = {};
                std::snprintf(
                    detail, sizeof(detail),
                    "large_tracks=1000 pagination=1 natural_sort=1 lru_eviction=1 pinned_queue=1 audio_stable=1 heap_tolerance_bytes=16384 player_gap_gt100=%lu player_gap_max_us=%lu",
                    static_cast<unsigned long>(
                        playerServiceStartGapOver100Ms_),
                    static_cast<unsigned long>(playerServiceStartGapMaxUs_));
                passTask(1, detail);
                if (phase_ == Phase::Failed) return;
                enter(Phase::FindMetadata);
                return;
            }

            if (!cacheVisitInside_) {
                search = findEntryStep(
                    kCacheVisitDirectories[cacheVisitIndex_],
                    LibraryEntryType::Directory, found);
                if (search == SearchResult::Pending) return;
                if (search != SearchResult::Found ||
                    library.enter(found) == LibraryResult::Error) {
                    fail("cache_visit_enter_failed");
                    return;
                }
                cacheVisitInside_ = true;
                searchActive_ = false;
                return;
            }

            if (!waitForReady()) return;
            if (player_->snapshot().state != PlayerState::Playing) {
                fail("cache_visit_stopped_audio");
                return;
            }
            if (library.parent() == LibraryResult::Error) {
                fail("cache_visit_return_failed");
                return;
            }
            cacheVisitInside_ = false;
            ++cacheVisitIndex_;
            searchActive_ = false;
            return;
        }

        case Phase::FindMetadata:
            if (!waitForReady()) return;
            search = findEntryStep("Metadata", LibraryEntryType::Directory,
                                   found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found ||
                library.enter(found) == LibraryResult::Error) {
                fail("metadata_directory_enter_failed");
                return;
            }
            enter(Phase::MetadataReady);
            return;

        case Phase::MetadataReady:
            if (!waitForReady()) return;
            metadataCaseIndex_ = 0;
            metadataCaseStarted_ = false;
            metadataReader_.cancel();
            enter(Phase::ReadMetadata);
            return;

        case Phase::ReadMetadata: {
            if (metadataCaseIndex_ >= kMetadataCaseCount) {
                enter(Phase::FormalMetadataRequest);
                return;
            }
            if (!metadataCaseStarted_) {
                if (!startMetadataCase(metadataCaseIndex_)) {
                    fail("metadata_case_open_failed");
                }
                return;
            }
            if (metadataReader_.pending()) {
                metadataReader_.service();
                const Mp3MetadataStatus status = metadataReader_.status();
                metadataServiceMaxUs_ =
                    std::max(metadataServiceMaxUs_, status.serviceMaxUs);
                if (now - phaseStartedAtMs_ >
                    kMetadataTimeoutMs * (metadataCaseIndex_ + 1)) {
                    fail("metadata_case_timeout");
                }
                return;
            }
            if (!metadataReader_.ready() ||
                !validateMetadataCase(metadataCaseIndex_)) {
                fail("metadata_value_or_warning_mismatch");
                return;
            }
            ++metadataCasesPassed_;
            ++metadataCaseIndex_;
            metadataCaseStarted_ = false;
            return;
        }

        case Phase::FormalMetadataRequest:
            search = findEntryStep("id3-v23-utf16.mp3",
                                   LibraryEntryType::Track, found);
            if (search == SearchResult::Pending) return;
            if (search != SearchResult::Found) {
                fail("formal_metadata_track_missing");
                return;
            }
            if (libraryRuntime_->requestMetadata(found) ==
                LibraryResult::Error) {
                fail("formal_metadata_request_failed");
                return;
            }
            enter(Phase::FormalMetadataWait);
            return;

        case Phase::FormalMetadataWait: {
            Mp3Metadata metadata;
            const Mp3MetadataStatus status =
                libraryRuntime_->metadataStatus();
            if (!libraryRuntime_->metadata(metadata)) {
                if (status.state == Mp3MetadataState::Error) {
                    fail("formal_metadata_runtime_error");
                } else if (now - phaseStartedAtMs_ > kMetadataTimeoutMs) {
                    fail("formal_metadata_runtime_timeout");
                }
                return;
            }
            if (status.error != Mp3MetadataError::None ||
                std::strcmp(metadata.title.value, "星の歌") != 0 ||
                std::strcmp(metadata.artist.value, "测试歌手") != 0 ||
                std::strcmp(metadata.album.value, "蓝色专辑") != 0 ||
                std::strcmp(metadata.trackNumber.value, "2/12") != 0) {
                fail("formal_metadata_runtime_mismatch");
                return;
            }
            enter(Phase::ReturnFromMetadata);
            return;
        }

        case Phase::ReturnFromMetadata:
            if (!returnToFixtureStep()) return;
            if (metadataCasesPassed_ != kMetadataCaseCount ||
                metadataCache_.size() != kMetadataCaseCount ||
                libraryRuntime_->metadataCacheSize() == 0) {
                fail("metadata_cache_or_case_count_failed");
                return;
            }
            passTask(2,
                     "id3v23=1 id3v24=1 utf16=1 utf16be=1 utf8=1 latin1=1 unsync=1 extended=1 fallback=1 malformed_fallback=1 cache_warning=1 runtime_api=1");
            if (phase_ == Phase::Failed) return;
            enter(Phase::WaitAutoRecent);
            return;

        case Phase::WaitAutoRecent: {
            char recentPath[kTrackPathCapacity] = {};
            const bool automaticRecorded =
                libraryRuntime_->recentCount() > 0 &&
                libraryRuntime_->recentPathAt(0, recentPath,
                                               sizeof(recentPath)) &&
                pathEquals(recentPath, kPlaybackPath) &&
                libraryRuntime_->recentResult() == RecentTracksResult::Ok;
            if (!autoRecentTimingPassed_ || !automaticRecorded) {
                if (now - phaseStartedAtMs_ > kRecentTimeoutMs) {
                    fail("recent_five_second_rule_timeout");
                }
                return;
            }
            if (!recentStore_.begin(SD, kRecentSlotA, kRecentSlotB)) {
                fail("recent_test_store_begin_failed");
                return;
            }
            const RecentTracksResult result = recentStore_.load();
            if (result != RecentTracksResult::NotFound &&
                result != RecentTracksResult::Ok) {
                fail("recent_test_store_load_failed");
                return;
            }
            recentSequenceIndex_ = 0;
            recentWriteStarted_ = false;
            enter(Phase::RecentAba);
            return;
        }

        case Phase::RecentAba: {
            constexpr size_t sequence[] = {1, 2, 1};
            if (recentSequenceIndex_ < 3) {
                char path[kTrackPathCapacity] = {};
                largeTrackPath(sequence[recentSequenceIndex_], path,
                               sizeof(path));
                if (recordRecentStep(recentStore_, path)) {
                    ++recentSequenceIndex_;
                }
                return;
            }
            char first[kTrackPathCapacity] = {};
            char second[kTrackPathCapacity] = {};
            if (recentStore_.count() != 2 ||
                !recentStore_.pathAt(0, first, sizeof(first)) ||
                !recentStore_.pathAt(1, second, sizeof(second))) {
                fail("recent_aba_count_failed");
                return;
            }
            char expectedA[kTrackPathCapacity] = {};
            char expectedB[kTrackPathCapacity] = {};
            largeTrackPath(1, expectedA, sizeof(expectedA));
            largeTrackPath(2, expectedB, sizeof(expectedB));
            if (!pathEquals(first, expectedA) || !pathEquals(second, expectedB)) {
                fail("recent_aba_order_failed");
                return;
            }
            recentBulkIndex_ = 1;
            recentWriteStarted_ = false;
            enter(Phase::RecentBulk);
            return;
        }

        case Phase::RecentBulk:
            if (recentBulkIndex_ <= 33) {
                char path[kTrackPathCapacity] = {};
                largeTrackPath(recentBulkIndex_, path, sizeof(path));
                if (recordRecentStep(recentStore_, path)) {
                    ++recentBulkIndex_;
                }
                return;
            } else {
                char first[kTrackPathCapacity] = {};
                char last[kTrackPathCapacity] = {};
                char expectedFirst[kTrackPathCapacity] = {};
                char expectedLast[kTrackPathCapacity] = {};
                largeTrackPath(33, expectedFirst, sizeof(expectedFirst));
                largeTrackPath(2, expectedLast, sizeof(expectedLast));
                if (recentStore_.count() != 32 ||
                    !recentStore_.pathAt(0, first, sizeof(first)) ||
                    !recentStore_.pathAt(31, last, sizeof(last)) ||
                    !pathEquals(first, expectedFirst) ||
                    !pathEquals(last, expectedLast)) {
                    fail("recent_32_limit_or_order_failed");
                    return;
                }
                recentMissingStep_ = 0;
                recentWriteStarted_ = false;
                enter(Phase::RecentMissing);
                return;
            }

        case Phase::RecentMissing:
            if (recentMissingStep_ == 0) {
                SD.remove(kMissingRecentPath);
                File temporary = SD.open(kMissingRecentPath, FILE_WRITE);
                if (!temporary || temporary.write(static_cast<uint8_t>(0)) !=
                                      1) {
                    if (temporary) temporary.close();
                    fail("recent_missing_fixture_create_failed");
                    return;
                }
                temporary.flush();
                temporary.close();
                recentMissingStep_ = 1;
                return;
            }
            if (recentMissingStep_ == 1) {
                if (!recordRecentStep(recentStore_, kMissingRecentPath)) {
                    return;
                }
                if (!SD.remove(kMissingRecentPath)) {
                    fail("recent_missing_fixture_remove_failed");
                    return;
                }
                if (!reloadStore_.begin(SD, kRecentSlotA, kRecentSlotB)) {
                    fail("recent_reload_begin_failed");
                    return;
                }
                const RecentTracksResult result = reloadStore_.load();
                if (result != RecentTracksResult::Ok) {
                    fail("recent_reload_load_failed");
                    return;
                }
                enter(Phase::RecentReload);
                return;
            }
            return;

        case Phase::RecentReload:
            if (reloadStore_.pending()) {
                reloadStore_.service();
                if (now - phaseStartedAtMs_ > kRecentTimeoutMs) {
                    fail("recent_cleanup_save_timeout");
                }
                return;
            }
            if (reloadStore_.lastResult() != RecentTracksResult::Ok ||
                reloadStore_.count() != 31) {
                fail("recent_missing_filter_failed");
                return;
            }
            for (size_t index = 0; index < reloadStore_.count(); ++index) {
                char path[kTrackPathCapacity] = {};
                if (!reloadStore_.pathAt(index, path, sizeof(path)) ||
                    pathEquals(path, kMissingRecentPath)) {
                    fail("recent_missing_path_survived");
                    return;
                }
            }
            if (!reloadStore_.begin(SD, kRecentSlotA, kRecentSlotB) ||
                reloadStore_.load() != RecentTracksResult::Ok) {
                fail("recent_clean_reload_failed");
                return;
            }
            enter(Phase::RecentReloadClean);
            return;

        case Phase::RecentReloadClean: {
            if (reloadStore_.pending()) {
                reloadStore_.service();
                return;
            }
            char first[kTrackPathCapacity] = {};
            char expectedFirst[kTrackPathCapacity] = {};
            largeTrackPath(33, expectedFirst, sizeof(expectedFirst));
            if (reloadStore_.lastResult() != RecentTracksResult::Ok ||
                reloadStore_.count() != 31 ||
                !reloadStore_.pathAt(0, first, sizeof(first)) ||
                !pathEquals(first, expectedFirst) ||
                !SD.exists(kRecentSlotA) || !SD.exists(kRecentSlotB)) {
                fail("recent_crc_roundtrip_failed");
                return;
            }
            passTask(3,
                     "playing_5s=1 pause_excluded=1 no_early_record=1 aba_dedupe=1 maximum=32 eviction=1 missing_filtered=1 ab_slots=1 crc_reload=1 isolated_state=1");
            if (phase_ == Phase::Failed) return;
            finalizePass();
            return;
        }

        default:
            return;
    }
}

void P2DeviceTestRunner::monitorPlayback(uint32_t now) {
    minimumHeap_ = std::min(minimumHeap_, ESP.getFreeHeap());
    if (!playbackSelected_ || player_ == nullptr) {
        return;
    }
    const PlayerSnapshot snapshot = player_->snapshot();
    const bool expectPaused = phase_ == Phase::RecentPaused;
    const bool expectPlaying = !expectPaused;

    if (snapshot.state == PlayerState::Error ||
        snapshot.audioErrorEvents != playbackAudioErrorsStart_) {
        fail("audio_error_during_library_gate");
        return;
    }
    if (snapshot.backpressureEvents != playbackBackpressureStart_) {
        fail("backpressure_during_library_gate");
        return;
    }
    char path[kTrackPathCapacity] = {};
    if (!snapshot.hasCurrent || !player_->currentPath(path, sizeof(path)) ||
        !pathEquals(path, kPlaybackPath)) {
        fail("pinned_queue_path_changed");
        return;
    }

    const bool stateHealthy =
        expectPaused
            ? snapshot.state == PlayerState::Paused
            : snapshot.state == PlayerState::Playing &&
                  snapshot.sampleRateHz == 44100;
    if (!stateHealthy) {
        if (unexpectedPlaybackStateSinceMs_ == 0) {
            unexpectedPlaybackStateSinceMs_ = now;
        } else if (now - unexpectedPlaybackStateSinceMs_ > 100) {
            ++unexpectedPlaybackStateOver100Ms_;
            fail(expectPaused ? "player_did_not_remain_paused"
                              : "player_did_not_remain_playing");
            return;
        }
    } else {
        unexpectedPlaybackStateSinceMs_ = 0;
    }

    if (snapshot.state == PlayerState::Playing && snapshot.sampleRateHz > 0 &&
        M5.Speaker.isPlaying(0) == 0) {
        if (speakerSilentSinceMs_ == 0) {
            speakerSilentSinceMs_ = now;
            speakerStarvationReported_ = false;
        } else if (!speakerStarvationReported_ &&
                   now - speakerSilentSinceMs_ > 100) {
            ++speakerStarvationCount_;
            speakerStarvationReported_ = true;
            fail("speaker_starvation_over_100ms");
            return;
        }
    } else {
        speakerSilentSinceMs_ = 0;
        speakerStarvationReported_ = false;
    }
}

bool P2DeviceTestRunner::waitForReady(uint32_t timeoutMs) {
    const MusicLibrary& library = libraryRuntime_->library();
    if (library.state() == LibraryState::Ready) {
        return true;
    }
    if (library.state() == LibraryState::Error) {
        fail("library_state_error");
    } else if (millis() - phaseStartedAtMs_ > timeoutMs) {
        fail("library_ready_timeout");
    }
    return false;
}

P2DeviceTestRunner::SearchResult P2DeviceTestRunner::findEntryStep(
    const char* name, LibraryEntryType type, size_t& outputIndex) {
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() == LibraryState::Error) {
        return SearchResult::Error;
    }
    if (library.state() != LibraryState::Ready) {
        return SearchResult::Pending;
    }
    if (!searchActive_ || std::strcmp(searchName_, name) != 0 ||
        searchType_ != type) {
        const size_t length = std::strlen(name);
        if (length >= sizeof(searchName_)) {
            return SearchResult::Error;
        }
        std::memcpy(searchName_, name, length + 1);
        searchType_ = type;
        searchIndex_ = 0;
        searchActive_ = true;
    }
    if (searchIndex_ >= library.entryCount()) {
        searchActive_ = false;
        return SearchResult::Missing;
    }
    LibraryEntry entry;
    const LibraryResult result = library.entryAt(searchIndex_, entry);
    if (result == LibraryResult::Pending) return SearchResult::Pending;
    if (result == LibraryResult::Error) return SearchResult::Error;
    if (entry.type == type && std::strcmp(entry.name, name) == 0) {
        outputIndex = searchIndex_;
        searchActive_ = false;
        return SearchResult::Found;
    }
    ++searchIndex_;
    return SearchResult::Pending;
}

bool P2DeviceTestRunner::returnToFixtureStep() {
    MusicLibrary& library = libraryRuntime_->library();
    if (!waitForReady()) return false;
    if (pathEquals(library.currentPath(), kFixtureRoot)) {
        return true;
    }
    if (library.parent() == LibraryResult::Error) {
        fail("return_to_fixture_failed");
    }
    ++deepReturnCount_;
    return false;
}

bool P2DeviceTestRunner::startMetadataCase(size_t index) {
    if (index >= kMetadataCaseCount) return false;
    char path[kTrackPathCapacity] = {};
    const int written = std::snprintf(path, sizeof(path), "%s/Metadata/%s",
                                      kFixtureRoot,
                                      kMetadataCases[index].filename);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(path) ||
        !metadataReader_.begin(SD, path)) {
        return false;
    }
    metadataCaseStarted_ = true;
    return true;
}

bool P2DeviceTestRunner::validateMetadataCase(size_t index) {
    if (index >= kMetadataCaseCount || !metadataReader_.ready()) return false;
    const MetadataCase& expected = kMetadataCases[index];
    const Mp3Metadata& actual = metadataReader_.metadata();
    const Mp3MetadataStatus status = metadataReader_.status();
    metadataBytesRead_ += status.bytesRead;
    const bool titleOk = expected.titlePrefixOnly
                             ? startsWith(actual.title.value, expected.title)
                             : std::strcmp(actual.title.value,
                                           expected.title) == 0;
    const bool artistOk = expected.artist == nullptr ||
                          std::strcmp(actual.artist.value,
                                      expected.artist) == 0;
    const bool albumOk = expected.album == nullptr ||
                         std::strcmp(actual.album.value,
                                     expected.album) == 0;
    const bool trackOk = expected.track == nullptr ||
                         std::strcmp(actual.trackNumber.value,
                                     expected.track) == 0;
    if (!titleOk || !artistOk || !albumOk || !trackOk ||
        status.error != expected.warning ||
        actual.titleFromFilename != expected.titleFallback ||
        actual.title.truncated != expected.titleTruncated) {
        return false;
    }

    char path[kTrackPathCapacity] = {};
    std::snprintf(path, sizeof(path), "%s/Metadata/%s", kFixtureRoot,
                  expected.filename);
    if (!metadataCache_.put(path, actual, status.error)) return false;
    Mp3Metadata cached;
    Mp3MetadataError cachedWarning = Mp3MetadataError::None;
    return metadataCache_.lookup(path, cached, &cachedWarning) &&
           cachedWarning == status.error &&
           std::strcmp(cached.title.value, actual.title.value) == 0;
}

bool P2DeviceTestRunner::serviceRecentWrite(RecentTracksStore& store) {
    if (store.pending()) {
        store.service();
        return false;
    }
    if (store.lastResult() != RecentTracksResult::Ok) {
        fail("recent_write_failed");
        return false;
    }
    recentWriteStarted_ = false;
    return true;
}

bool P2DeviceTestRunner::recordRecentStep(RecentTracksStore& store,
                                          const char* path) {
    if (!recentWriteStarted_) {
        const RecentTracksResult result = store.record(path);
        if (result == RecentTracksResult::Ok) {
            return true;
        }
        if (result != RecentTracksResult::Pending) {
            fail("recent_record_failed");
            return false;
        }
        recentWriteStarted_ = true;
    }
    return serviceRecentWrite(store);
}

void P2DeviceTestRunner::enter(Phase phase) {
    phase_ = phase;
    phaseStartedAtMs_ = millis();
    searchActive_ = false;
    render(true);
}

void P2DeviceTestRunner::passTask(size_t taskIndex, const char* detail) {
    if (taskIndex >= 4) return;
    std::strncpy(taskDetail_[taskIndex], detail == nullptr ? "none" : detail,
                 sizeof(taskDetail_[taskIndex]) - 1);
    taskDetail_[taskIndex][sizeof(taskDetail_[taskIndex]) - 1] = '\0';
    taskPassed_[taskIndex] = true;
}

bool P2DeviceTestRunner::finalizePass() {
    for (bool passed : taskPassed_) {
        if (!passed) {
            fail("gate_task_not_completed");
            return false;
        }
    }
    const PlayerSnapshot health = player_->snapshot();
    if (!playbackSelected_ || health.state != PlayerState::Playing ||
        health.error != PlayerError::None ||
        health.audioError != AudioError::None || health.sampleRateHz != 44100 ||
        health.audioErrorEvents != playbackAudioErrorsStart_ ||
        health.backpressureEvents != playbackBackpressureStart_ ||
        speakerStarvationCount_ != 0) {
        fail("final_audio_health_failed");
        return false;
    }
    finalPlayerSnapshot_ = player_->snapshot();
    finalPlayerSnapshotValid_ = true;
    player_->stop();
    for (size_t index = 0; index < 4; ++index) {
        if (!writeLog(index, "PASS", taskDetail_[index])) {
            taskPassed_[index] = false;
            fail("pass_log_write_failed");
            return false;
        }
    }
    enter(Phase::Passed);
    return true;
}

void P2DeviceTestRunner::fail(const char* reason) {
    if (phase_ == Phase::Failed) return;
    std::strncpy(failure_, reason == nullptr ? "unknown" : reason,
                 sizeof(failure_) - 1);
    failure_[sizeof(failure_) - 1] = '\0';
    if (player_ != nullptr && !finalPlayerSnapshotValid_) {
        finalPlayerSnapshot_ = player_->snapshot();
        finalPlayerSnapshotValid_ = true;
        player_->stop();
    }
    SD.remove(kMissingRecentPath);
    for (size_t index = 0; index < 4; ++index) {
        writeLog(index, taskPassed_[index] ? "PASS" : "FAIL",
                 taskPassed_[index] ? taskDetail_[index] : failure_);
    }
    enter(Phase::Failed);
}

void P2DeviceTestRunner::render(bool force) {
    if (!force) return;
    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(6, 6);
    display.setTextColor(phase_ == Phase::Failed ? RED : YELLOW, BLACK);
    display.setTextSize(1.35f);
    display.printf("P2 GATE %s\n",
                   phase_ == Phase::Passed
                       ? "PASS"
                       : phase_ == Phase::Failed ? "FAIL" : "RUNNING");
    display.setTextSize(1.0f);
    display.setTextColor(WHITE, BLACK);
    display.printf("%s\n", phaseName());
    if (player_ != nullptr) {
        const PlayerSnapshot snapshot = player_->snapshot();
        display.printf("Audio %s SR %lu\n", playerStateName(snapshot.state),
                       static_cast<unsigned long>(snapshot.sampleRateHz));
    }
    if (libraryRuntime_ != nullptr) {
        const MusicLibrary& library = libraryRuntime_->library();
        display.printf("Lib %s %u entries\n", libraryStateName(library.state()),
                       static_cast<unsigned>(library.entryCount()));
    }
    display.printf("Heap %lu Min %lu\n",
                   static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(minimumHeap_));
    if (phase_ == Phase::Failed) {
        display.setTextColor(ORANGE, BLACK);
        display.printf("%s\n", failure_);
        display.println("Logs saved to SD");
    } else if (phase_ == Phase::Passed) {
        display.setTextColor(GREEN, BLACK);
        display.println("4 logs saved to SD");
    } else {
        display.println("Do not press keys");
    }
}

bool P2DeviceTestRunner::writeLog(size_t taskIndex, const char* status,
                                  const char* detail) {
    if (taskIndex >= 4 || status == nullptr) return false;
    const char* path = kLogPaths[taskIndex];
    SD.remove(path);
    File log = SD.open(path, FILE_WRITE);
    if (!log) return false;

    const PlayerSnapshot snapshot = finalPlayerSnapshotValid_
                                        ? finalPlayerSnapshot_
                                        : player_ == nullptr
                                              ? PlayerSnapshot{}
                                              : player_->snapshot();
    const MusicLibrary* library =
        libraryRuntime_ == nullptr ? nullptr : &libraryRuntime_->library();
    const LibraryStats stats =
        library == nullptr ? LibraryStats{} : library->stats();
    log.printf("status=%s\n", status);
    log.printf("version=%s\n", ADV_WALKMAN_VERSION);
    log.printf("gate=P2\n");
    log.printf("task=P2-0%u\n", static_cast<unsigned>(taskIndex + 1));
    log.printf("phase=%s\n", phaseName());
    log.printf("detail=%s\n", detail == nullptr ? "none" : detail);
    log.printf("elapsed_ms=%lu\n",
               static_cast<unsigned long>(millis() - runStartedAtMs_));
    log.printf("fixture_manifest_sha256=%s\n", manifestSha256_);
    log.printf("library_path=%s\n",
               library == nullptr ? "none" : library->currentPath());
    log.printf("library_state=%s\n",
               library == nullptr ? "none" : libraryStateName(library->state()));
    log.printf("library_error=%s\n",
               library == nullptr ? "none" : libraryErrorName(library->error()));
    log.printf("library_entries=%u\n",
               library == nullptr ? 0U :
                   static_cast<unsigned>(library->entryCount()));
    log.printf("cache_hits=%lu\n", static_cast<unsigned long>(stats.cacheHits));
    log.printf("cache_misses=%lu\n",
               static_cast<unsigned long>(stats.cacheMisses));
    log.printf("cache_evictions=%lu\n",
               static_cast<unsigned long>(stats.cacheEvictions));
    log.printf("page_hits=%lu\n", static_cast<unsigned long>(stats.pageHits));
    log.printf("page_misses=%lu\n",
               static_cast<unsigned long>(stats.pageMisses));
    log.printf("library_service_max_us=%lu\n",
               static_cast<unsigned long>(stats.serviceMaxUs));
    log.printf("library_entry_read_max_us=%lu\n",
               static_cast<unsigned long>(stats.entryReadMaxUs));
    log.printf("library_over_budget_steps=%lu\n",
               static_cast<unsigned long>(stats.overBudgetSteps));
    log.printf("player_state=%s\n", playerStateName(snapshot.state));
    log.printf("player_error=%s\n", playerErrorName(snapshot.error));
    log.printf("audio_error=%s\n", audioErrorName(snapshot.audioError));
    log.printf("sample_rate=%lu\n",
               static_cast<unsigned long>(snapshot.sampleRateHz));
    log.printf("backpressure=%lu\n",
               static_cast<unsigned long>(snapshot.backpressureEvents));
    log.printf("audio_error_events=%lu\n",
               static_cast<unsigned long>(snapshot.audioErrorEvents));
    log.printf("player_service_start_gap_over_100ms=%lu\n",
               static_cast<unsigned long>(playerServiceStartGapOver100Ms_));
    log.printf("player_service_start_gap_max_us=%lu\n",
               static_cast<unsigned long>(playerServiceStartGapMaxUs_));
    log.printf("player_service_start_gap_max_from_phase=%s\n",
               playerServiceStartGapMaxFromPhase_);
    log.printf("player_service_start_gap_max_to_phase=%s\n",
               playerServiceStartGapMaxToPhase_);
    log.printf("player_gap_previous_player_runtime_us=%lu\n",
               static_cast<unsigned long>(
                   playerGapPreviousPlayerRuntimeUs_));
    log.printf("player_gap_previous_library_runtime_us=%lu\n",
               static_cast<unsigned long>(
                   playerGapPreviousLibraryRuntimeUs_));
    log.printf("player_gap_previous_gate_us=%lu\n",
               static_cast<unsigned long>(playerGapPreviousGateUs_));
    log.printf("player_gap_previous_loop_body_us=%lu\n",
               static_cast<unsigned long>(playerGapPreviousLoopBodyUs_));
    log.printf("player_gap_current_input_us=%lu\n",
               static_cast<unsigned long>(playerGapCurrentInputUs_));
    log.printf("speaker_channel_empty_at_service_start=%lu\n",
               static_cast<unsigned long>(speakerChannelEmptyAtServiceStart_));
    log.printf("unexpected_playback_state_over_100ms=%lu\n",
               static_cast<unsigned long>(
                   unexpectedPlaybackStateOver100Ms_));
    log.printf("input_update_max_us=%lu\n",
               static_cast<unsigned long>(inputUpdateMaxUs_));
    log.printf("player_runtime_service_max_us=%lu\n",
               static_cast<unsigned long>(playerRuntimeServiceMaxUs_));
    log.printf("player_engine_service_max_us=%lu\n",
               static_cast<unsigned long>(snapshot.serviceMaxUs));
    log.printf("library_runtime_service_max_us=%lu\n",
               static_cast<unsigned long>(libraryRuntimeServiceMaxUs_));
    log.printf("gate_service_max_us=%lu\n",
               static_cast<unsigned long>(gateServiceMaxUs_));
    log.printf("loop_body_max_us=%lu\n",
               static_cast<unsigned long>(loopBodyMaxUs_));
    log.printf("speaker_starvation_over_100ms=%lu\n",
               static_cast<unsigned long>(speakerStarvationCount_));
    log.printf("heap_start=%lu\n", static_cast<unsigned long>(initialHeap_));
    log.printf("heap_now=%lu\n",
               static_cast<unsigned long>(ESP.getFreeHeap()));
    log.printf("heap_min_sampled=%lu\n",
               static_cast<unsigned long>(minimumHeap_));
    log.printf("metadata_cases=%lu\n",
               static_cast<unsigned long>(metadataCasesPassed_));
    log.printf("metadata_cache_entries=%u\n",
               static_cast<unsigned>(metadataCache_.size()));
    log.printf("metadata_service_max_us=%lu\n",
               static_cast<unsigned long>(metadataServiceMaxUs_));
    log.printf("metadata_bytes_read_accumulated=%lu\n",
               static_cast<unsigned long>(metadataBytesRead_));
    log.printf("recent_auto_count=%u\n",
               libraryRuntime_ == nullptr
                   ? 0U
                   : static_cast<unsigned>(libraryRuntime_->recentCount()));
    log.printf("recent_test_count=%u\n",
               static_cast<unsigned>(reloadStore_.loaded()
                                         ? reloadStore_.count()
                                         : recentStore_.count()));
    log.printf("recent_generation=%lu\n",
               static_cast<unsigned long>(reloadStore_.loaded()
                                              ? reloadStore_.generation()
                                              : recentStore_.generation()));
    log.print("log_complete=1\n");
    log.flush();
    log.close();

    File verify = SD.open(path, FILE_READ);
    if (!verify) return false;
    char firstLine[32] = {};
    const size_t read = verify.readBytesUntil('\n', firstLine,
                                              sizeof(firstLine) - 1);
    firstLine[read] = '\0';
    char expected[32] = {};
    std::snprintf(expected, sizeof(expected), "status=%s", status);
    bool complete = false;
    char line[64] = {};
    while (verify.available()) {
        const size_t length = verify.readBytesUntil('\n', line,
                                                    sizeof(line) - 1);
        line[length] = '\0';
        if (std::strcmp(line, "log_complete=1") == 0) {
            complete = true;
        }
    }
    verify.close();
    return std::strcmp(firstLine, expected) == 0 && complete;
}

bool P2DeviceTestRunner::verifyFixtureMarker() {
    File marker = SD.open(kFixtureMarker, FILE_READ);
    if (!marker || marker.size() == 0 || marker.size() >= 512) {
        if (marker) marker.close();
        return false;
    }
    char buffer[512] = {};
    const size_t received = marker.readBytes(buffer, marker.size());
    marker.close();
    buffer[received] = '\0';
    if (std::strstr(buffer, "adv-walkman-p2-fixture") == nullptr) {
        return false;
    }
    const char* key = std::strstr(buffer, "\"manifest_sha256\"");
    if (key == nullptr) return false;
    const char* colon = std::strchr(key, ':');
    const char* quote = colon == nullptr ? nullptr : std::strchr(colon, '"');
    if (quote == nullptr) return false;
    ++quote;
    for (size_t index = 0; index < 64; ++index) {
        if (!std::isxdigit(static_cast<unsigned char>(quote[index]))) {
            return false;
        }
    }
    if (quote[64] != '"') return false;
    char markerHash[65] = {};
    std::memcpy(markerHash, quote, 64);
    if (!sha256File(kFixtureManifest, manifestSha256_)) return false;
    return equalsIgnoreAsciiCase(markerHash, manifestSha256_);
}

bool P2DeviceTestRunner::sha256File(const char* path, char output[65]) {
    File file = SD.open(path, FILE_READ);
    if (!file) return false;
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    bool ok = mbedtls_sha256_starts_ret(&context, 0) == 0;
    uint8_t buffer[1024] = {};
    while (ok && file.available()) {
        const size_t received = file.read(buffer, sizeof(buffer));
        if (received == 0) {
            ok = false;
            break;
        }
        ok = mbedtls_sha256_update_ret(&context, buffer, received) == 0;
        yield();
    }
    file.close();
    uint8_t digest[32] = {};
    if (ok) ok = mbedtls_sha256_finish_ret(&context, digest) == 0;
    mbedtls_sha256_free(&context);
    if (!ok) return false;
    for (size_t index = 0; index < sizeof(digest); ++index) {
        std::snprintf(output + index * 2, 3, "%02x", digest[index]);
    }
    output[64] = '\0';
    return true;
}

bool P2DeviceTestRunner::equalsIgnoreAsciiCase(const char* left,
                                                const char* right) {
    if (left == nullptr || right == nullptr) return left == right;
    while (*left != '\0' && *right != '\0') {
        const unsigned char a = static_cast<unsigned char>(*left++);
        const unsigned char b = static_cast<unsigned char>(*right++);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return *left == *right;
}

bool P2DeviceTestRunner::startsWith(const char* value, const char* prefix) {
    if (value == nullptr || prefix == nullptr) return false;
    return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

}  // namespace player
}  // namespace adv_walkman
