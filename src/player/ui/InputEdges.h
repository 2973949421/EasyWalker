#pragma once
#include <cstdint>
#if __cplusplus >= 201402L
#define ADV_INPUT_CX constexpr
#else
#define ADV_INPUT_CX
#endif
namespace adv_walkman { namespace player {
class InputEdges {
 public:
    struct Event { uint32_t at=0,epoch=0; uint8_t key=255; };
    // Preserve ordered press/release events, including taps shorter than a poll.
    ADV_INPUT_CX void observe(uint64_t mask,uint32_t now,uint32_t epoch=0,bool suppressed=false) {
        if(raw_ && !(raw_&mask)) finish();
        if(!mask){finish();raw_=0;blocked_=false;return;}
        if(!raw_){at_=now;epoch_=epoch;sent_=false;blocked_=suppressed;key_=255;
            if(!(mask&(mask-1)))for(unsigned i=0;i<56;++i)if(mask&(uint64_t(1)<<i))key_=i;
        }
        if(suppressed || (mask&(mask-1)) || (raw_ && mask!=raw_))blocked_=true;
        raw_=mask;
        if(!sent_&&!blocked_&&uint32_t(now-at_)>=25)emit();
    }
    ADV_INPUT_CX bool pop(uint32_t epoch,Event& out){
        while(count_){out=queue_[head_];head_=(head_+1)%16;--count_;
            if(out.epoch==epoch)return true;++stale;}
        return false;
    }
    ADV_INPUT_CX bool sample(uint64_t mask,uint32_t now,uint8_t& key){observe(mask,now);Event e;if(!pop(0,e))return false;key=e.key;return true;}
    uint32_t overflow=0,stale=0;
 private:
    Event queue_[16]{};uint8_t head_=0,count_=0,key_=255;
    uint64_t raw_=0;uint32_t at_=0,epoch_=0;bool blocked_=false,sent_=false;
    ADV_INPUT_CX void emit(){
        if(sent_||blocked_||key_==255)return;sent_=true;
        if(count_==16){++overflow;head_=count_=0;blocked_=true;return;}
        auto& e=queue_[(head_+count_)%16];e.at=at_;e.epoch=epoch_;e.key=key_;++count_;
    }
    ADV_INPUT_CX void finish(){emit();raw_=0;key_=255;sent_=false;}
};
inline ADV_INPUT_CX bool checkInputEdges(){
    InputEdges e;uint8_t k=255;const uint64_t a=1ULL<<10,b=1ULL<<12;
    if(e.sample(a,0,k)||e.sample(a,24,k)||!e.sample(a,25,k)||k!=10)return false;
    if(e.sample(a,1000,k)||e.sample(0,1001,k))return false;
    e.sample(b,1100,k);if(!e.sample(0,1102,k)||k!=12)return false;
    e.sample(a|b,1200,k);if(e.sample(0,1226,k))return false;
    e.observe(a,1300,1);e.observe(0,1301,1);InputEdges::Event event;
    return !e.pop(2,event)&&e.stale==1;
}
} }
