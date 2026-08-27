#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
// User-calibrated range for the current ADV/headphones, not an SPL guarantee.
// UI retains 0..255 / 0..100%; physical Speaker volume is capped below the
// previous ~25% setting. No old high volume is restored from session state.
struct VolumePolicy {
    static constexpr uint8_t maximumRaw = 63;
    static constexpr uint8_t initialLevel = 128;
    static constexpr uint8_t toRaw(uint8_t level) {
        return (unsigned(level) * maximumRaw + 127U) / 255U;
    }
};
} }
