#pragma once
#include <cstdint>
#if __cplusplus >= 201402L
#define ADV_DISPLAY_CX constexpr
#else
#define ADV_DISPLAY_CX
#endif
namespace adv_walkman { namespace player {
struct DisplayPreferences {
    uint8_t brightness=70, playerTimeout=3, otherTimeout=1;
};
constexpr uint32_t displayTimeoutMs(uint8_t index) {
    return index==0?15000:index==1?30000:index==2?60000:index==3?180000:
           index==4?300000:index==5?600000:0;
}
constexpr bool validDisplayPreferences(const DisplayPreferences& p) {
    return p.brightness>=10 && p.brightness<=100 && p.brightness%10==0 && p.playerTimeout<=6 && p.otherTimeout<=6;
}
constexpr uint32_t displayRecordU32(const uint8_t* b){return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
inline ADV_DISPLAY_CX uint32_t displayRecordCrc(const uint8_t* b){uint32_t crc=~0U;
    for(unsigned i=0;i<20;++i){crc^=b[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xEDB88320U:0U);}return crc^~0U;}
inline ADV_DISPLAY_CX bool validDisplayRecord(const uint8_t* b){
    return b[0]=='D'&&b[1]=='S'&&b[2]=='P'&&b[3]=='L'&&b[4]==1&&b[5]==0&&b[6]==24&&b[7]==0&&displayRecordU32(b+12)==4&&
        b[16]>=10&&b[16]<=100&&b[16]%10==0&&b[17]<=6&&b[18]<=6&&b[19]==0&&displayRecordCrc(b)==displayRecordU32(b+20);}
constexpr bool newerDisplayGeneration(uint32_t candidate,uint32_t current){return int32_t(candidate-current)>0;}
// Raw physical activity is examined BEFORE action filtering. A waking chord
// remains swallowed until every key has been released and debounced.
class ScreenPowerController {
 public:
    ADV_DISPLAY_CX void begin(uint32_t now,bool player) {last_=changed_=now;player_=player;}
    ADV_DISPLAY_CX bool sample(uint64_t mask,uint32_t now,bool player,const DisplayPreferences& p) {
        if(player!=player_){player_=player;last_=now;}
        if(mask!=raw_){raw_=mask;changed_=now;}
        if(raw_!=stable_ && uint32_t(now-changed_)>=25){
            const uint64_t pressed=raw_&~stable_;stable_=raw_;
            if(pressed){last_=now;if(asleep_){asleep_=false;swallow_=true;lastWakeMask=mask;++wakes;}}
            if(!stable_)swallow_=false;
        }
        const uint32_t timeout=displayTimeoutMs(player?p.playerTimeout:p.otherTimeout);
        if(!asleep_ && !raw_ && !swallow_ && timeout && uint32_t(now-last_)>=timeout){asleep_=true;++sleeps;}
        return !asleep_&&!swallow_;
    }
    constexpr bool asleep()const{return asleep_;}
    constexpr bool swallowing()const{return swallow_;}
    uint32_t sleeps=0,wakes=0;
    uint64_t lastWakeMask=0;
 private:
    uint64_t raw_=0,stable_=0;
    uint32_t last_=0,changed_=0;
    bool player_=false,asleep_=false,swallow_=false;
};
} }
