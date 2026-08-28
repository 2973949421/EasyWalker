#pragma once

#include <cstdint>

#include "UiTypes.h"
#include "InputEdges.h"

namespace adv_walkman {
namespace player {

// Physical positions are UI buttons; no Fn requirement or PC Arrow decoding.
class InputRouter final {
  public:
    bool poll(UiAction& action, RawKeyEvent& raw, bool playerPage=false);
    static uint64_t physicalMask();
    bool pollMask(uint64_t mask,uint32_t now,UiAction& action,RawKeyEvent& raw,bool playerPage);
    void capture(uint64_t mask,uint32_t now,uint32_t epoch,bool suppressed){edges_.observe(mask,now,epoch,suppressed);}
    bool pop(uint32_t epoch,UiAction& action,RawKeyEvent& raw,bool playerPage);
    uint32_t overflow()const{return edges_.overflow;}
    uint32_t stale()const{return edges_.stale;}

  private:
    InputEdges edges_;
};

}  // namespace player
}  // namespace adv_walkman
