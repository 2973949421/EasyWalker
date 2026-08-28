#pragma once
#include "CoverRenderer.h"
#include "LyricsRenderer.h"
#include "ViewTransition.h"
namespace adv_walkman { namespace player {
struct NowPlayingMediaStatus {
    MediaState lyrics=MediaState::Idle,cover=MediaState::Idle;
    MediaView preferred=MediaView::Lyrics,view=MediaView::Cover;
    int current=-1;
    uint32_t frames=0,viewChanges=0,cancellations=0,reads=0,serviceMaxUs=0;
    uint32_t lyricsFrames=0,coverFrames=0;
    uint32_t generation=0,prepareMaxUs=0,presentMaxUs=0,lyricLateMaxMs=0,presentIoViolations=0;
    uint32_t frameId=0,deadlineUpdates=0,missedDeadlines=0;
    uint32_t lyricDueMs=0,lyricPreparedMs=0,lyricSubmittedMs=0,coverOpens=0;
    uint8_t page=0,pages=1;bool layoutError=false,invalidUtf8=false;
    uint8_t peakWork=0;
    const char* error="none";
    uint32_t viewCoalesced=0,viewWarmMaxMs=0,viewColdMaxMs=0,viewFailures=0;
    bool viewPending=false;
    uint32_t viewWarmCompleted=0,viewColdCompleted=0;
    const char* viewFailure="none";
    uint32_t viewRequestedMs=0,viewReadyMs=0,viewFirstStripeMs=0,viewCompletedMs=0;
};
class NowPlayingMedia final {
  public:
    void begin(FontCache& fonts){fonts_=&fonts;}
    void selectTrack(const char* path);void release();void service();
    void suspend();
    const char* bootSelfCheck(const char* track); // muted current resource check
    void updatePosition(uint32_t positionMs,uint32_t durationMs,bool paused);
    void resetDiagnostics();
    void setPreferred(uint8_t value);bool toggleView();void requestRedraw();
    bool wantsFrame(uint32_t nowMs)const;bool beginFrame(uint32_t nowMs);
    bool prepareStripe(lgfx::LGFXBase& canvas,int y,int height);void drawStripe(lgfx::LGFXBase& canvas,int y,int height);void endFrame();
    bool bandActive()const{return cover_.bandActive();}
    void finishStripe(){cover_.finishBand();}
    void stripeSubmitted(){if(transition_.pending&&frameView_==transition_.requested&&!viewFirstStripeMs_)viewFirstStripeMs_=millis();}
    int stripeHeight()const{return frameView_==MediaView::Cover?cover_.stripeHeight():18;}
    NowPlayingMediaStatus status()const;
    const LyricsTimeline& timeline()const{return timeline_;}
    const CoverRenderer& cover()const{return cover_;}
    bool frameInProgress()const{return frameInProgress_;}
    bool canPatchOverlay()const;
    bool presenting()const{return frameInProgress_;}
    bool presentingLyrics()const{return frameInProgress_&&frameView_==MediaView::Lyrics;}
    MediaView frameView()const{return frameView_;}
    MediaView requestedView()const{return effectiveView();}
  private:
    FontCache* fonts_=nullptr;LyricsTimeline timeline_;CoverRenderer cover_;LyricsRenderer renderer_;
    LyricsRenderer displayedRenderer_;
    char track_[512]{};
    bool frameFromPrepared_=false;
    bool preparationTurn_=false;
    uint32_t preparedReadyAt_=0,lastDue_=0,lastPrepared_=0,lastSubmitted_=0;
    MediaView preferred_=MediaView::Lyrics,frameView_=MediaView::Cover;
    ViewTransition transition_;
    uint32_t transitionAt_=0,transitionProgressAt_=0,transitionProgress_=0;
    uint32_t viewReadyMs_=0,viewFirstStripeMs_=0,viewCompletedMs_=0;
    uint32_t viewWarmMaxMs_=0,viewColdMaxMs_=0,viewFailures_=0;
    uint32_t viewWarmCompleted_=0,viewColdCompleted_=0;
    const char* viewFailure_="none";
    bool transitionWarm_=false;
    uint32_t positionMs_=0,durationMs_=0,shownCoverRevision_=UINT32_MAX,generation_=0,shownGeneration_=UINT32_MAX;
    unsigned workerTurn_=0;
    uint8_t peakWork_=0;
    uint32_t frames_=0,viewChanges_=0,cancellations_=0,serviceMaxUs_=0;
    uint32_t lyricsFrames_=0,coverFrames_=0;
    uint32_t prepareAt_=0,presentAt_=0,prepareMaxUs_=0,presentMaxUs_=0,lyricLateMaxMs_=0,ioAt_=0,presentIoViolations_=0;
    uint32_t preparedPosition_=0,preparedDue_=0,preparedUntil_=0;
    uint32_t frameId_=0,deadlineUpdates_=0,missedDeadlines_=0;
    int shownCurrent_=-2,preparedCue_=-2;
    uint8_t shownPage_=255,shownPages_=1;
    bool active_=false,dirty_=true,paused_=true,frameInProgress_=false,seek_=false;
    bool preparing_=false,layoutReady_=false,glyphsReady_=false,deadline_=false,redrawAfterFrame_=false;
    void cancelPreparation();void prepareUpcoming();
    // Loading is not missing. Keep Lyrics selected until presence is known.
    MediaView effectiveView()const{return timeline_.hasLyrics()||timeline_.state()==MediaState::Loading||timeline_.state()==MediaState::Idle?transition_.requested:MediaView::Cover;}
};
// Media-owned stdio buffers, File wrappers and transient path/I/O overhead.
// The SDK's fixed global FatFs mount table is reported separately by the Gate.
constexpr size_t kMediaFsReserve=7*1024;
constexpr size_t kMediaBudgetBytes=FontCache::workBytes()+LyricsTimeline::workBytes()+sizeof(NowPlayingMedia)+sizeof(FontCache)+kMediaFsReserve;
static_assert(kMediaBudgetBytes<=48*1024,"P3C media memory exceeds 48 KiB; do not enlarge the budget");
} }
