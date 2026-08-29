#include "InputRouter.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include "P4Controls.h"
namespace adv_walkman { namespace player {
bool InputRouter::poll(UiAction& action,RawKeyEvent& raw,UiPage page){
    return pollMask(physicalMask(),millis(),action,raw,page);
}
uint64_t InputRouter::physicalMask(){uint64_t mask=0;
    for(const auto& p:M5Cardputer.Keyboard.keyList())if(p.x>=0&&p.x<14&&p.y>=0&&p.y<4)mask|=uint64_t(1)<<(p.y*14+p.x);
    return mask;
}
bool InputRouter::pollMask(uint64_t mask,uint32_t now,UiAction& action,RawKeyEvent& raw,UiPage page){
    capture(mask,now,0,false);return pop(0,action,raw,page);
}
bool InputRouter::pop(uint32_t epoch,UiAction& action,RawKeyEvent& raw,UiPage page){
    action=UiAction::None;raw=RawKeyEvent{};
    InputEdges::Event event;if(!edges_.pop(epoch,event))return false;
    const uint8_t key=event.key;raw.capturedAtMs=event.at;
    raw.x=key%14;raw.y=key/14;raw.fn=false;raw.keyCount=1;
    action=routedActionAt(page,raw.x,raw.y);
    return action!=UiAction::None;
}
} }
