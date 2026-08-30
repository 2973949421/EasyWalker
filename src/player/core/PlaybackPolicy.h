#pragma once

#include "CoreTypes.h"

namespace adv_walkman {
namespace player {

// Automatic end-of-track behavior and explicit key navigation are separate
// product decisions.  A manual key always remains useful at queue boundaries.
constexpr bool manualTrackNavigationWraps() { return true; }
constexpr bool manualPreviousUsesHistory(bool shuffle) { return shuffle; }
constexpr bool automaticTrackEndWraps(RepeatMode repeat, bool shuffle) {
    return repeat == RepeatMode::All || shuffle;
}

static_assert(manualTrackNavigationWraps(),
              "manual Previous/Next wrap in every playback mode");
static_assert(!manualPreviousUsesHistory(false) && manualPreviousUsesHistory(true),
              "only random playback uses real play history for Previous");
static_assert(!automaticTrackEndWraps(RepeatMode::Off, false),
              "list once stops at natural queue end");
static_assert(automaticTrackEndWraps(RepeatMode::All, false),
              "list loop wraps at natural queue end");
static_assert(automaticTrackEndWraps(RepeatMode::Off, true),
              "random loop starts a new shuffled round");

}  // namespace player
}  // namespace adv_walkman
