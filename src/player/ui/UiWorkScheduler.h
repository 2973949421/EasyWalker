#pragma once

#include <cstdint>

namespace adv_walkman { namespace player {

enum class UiScheduledWork : uint8_t {
    Render,
    BrowserModel,
    PendingIntent,
    FontIo,
    PlayerMedia,
    LibraryMedia,
};

struct UiWorkSnapshot {
    bool player=false;
    bool library=false;
    bool browserReady=false;
    bool pendingIntent=false;
    bool fontBusy=false;
    bool playerBandWaiting=false;
    bool libraryBandWaiting=false;
    bool libraryLoading=false;
};

constexpr UiScheduledWork chooseUiWork(const UiWorkSnapshot& value,bool playerMediaTurn,bool libraryMediaTurn){
    return value.pendingIntent?UiScheduledWork::PendingIntent:
        (!value.player&&!value.browserReady)?UiScheduledWork::BrowserModel:
        value.player?(value.playerBandWaiting||playerMediaTurn?UiScheduledWork::PlayerMedia:UiScheduledWork::Render):
        value.libraryBandWaiting?UiScheduledWork::LibraryMedia:
        value.fontBusy?UiScheduledWork::FontIo:
        value.library&&value.libraryLoading&&libraryMediaTurn?UiScheduledWork::LibraryMedia:
        UiScheduledWork::Render;
}

// The visible model always wins over an obsolete resource.  In particular a
// ready old Library band can never prevent the new selection from being built.
class UiWorkScheduler final {
 public:
    UiScheduledWork choose(const UiWorkSnapshot& value){
        if(value.player&&!value.playerBandWaiting)playerMediaTurn_=!playerMediaTurn_;
        if(value.library&&value.libraryLoading&&!value.libraryBandWaiting)libraryMediaTurn_=!libraryMediaTurn_;
        return chooseUiWork(value,playerMediaTurn_,libraryMediaTurn_);
    }
 private:
    bool playerMediaTurn_=false;
    bool libraryMediaTurn_=false;
};

} }
