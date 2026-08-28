#pragma once

#include <cstddef>
#include <cstdint>

#include "UiTextLayout.h"
#include "NavigationLoad.h"

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
    ToggleView,
    TogglePlayback,
    VolumeUp,
    VolumeDown,
    SaveDiagnostics,
    ToggleCurrentPlaybackPage,
};

struct RawKeyEvent {
    int8_t x = -1;
    int8_t y = -1;
    uint8_t keyCount = 0;
    bool fn = false;
};

struct UiStats {
    NavigationState navigationState = NavigationState::Idle;
    uint32_t navigationGeneration = 0, navigationErrors = 0;
    uint32_t completedPages = 0, playlistFrames = 0, libraryFrames = 0;
    uint32_t trackSelections = 0, differentTrackSelections = 0;
    uint32_t lastQueueCount = 0, prepareMaxUs = 0, navigationMaxUs = 0;
    uint32_t largestFreeBlock = 0, lastLibraryError = 0;
    uint32_t tabEvents = 0, tabStateErrors = 0, windowBuilds = 0, highlightUpdates = 0;
    uint32_t tabPlaying = 0, tabPaused = 0, tabBeforeMs = 0, tabAfterMs = 0;
    uint8_t tabState = 0;
    uint32_t firstFrameMaxMs = 0;
    uint32_t warmReturnMaxMs = 0, selectionFeedbackMaxMs = 0, warmReturns = 0;
    bool pageFirstFrameComplete = false;
    uint32_t renderCount = 0;
    uint32_t renderMaxUs = 0;
    uint32_t inputEvents = 0;
    uint32_t pageTransitions = 0;
    uint32_t minimumHeap = UINT32_MAX;
    UiTextLayoutResult libraryText{};
    bool libraryTextIsBenchmark = false;
    uint16_t displaySelfChecks = 0;
    const char* displaySelfFailure = nullptr;
};

const char* uiPageName(UiPage page);
const char* uiActionName(UiAction action);

}  // namespace player
}  // namespace adv_walkman
