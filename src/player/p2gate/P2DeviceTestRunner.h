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
    void recordLoopTimings(uint32_t inputUpdateUs,
                           uint32_t playerServiceStartGapUs,
                           bool speakerRequestSlotOccupiedAtServiceStart,
                           uint32_t playerRuntimeServiceUs,
                           uint32_t libraryRuntimeServiceUs,
                           uint32_t preGateLoopBodyUs);
    void recordGateServiceTiming(uint32_t gateServiceUs,
                                 uint32_t loopBodyUs);

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
        RecentRaceWait,
        RecentRaceRestore,
        FindChinese,
        ChineseReady,
        FindJapanese,
        JapaneseReady,
        FindLevelThree,
        LevelThreeReady,
        ValidateDeepTrack,
        ReturnFromDeep,
        WaitShortRepeat,
        ReturnToMusicRootForBenchmark,
        FindBenchmark,
        BenchmarkReady,
        FindBenchmarkTrack,
        SelectBenchmark,
        WaitBenchmark,
        ReturnToMusicRootForFixture,
        FindFixtureForStress,
        FixtureStressReady,
        FindLarge,
        LargeReady,
        ValidateLarge,
        ReturnFromLarge,
        CacheVisits,
        FindLongSort,
        LongSortReady,
        ValidateLongSort,
        ReturnFromLongSort,
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
    void captureTaskSnapshot(size_t taskIndex);
    void resetMeasurementWindow(const char* track);
    size_t currentTaskIndex() const;
    bool finalizePass();
    void fail(const char* reason);
    void render(bool force = false);
    bool writeLog(size_t taskIndex, const char* status, const char* detail);
    bool verifyFixtureMarker();
    bool sha256File(const char* path, char output[65]);
    bool verifyLongBenchmark();
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
    size_t longSortIndex_ = 0;
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
    uint32_t speakerRequestSlotEmptySamples_ = 0;
    uint32_t unexpectedPlaybackStateOver100Ms_ = 0;
    uint32_t playerServiceStartGapMaxUs_ = 0;
    uint32_t playerServiceStartGapOver100Ms_ = 0;
    uint32_t inputUpdateMaxUs_ = 0;
    uint32_t playerRuntimeServiceMaxUs_ = 0;
    uint32_t libraryRuntimeServiceMaxUs_ = 0;
    uint32_t gateServiceMaxUs_ = 0;
    uint32_t loopBodyMaxUs_ = 0;
    uint32_t pendingLoopPlayerRuntimeUs_ = 0;
    uint32_t pendingLoopLibraryRuntimeUs_ = 0;
    uint32_t lastLoopPlayerRuntimeUs_ = 0;
    uint32_t lastLoopLibraryRuntimeUs_ = 0;
    uint32_t lastLoopGateUs_ = 0;
    uint32_t lastLoopBodyUs_ = 0;
    uint32_t playerGapPreviousPlayerRuntimeUs_ = 0;
    uint32_t playerGapPreviousLibraryRuntimeUs_ = 0;
    uint32_t playerGapPreviousGateUs_ = 0;
    uint32_t playerGapPreviousLoopBodyUs_ = 0;
    uint32_t playerGapCurrentInputUs_ = 0;
    uint32_t unexpectedPlaybackStateSinceMs_ = 0;
    bool playerServiceGapArmed_ = false;
    bool pendingLoopTimingValid_ = false;
    bool lastLoopTimingValid_ = false;
    bool playbackSelected_ = false;
    bool longMeasurementActive_ = false;
    uint32_t measurementStartedAtMs_ = 0;
    uint32_t measurementHeapStart_ = 0;
    bool taskPassed_[4] = {};
    bool taskExecuted_[4] = {};
    char taskDetail_[4][224] = {};
    struct TaskSnapshot {
        bool valid = false;
        PlayerSnapshot player{};
        LibraryStats library{};
        uint32_t elapsedMs = 0;
        uint32_t measurementElapsedMs = 0;
        uint32_t heapStart = 0;
        uint32_t heapNow = 0;
        uint32_t heapMinimum = 0;
        uint32_t playerServiceStartGapMaxUs = 0;
        uint32_t playerServiceStartGapOver100Ms = 0;
        uint32_t speakerRequestSlotEmptySamples = 0;
        uint32_t unexpectedPlaybackStateOver100Ms = 0;
        uint32_t inputUpdateMaxUs = 0;
        uint32_t playerRuntimeServiceMaxUs = 0;
        uint32_t libraryRuntimeServiceMaxUs = 0;
        uint32_t gateServiceMaxUs = 0;
        uint32_t loopBodyMaxUs = 0;
        uint32_t playerGapPreviousPlayerRuntimeUs = 0;
        uint32_t playerGapPreviousLibraryRuntimeUs = 0;
        uint32_t playerGapPreviousGateUs = 0;
        uint32_t playerGapPreviousLoopBodyUs = 0;
        uint32_t playerGapCurrentInputUs = 0;
        char measurementTrack[kTrackPathCapacity] = "none";
        char gapFromPhase[40] = "none";
        char gapToPhase[40] = "none";
    };
    TaskSnapshot taskSnapshots_[4]{};
    size_t primaryFailureTaskIndex_ = 4;
    char primaryFailurePhase_[48] = "none";
    bool autoRecentTimingPassed_ = false;
    char playbackPath_[kTrackPathCapacity] = {};
    char pendingLoopPhaseFrom_[40] = "none";
    char lastLoopPhaseFrom_[40] = "none";
    char lastLoopPhaseTo_[40] = "none";
    char playerServiceStartGapMaxFromPhase_[40] = "none";
    char playerServiceStartGapMaxToPhase_[40] = "none";
    char manifestSha256_[65] = "unverified";
    char failure_[96] = "none";
};

}  // namespace player
}  // namespace adv_walkman
