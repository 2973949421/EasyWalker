#pragma once
#include <cstddef>
namespace adv_walkman { namespace player {
// Visual slots are independent instances, even when they share one directory.
constexpr size_t wheelIndex(size_t current,size_t count,unsigned slot){
    return !count?0:slot==0?(current+count-1)%count:slot==2?(current+1)%count:current;
}
constexpr int kLibraryNameTop=174,kLibraryNameHeight=22,kLibraryWheelTop=196;
constexpr unsigned wheelAnimationStep(unsigned current,unsigned elapsed){
    return current==3||elapsed>=160?3:elapsed*3/160;
}
} }
