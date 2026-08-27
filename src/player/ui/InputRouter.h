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

  private:
    InputEdges edges_;
};

}  // namespace player
}  // namespace adv_walkman
