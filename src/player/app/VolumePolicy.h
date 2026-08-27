#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
// User-calibrated range for the current ADV/headphones, not an SPL guarantee.
// UI retains 0..255 / 0..100%; physical Speaker volume is capped below the
// original 40% setting. No old high volume is restored from session state.
struct VolumePolicy {
    static constexpr uint8_t maximumRaw = 102;
    static constexpr uint8_t initialLevel = 80;
    static constexpr uint8_t toRaw(uint8_t level) {
        return (unsigned(level) * maximumRaw + 127U) / 255U;
    }
};
static_assert(VolumePolicy::toRaw(80)==32 && VolumePolicy::toRaw(255)==102,"volume calibration");
} }
