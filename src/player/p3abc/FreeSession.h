#pragma once
#include <SD.h>
#include <algorithm>
#include "player/ui/UiCoordinator.h"
#include "SaveTransaction.h"
namespace adv_walkman { namespace player {
// Observer only: never changes transport, view, cache, or persistence.
// Checkpoints are append-only, bounded writes with a CRC commit line. A torn
// tail is ignored by the host; the last complete checkpoint remains usable.
class FreeSession final {
    enum class StreamKind:uint8_t{None,Summary,Full,SaveBegin,SaveEnd};
    enum class ManualPhase:uint8_t{None,Begin,WaitState,Full,End};
    enum class SaveFailureStage:uint8_t{None,DisplayOpen,DisplayWrite,DisplayFlush,DisplayVerifyOpen,DisplayVerify,
        Snapshot,LogOpen,LogWrite,LogFlush,Timeout,PlayerBase=32};
 public:
    void begin();
    void action(UiAction action,const RawKeyEvent& raw,UiPage page,bool accepted,
                uint32_t pcmBefore,const PlayerSnapshot& after);
    void observe(const UiCoordinator& ui,const PlayerRuntime& player);
    void service(UiCoordinator& ui,const PlayerRuntime& player,uint32_t uiBurstUs);
    bool workDue()const{return bool(file_)||logPrepared_||requestSave_||save_.pending()||millis()-lastSaved_>=60000;}
    bool storageIdle()const{return !file_&&!logPrepared_;}
    uint32_t requestManualSave(uint32_t playerRevision,uint32_t displayRevision,UiPage page,uint32_t now){
        latestSavePage_=uint8_t(page);const uint32_t ticket=save_.request(now,playerRevision,displayRevision);
        if(!save_.active()&&save_.begin(now)){activeSavePage_=latestSavePage_;manualPhase_=ManualPhase::Begin;}
        return ticket;
    }
    bool manualSavePending()const{return save_.pending();}
    const SaveTransaction& saveTransaction()const{return save_.transaction();}
    bool lastSaveOk()const{return lastSaveOk_;}
    void recordBurst(uint32_t){}
    void recordWork(uint32_t audio,uint32_t,uint32_t){audioMax_=std::max(audioMax_,audio);}
 private:
    void fail(const char* component,const char* reason);
    void beginStream(StreamKind kind,const UiCoordinator& ui,const PlayerRuntime& player);
    void prepareNext(const UiCoordinator& ui,const PlayerRuntime& player);
    void finishStream(UiCoordinator& ui,const PlayerRuntime& player,bool logOk);
    void startNextTicket(uint32_t now);
    static const char* outcomeName(SaveOutcome outcome);
    static const char* saveFailureName(SaveFailureStage stage);
    void append(const char* format,...);
    fs::File file_;
    // Checkpoints are generated section-by-section.  The only formatting
    // scratch is one KiB and no complete log record is retained in RAM.
    char buffer_[1024]{},track_[512]{},failure_[64]{},component_[24]{};
    struct MediaError { ResourceFailure failure;char path[560]{};uint32_t generation=0,at=0,count=0; } mediaErrors_[4];
    char firstNavigationReason_[64]{};
    uint32_t lastErrorGeneration_[2]={UINT32_MAX,UINT32_MAX};
    uint32_t pcmPeakAt_=0,pcmPeakGeneration_=0;
    UiPage pcmPeakPage_=UiPage::Library;
    char pcmPeakTrack_[512]{},lyricPeakTrack_[512]{};
    uint32_t lyricPeakGeneration_=0,lyricPeakDue_=0,lyricPeakPrepared_=0,lyricPeakSubmitted_=0;
    char restoredTrack_[512]{};
    uint32_t bootId_=1,audioMax_=0,restoredPosition_=0;
    uint32_t noLyricsView_=0,preferenceTransitions_=0,startupObservedMs_=0;
    uint8_t restoredView_=0,previousPreferred_=0;
    bool startupCaptured_=false,startupPaused_=false,startupSilent_=true,noLyricsPending_=false;
    char resourcePath_[560]{},resourceOperation_[24]{};
    struct Event {uint32_t ms;UiAction action;UiPage page;int8_t x,y;bool accepted;uint32_t captured;};
    Event events_[32]{};
    uint32_t eventCount_=0,actions_=0,nav_=0,volumeEvents_=0,playEvents_=0,viewEvents_=0;
    uint32_t previousRequests_=0,nextRequests_=0,playModeRequests_=0;
    uint32_t transportFirstPcmMaxMs_=0,transportPcmBaseline_=0,transportAcceptedAt_=0;
    uint32_t transportPcmCompleted_=0,transportPcmSuperseded_=0,transportPaused_=0;
    size_t lastTransportIndex_=0;
    uint8_t lastTransportState_=uint8_t(PlayerState::Empty);
    bool transportPcmPending_=false;
    uint32_t started_=0,lastSaved_=0,sequence_=0,playingAt_=0,longestPlaying_=0;
    uint32_t loadAt_=0,loadingGeneration_=0,minimumHeap_=UINT32_MAX;
    uint32_t pcmGapMaxUs_=0,backpressure_=0,audioErrors_=0,lyricFrames_=0,coverFrames_=0,deadlineUpdates_=0;
    uint32_t prepareMaxUs_=0,presentMaxUs_=0,lateMaxMs_=0,libraryWidth_=0;
    int resourceErrno_=0;
    int32_t resourceExpected_=-1,resourceActual_=-1;
    size_t length_=0,written_=0;
    bool playing_=false,loading_=false,requestSave_=true,failureSnapshotPending_=false;
    bool inputCheck_=false,textSeen_=false,textOk_=false,lyricsReady_=false,coverReady_=false,overflow_=false;
    uint32_t lastPageCount_=0,lastSelections_=0,lastNavigationErrors_=0;
    bool lastSaveOk_=false,logPrepared_=false,logFlushed_=false,logWriteOk_=true;
    bool streamFinalChunk_=false;
    uint8_t streamSection_=0;
    StreamKind streamKind_=StreamKind::None;
    ManualPhase manualPhase_=ManualPhase::None;
    SaveOutcome pendingOutcome_=SaveOutcome::Succeeded;
    SaveFailureStage saveFailureStage_=SaveFailureStage::None;
    uint8_t latestSavePage_=uint8_t(UiPage::Player),activeSavePage_=uint8_t(UiPage::Player);
    uint32_t streamCrc_=~0U;
    CheckpointCoordinator save_{};
};
} }
