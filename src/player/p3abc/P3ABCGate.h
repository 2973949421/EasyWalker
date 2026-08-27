#pragma once
#include "player/p3a/P3AGate.h"
#include "player/ui/P3BChecks.h"
#include <cstdio>

namespace adv_walkman { namespace player {
class P3ABCGate final {
  public:
    void begin(UiCoordinator& ui,PlayerRuntime& player,M5GFX& display);
    bool beforeAction(UiAction action,const RawKeyEvent& raw,UiCoordinator& ui,PlayerRuntime& player);
    void service(UiCoordinator& ui,PlayerRuntime& player);
    bool renderResult(M5GFX& display);
    bool suspendBackground() const {return phase_==Phase::FileQuota;}
  private:
    enum class Phase:uint8_t { Disabled,FileQuota,Preflight,NavigationCard,A,IntroCard,PrepareIntro,Intro,PrepareLyrics,Lyrics,
        WaitView,Cover,PrepareMeasure,Warmup,Measure,PauseNotice,PauseCheck,AudioConfirm,
        SeekNotice,SeekCheck,AuxNotice,AuxWait,ReturnWait,SaveNotice,Save,
        RebootCheck,Passed,Failed };
    struct SingleSource:TrackSource {
        const char* path;
        explicit SingleSource(const char* p):path(p){}
        size_t count() const override {return 1;}
        bool pathAt(size_t index,char* output,size_t capacity)const override;
    };
    P3AGate a_;
    P3BValidation b_;
    P3BCheckResult model_,drawing_,overlay_;
    SingleSource benchmark_{"/Music/ADVWalkmanBenchmark/benchmark.mp3"};
    SingleSource auxiliary_{"/Music/ADVWalkmanP3Test/no-lyrics.mp3"};
    Phase phase_=Phase::Disabled;
    uint32_t phaseAt_=0,autoMs_=0,lastAt_=0,windowAt_=0,frameBaseline_=0,readBaseline_=0;
    uint32_t positionAt_=0,bufferAt_=0,viewBase_=0,minimumHeap_=UINT32_MAX,heapAt_=0;
    uint32_t pausedPosition_=0,pausedPage_=0,measuredFrames_=0,measuredReads_=0;
    uint32_t measuredMs_=0;
    uint8_t pulse_=0;
    bool modelRan_=false,bRan_=false,cRan_=false,displayOk_=false,lyricsOk_=false,coverOk_=false;
    bool pauseOk_=false,seekOk_=false,fallbackOk_=false,rebootOk_=false;
    bool resultDrawn_=false,restoredRun_=false,viewKeyOk_=false;
    bool audioUserConfirmed_=false;
    bool preflightOk_=false,quotaOk_=false,inputOk_=false,measurementStarted_=false;
    bool aStarted_=false,haveFailure_=false;
    uint8_t quotaCount_=0,preflightGlyph_=0;
    int quotaErrno_=0;
    FILE* quotaFiles_[13]{};
    uint16_t width_=0,height_=0;uint8_t rotation_=0;
    const char* reason_="none";
    const char* failureComponent_="none";
    Phase failurePhase_=Phase::Disabled;
    ResourceFailure resourceFailure_{};
    char resourcePath_[576]{},failureTrack_[512]{};
    PlayerSnapshot failureAudio_{};
    NowPlayingMediaStatus failureMedia_{};
    FontCacheStats failureFont_{};
    PlayerSnapshot measured_{};
    NowPlayingMediaStatus media_{};
    FontCacheStats font_{};
    void transition(Phase phase,UiCoordinator& ui,const char* hint);
    static const char* phaseName(Phase phase);
    bool checkResources(UiCoordinator& ui,PlayerRuntime& player,bool requireReal);
    void captureFailure(UiCoordinator& ui,PlayerRuntime& player);
    void closeQuotaFiles();
    bool waitsForHuman() const;
    void fail(const char* reason,UiCoordinator& ui,PlayerRuntime& player);
    bool writeCLog(const char* result,UiCoordinator& ui,PlayerRuntime& player);
    RawKeyEvent viewRaw_{};
    void writeSkipped(const char* path,const char* reason);
};
} }
