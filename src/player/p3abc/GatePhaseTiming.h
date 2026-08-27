#pragma once
#include <cstdint>

namespace adv_walkman { namespace player {
struct GatePhaseStamp {
    uint8_t phase;
    uint32_t startedAtMs;
};

constexpr uint32_t kGatePhaseTimeoutMs = 45000;

// checkedAtMs must be sampled AFTER the work/transition. Never subtract a
// newly recorded start from the service-entry timestamp. A changed phase is
// first checked on its next service call. Unsigned subtraction preserves
// normal millis() wraparound for these short (<5 minute) intervals.
constexpr bool gatePhaseTimedOut(GatePhaseStamp entered, GatePhaseStamp current,
                                uint32_t checkedAtMs, bool eligible) {
    return eligible && entered.phase == current.phase &&
           entered.startedAtMs == current.startedAtMs &&
           uint32_t(checkedAtMs - current.startedAtMs) > kGatePhaseTimeoutMs;
}
} }
