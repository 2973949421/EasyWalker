#pragma once
#include <SD.h>
#include "player/ui/UiCoordinator.h"
namespace adv_walkman { namespace player {
// Observer only: never changes transport, view, cache, or persistence.
// Checkpoints are append-only, bounded writes with a CRC commit line. A torn
// tail is ignored by the host; the last complete checkpoint remains usable.
class FreeSession final {
 public:
    void begin();
    void action(UiAction action,const RawKeyEvent& raw,UiPage page,bool accepted);
    void observe(const UiCoordinator& ui,const PlayerRuntime& player);
    void service(UiCoordinator& ui,const PlayerRuntime& player,uint32_t uiBurstUs);
 private:
    void fail(const char* component,const char* reason);
    void prepare(const UiCoordinator& ui,const PlayerRuntime& player);
    void append(const char* format,...);
    fs::File file_;
    char buffer_[4096]{},track_[512]{},failure_[64]{},component_[24]{};
    char resourcePath_[560]{},resourceOperation_[24]{};
    struct Event {uint32_t ms;UiAction action;UiPage page;int8_t x,y;bool accepted;};
    Event events_[12]{};
    uint32_t eventCount_=0,actions_=0,nav_=0,volumeEvents_=0,playEvents_=0,viewEvents_=0;
    uint32_t started_=0,lastSaved_=0,sequence_=0,playingAt_=0,longestPlaying_=0;
    uint32_t loadAt_=0,loadingGeneration_=0,uiBurstMaxUs_=0,writeMaxUs_=0,minimumHeap_=UINT32_MAX;
    uint32_t pcmGapMaxUs_=0,backpressure_=0,audioErrors_=0,lyricFrames_=0,coverFrames_=0,deadlineUpdates_=0;
    uint32_t prepareMaxUs_=0,presentMaxUs_=0,lateMaxMs_=0,libraryWidth_=0;
    int resourceErrno_=0;
    int32_t resourceExpected_=-1,resourceActual_=-1;
    size_t length_=0,written_=0;
    bool playing_=false,loading_=false,requestSave_=true,manual_=false,writingManual_=false;
    bool inputCheck_=false,textSeen_=false,textOk_=false,lyricsReady_=false,coverReady_=false,overflow_=false;
};
} }
