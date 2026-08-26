#pragma once

#include <cstddef>
#include <cstdint>

namespace adv_walkman {
namespace player {

enum class UiPage : uint8_t {
    Player,
    Playlist,
    Library,
    Settings,
};

enum class UiAction : uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Confirm,
    Back,
    OpenSettings,
};

struct RawKeyEvent {
    int8_t x = -1;
    int8_t y = -1;
    uint8_t keyCount = 0;
    bool fn = false;
};

struct UiStats {
    uint32_t renderCount = 0;
    uint32_t renderMaxUs = 0;
    uint32_t inputEvents = 0;
    uint32_t pageTransitions = 0;
    uint32_t minimumHeap = UINT32_MAX;
};

const char* uiPageName(UiPage page);
const char* uiActionName(UiAction action);

}  // namespace player
}  // namespace adv_walkman
