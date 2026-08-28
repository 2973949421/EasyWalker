#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
constexpr uint8_t promoteFramePins(uint8_t pins) {return (pins&4)|((pins&1)?2:0);}
constexpr uint8_t currentTextSlot(int first,int second,int wanted) {
    return first==wanted?0:second==wanted?1:0;
}
} }
