#pragma once

#include <M5GFX.h>

#include <cstddef>

#include "UiTextLayout.h"
#include "UiTypes.h"
#include "player/core/CoreTypes.h"
#include "player/library/MusicLibrary.h"

namespace adv_walkman {
namespace player {

constexpr size_t kP3AVisibleRows = 6;

struct PlaylistRenderRow {
    bool valid = false;
    bool selected = false;
    bool playing = false;
    LibraryEntryType type = LibraryEntryType::Track;
    // Keep the full bounded source; pixel layout, not a byte cap, decides
    // where to truncate it. Only six visible rows are resident.
    char label[kTrackPathCapacity] = {};
};

struct UiRenderContext {
    UiPage page = UiPage::Library;
    const char* hint = nullptr;
    const char* error = nullptr;
    char libraryName[kTrackPathCapacity] = {};
    const char* directoryPath = nullptr;
    const char* currentTrack = nullptr;
    const char* playerState = nullptr;
    size_t catalogIndex = 0;
    size_t catalogCount = 0;
    size_t playlistSelected = 0;
    size_t playlistCount = 0;
    uint32_t positionMs = 0;
    PlaylistRenderRow rows[kP3AVisibleRows]{};
};

class LibraryPageRenderer final {
  public:
    static UiTextLayoutResult render(M5GFX& display,
                                     const UiRenderContext& context);
};

class PlaylistPageRenderer final {
  public:
    static void render(M5GFX& display, const UiRenderContext& context);
    static void renderRegion(M5GFX& display, const UiRenderContext& context, uint8_t region);
};

class SettingsPageRenderer final {
  public:
    static void render(M5GFX& display, const UiRenderContext& context);
};

}  // namespace player
}  // namespace adv_walkman
