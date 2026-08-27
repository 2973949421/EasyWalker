#pragma once

#include <cstdint>

#include "UiTypes.h"

namespace adv_walkman {
namespace player {

// Cardputer ADV navigation is printed as Fn combinations. M5Cardputer exposes
// the physical 4x14 coordinates and modifier state, not PC-style Arrow events.
class InputRouter final {
  public:
    bool poll(UiAction& action, RawKeyEvent& raw, bool playerPage=false);

  private:
    static constexpr uint32_t kDebounceMs = 80;
    uint32_t lastAcceptedAtMs_ = 0;
    bool viewHeld_=false;
};

}  // namespace player
}  // namespace adv_walkman
