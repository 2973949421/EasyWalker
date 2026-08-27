#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
class InputEdges {
 public:
    bool sample(uint64_t mask,uint32_t now,uint8_t& key) {
        uint64_t rising=0;
        for(unsigned i=0;i<56;++i){const uint64_t bit=uint64_t(1)<<i;
            if((mask^raw_)&bit)changed_[i]=now;
            if(((mask^stable_)&bit) && uint32_t(now-changed_[i])>=25){
                stable_^=bit;if(stable_&bit)rising|=bit;
            }
        }
        raw_=mask;
        if(!rising || !mask || (mask&(mask-1)) || mask!=stable_)return false;
        for(unsigned i=0;i<56;++i)if(rising&(uint64_t(1)<<i)){key=i;return true;}
        return false;
    }
 private:uint64_t raw_=0,stable_=0;uint32_t changed_[56]{};
};
inline bool checkInputEdges(){
    InputEdges edges;uint8_t key=255;const uint64_t a=uint64_t(1)<<10,b=uint64_t(1)<<12;
    if(edges.sample(a,0,key)||edges.sample(a,24,key)||!edges.sample(a,25,key)||key!=10)return false;
    if(edges.sample(a,1000,key))return false;
    if(edges.sample(b,1001,key)||!edges.sample(b,1026,key)||key!=12)return false;
    if(edges.sample(a|b,1030,key)||edges.sample(a|b,1055,key))return false;
    if(edges.sample(a,1060,key)||edges.sample(a,1085,key))return false;
    edges.sample(0,1100,key);edges.sample(0,1125,key);
    return !edges.sample(a,1200,key)&&edges.sample(a,1225,key)&&key==10;
}
} }
