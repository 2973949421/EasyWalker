#include "InputRouter.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include "PlayerKeys.h"
namespace adv_walkman { namespace player {
bool InputRouter::poll(UiAction& action,RawKeyEvent& raw,bool playerPage){
    return pollMask(physicalMask(),millis(),action,raw,playerPage);
}
uint64_t InputRouter::physicalMask(){uint64_t mask=0;
    for(const auto& p:M5Cardputer.Keyboard.keyList())if(p.x>=0&&p.x<14&&p.y>=0&&p.y<4)mask|=uint64_t(1)<<(p.y*14+p.x);
    return mask;
}
bool InputRouter::pollMask(uint64_t mask,uint32_t now,UiAction& action,RawKeyEvent& raw,bool playerPage){
    capture(mask,now,0,false);return pop(0,action,raw,playerPage);
}
bool InputRouter::pop(uint32_t epoch,UiAction& action,RawKeyEvent& raw,bool playerPage){
    action=UiAction::None;raw=RawKeyEvent{};
    InputEdges::Event event;if(!edges_.pop(epoch,event))return false;
    const uint8_t key=event.key;raw.capturedAtMs=event.at;
    raw.x=key%14;raw.y=key/14;raw.fn=false;raw.keyCount=1;
    const auto at=[&](int x,int y){return raw.x==x&&raw.y==y;};
    if(at(0,0))action=UiAction::Back;
    else if(at(0,1))action=UiAction::ToggleCurrentPlaybackPage;
    else if(at(5,1))action=UiAction::SaveDiagnostics; // T: development log checkpoint, not a product shortcut.
    else if(playerPage){
        switch(playerKeyAt(raw.x,raw.y)){
            case PlayerKey::VolumeUp:action=UiAction::VolumeUp;break;
            case PlayerKey::VolumeDown:action=UiAction::VolumeDown;break;
            case PlayerKey::TogglePlayback:action=UiAction::TogglePlayback;break;
            case PlayerKey::View:action=UiAction::ToggleView;break;
            case PlayerKey::None:break;
        }
    }
    else if(at(13,2))action=UiAction::Confirm;
    else if(!playerPage){
        if(at(11,2))action=UiAction::Up;else if(at(10,3))action=UiAction::Left;
        else if(at(11,3))action=UiAction::Down;else if(at(12,3))action=UiAction::Right;
        else if(at(3,2))action=UiAction::OpenSettings;
    }
    return action!=UiAction::None;
}
} }
