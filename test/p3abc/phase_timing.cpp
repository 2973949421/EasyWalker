// Compile-time execution of the SAME timeout predicate used by the firmware.
// No Arduino emulation or new native compiler is required.
#include "player/p3abc/GatePhaseTiming.h"
using namespace adv_walkman::player;

constexpr bool evaluate(GatePhaseStamp entered, GatePhaseStamp current,
                        uint32_t entryTick, uint32_t checkedTick, bool eligible) {
#ifdef ADV_GATE_REPRODUCE_OLD_TIMER
    return eligible && uint32_t(entryTick-current.startedAtMs)>45000;
#else
    return gatePhaseTimedOut(entered,current,checkedTick,eligible);
#endif
}

// FileQuota -> Preflight, including the observed old-now/new-start bug.
static_assert(!evaluate({1,90},{2,101},100,101,true), "phase transition underflow regression");
static_assert(!evaluate({1,90},{2,101},100,108,true), "work can cross a millisecond");
static_assert(!evaluate({1,90},{2,100},100,100,true), "same-tick transition");
static_assert(!evaluate({2,90},{2,101},100,101,true), "same-phase re-entry resets deadline");
static_assert(!evaluate({2,101},{2,101},102,102,true), "next service starts normally");
static_assert(!evaluate({2,101},{2,101},45101,45101,true), "45 second boundary unchanged");
static_assert(evaluate({2,101},{2,101},45102,45102,true), "real timeout remains enforced");
static_assert(evaluate({2,101},{2,101},45100,45102,true), "include time spent in current work");
static_assert(!evaluate({2,0},{2,0},60000,60000,false), "human/measurement/terminal phases exempt");
static_assert(!evaluate({2,0xFFFFFFF0U},{2,0xFFFFFFF0U},15,16,true), "normal clock wrap is not a timeout");
static_assert(!evaluate({2,0xFFFFFFF0U},{2,0xFFFFFFF0U},44984,44984,true), "wrapped 45 second boundary");
static_assert(evaluate({2,0xFFFFFFF0U},{2,0xFFFFFFF0U},44985,44985,true), "real timeout across wrap");
static_assert(kGatePhaseTimeoutMs==45000, "do not loosen the phase timeout");
