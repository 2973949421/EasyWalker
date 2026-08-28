#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
// A loading placeholder is not the owner of the validator's file.
constexpr bool mayCloseCoverAfterFrame(uint8_t validationPhase){return validationPhase==0;}
constexpr bool validCoverDimensions(unsigned w,unsigned h,uint32_t payload){
    return w>0&&w<=135&&h>0&&h<=188&&payload==w*h*2;
}
constexpr unsigned coverRowsPerRead(unsigned width){return width?512/(width*2):0;}
} }
