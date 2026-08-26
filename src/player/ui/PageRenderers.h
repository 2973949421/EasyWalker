#pragma once

#include <M5GFX.h>

#include <cstddef>

#include "UiTypes.h"
#include "player/core/CoreTypes.h"
#include "player/library/MusicLibrary.h"

namespace adv_walkman {
namespace player {

constexpr size_t kP3AVisibleRows = 7;

struct PlaylistRenderRow {
    bool valid = false;
    bool selected = false;
    bool playing = false;
    LibraryEntryType type = LibraryEntryType::Track;
    char label[64] = {};
};

struct UiRenderContext {
    UiPage page = UiPage::Library;
    const char* hint = nullptr;
    const char* error = nullptr;
    const char* libraryName = nullptr;
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
    static void render(M5GFX& display, const UiRenderContext& context);
};

class PlaylistPageRenderer final {
  public:
    static void render(M5GFX& display, const UiRenderContext& context);
};

class PlayerPageRenderer final {
  public:
    static void render(M5GFX& display, const UiRenderContext& context);
};

class SettingsPageRenderer final {
  public:
    static void render(M5GFX& display, const UiRenderContext& context);
};

}  // namespace player
}  // namespace adv_walkman
