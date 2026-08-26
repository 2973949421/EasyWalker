#pragma once

#include <cstddef>
#include <cstdint>

#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"
#include "player/library/MetadataCache.h"
#include "player/library/Mp3MetadataReader.h"
#include "player/library/RecentTracksStore.h"

namespace adv_walkman {
namespace player {

class P2DeviceTestRunner final {
  public:
    void begin(PlayerRuntime& player, LibraryRuntime& libraryRuntime);
    bool start();
    void service();

    bool active() const;
    bool ownsDisplay() const;
    const char* phaseName() const;

  private:
    enum class Phase : uint8_t {
        Idle,
        OpenRoot,
        FindFixture,
        FixtureReady,
        ValidateFixture,
        SelectPlayback,
        WaitPlayback,
        RecentPrePause,
        RecentPaused,
        RecentResumeWait,
        FindChinese,
        ChineseReady,
        FindJapanese,
        JapaneseReady,
        FindLevelThree,
        LevelThreeReady,
        ValidateDeepTrack,
        ReturnFromDeep,
        FindLarge,
        LargeReady,
        ValidateLarge,
        ReturnFromLarge,
        CacheVisits,
        FindMetadata,
        MetadataReady,
        ReadMetadata,
        FormalMetadataRequest,
        FormalMetadataWait,
        ReturnFromMetadata,
        WaitAutoRecent,
        RecentAba,
        RecentBulk,
        RecentMissing,
        RecentReload,
        RecentReloadClean,
        Passed,
        Failed,
    };

    enum class SearchResult : uint8_t {
        Pending,
        Found,
        Missing,
        Error,
    };

    void resetRun();
    void servicePhase(uint32_t now);
    void monitorPlayback(uint32_t now);
    bool waitForReady(uint32_t timeoutMs = 30000);
    SearchResult findEntryStep(const char* name, LibraryEntryType type,
                               size_t& outputIndex);
    bool returnToFixtureStep();
    bool startMetadataCase(size_t index);
    bool validateMetadataCase(size_t index);
    bool serviceRecentWrite(RecentTracksStore& store);
    bool recordRecentStep(RecentTracksStore& store, const char* path);

    void enter(Phase phase);
    void passTask(size_t taskIndex, const char* detail);
    bool finalizePass();
    void fail(const char* reason);
    void render(bool force = false);
    bool writeLog(size_t taskIndex, const char* status, const char* detail);
    bool verifyFixtureMarker();
    bool sha256File(const char* path, char output[65]);
    static bool equalsIgnoreAsciiCase(const char* left, const char* right);
    static bool startsWith(const char* value, const char* prefix);

    PlayerRuntime* player_ = nullptr;
    LibraryRuntime* libraryRuntime_ = nullptr;
    Phase phase_ = Phase::Idle;
    uint32_t phaseStartedAtMs_ = 0;
    uint32_t runStartedAtMs_ = 0;

    size_t searchIndex_ = 0;
    bool searchActive_ = false;
    char searchName_[kTrackPathCapacity] = {};
    LibraryEntryType searchType_ = LibraryEntryType::Track;

    size_t validationIndex_ = 0;
    size_t playbackEntryIndex_ = 0;
    size_t deepReturnCount_ = 0;
    size_t largeIndex_ = 0;
    size_t cacheVisitIndex_ = 0;
    bool cacheVisitInside_ = false;
    size_t metadataCaseIndex_ = 0;
    bool metadataCaseStarted_ = false;

    RecentTracksStore recentStore_;
    RecentTracksStore reloadStore_;
    size_t recentSequenceIndex_ = 0;
    size_t recentBulkIndex_ = 0;
    uint8_t recentMissingStep_ = 0;
    bool recentWriteStarted_ = false;

    Mp3MetadataReader metadataReader_;
    MetadataCache metadataCache_;

    uint32_t initialHeap_ = 0;
    uint32_t minimumHeap_ = 0;
    uint32_t playbackBackpressureStart_ = 0;
    uint32_t playbackAudioErrorsStart_ = 0;
    uint32_t metadataServiceMaxUs_ = 0;
    uint32_t metadataBytesRead_ = 0;
    uint32_t metadataCasesPassed_ = 0;
    uint32_t speakerSilentSinceMs_ = 0;
    uint32_t speakerStarvationCount_ = 0;
    uint32_t lastPlaybackMonitorAtMs_ = 0;
    uint32_t unexpectedPlaybackStateSinceMs_ = 0;
    bool speakerStarvationReported_ = false;
    bool lastPlaybackWasPlaying_ = false;
    bool playbackSelected_ = false;
    bool taskPassed_[4] = {};
    char taskDetail_[4][224] = {};
    PlayerSnapshot finalPlayerSnapshot_{};
    bool finalPlayerSnapshotValid_ = false;
    bool autoRecentTimingPassed_ = false;
    char playbackPath_[kTrackPathCapacity] = {};
    char manifestSha256_[65] = "unverified";
    char failure_[96] = "none";
};

}  // namespace player
}  // namespace adv_walkman
