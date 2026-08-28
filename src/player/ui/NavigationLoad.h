#pragma once
#include <cstdint>
#if __cplusplus >= 201402L
#define ADV_NAV_CONSTEXPR constexpr
#else
#define ADV_NAV_CONSTEXPR
#endif

namespace adv_walkman { namespace player {
enum class NavigationState : uint8_t { Idle, Loading, Ready, Error };
enum class NavigationObservation : uint8_t { Pending, Ready, Error };

// Production state machine; no filesystem/UI dependency. A callback from an
// old request cannot complete a newer one. Progress, not repeated polls,
// refreshes the timeout (including across millis() wrap).
struct NavigationLoad {
    uint32_t generation = 0, progress = 0, progressedAt = 0;
    NavigationState state = NavigationState::Idle;
    bool stalled = false;
    ADV_NAV_CONSTEXPR uint32_t begin(uint32_t now) {
        ++generation; progress = 0; progressedAt = now;
        state = NavigationState::Loading; stalled = false; return generation;
    }
    ADV_NAV_CONSTEXPR void cancel() { ++generation; state = NavigationState::Idle; }
    ADV_NAV_CONSTEXPR void observe(uint32_t request, uint32_t now, uint32_t work,
                           NavigationObservation result) {
        if (request != generation || state != NavigationState::Loading) return;
        if (work != progress) { progress = work; progressedAt = now; }
        if (result == NavigationObservation::Ready) state = NavigationState::Ready;
        else if (result == NavigationObservation::Error) state = NavigationState::Error;
        else if (uint32_t(now - progressedAt) >= 5000) {
            state = NavigationState::Error; stalled = true;
        }
    }
};
} }
#undef ADV_NAV_CONSTEXPR
