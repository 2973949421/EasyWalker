#pragma once

#include <M5GFX.h>

#include <cstddef>
#include <cstdint>

#include "LibraryCatalog.h"
#include "PageRenderers.h"
#include "UiTypes.h"
#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"

namespace adv_walkman {
namespace player {

class UiCoordinator final {
  public:
    bool begin(M5GFX& display, PlayerRuntime& player,
               LibraryRuntime& libraryRuntime);
    void service();
    bool handleAction(UiAction action);
    // Deterministic test entry; changes only the visible page and browser.
    // It does not stop or alter the restored Player state.
    void showLibrary();
    void setHint(const char* hint);
    void setExternalError(const char* error);

    UiPage page() const;
    UiStats stats() const;
    bool currentTrackPath(char* output, size_t capacity) const;

  private:
    enum class PendingNavigation : uint8_t {
        None,
        EnterLibrary,
        SelectTrack,
        RestorePlaylist,
    };

    enum class PendingIntent : uint8_t {
        None,
        SelectLibrary,
        ActivatePlaylistEntry,
    };

    static constexpr uint32_t kMinimumRenderIntervalMs = 20;

    void setPage(UiPage page);
    void servicePendingIntent();
    void servicePendingNavigation();
    void serviceSelectedMetadata();
    void render();
    void buildRenderContext(UiRenderContext& context);
    void buildPlaylistRows(UiRenderContext& context);

    bool beginSelectedLibrary();
    bool activatePlaylistEntry();
    bool returnFromPlaylist();
    bool restorePlaylistForCurrentTrack();
    size_t playlistCount() const;
    size_t physicalEntryIndex(size_t logicalIndex) const;
    bool setPath(char* destination, size_t capacity, const char* source);
    static bool parentDirectoryOfTrack(const char* track, char* output,
                                       size_t capacity);
    static bool firstLevelLibraryOfTrack(const char* track, char* output,
                                         size_t capacity,
                                         bool& uncategorized);
    static void displayNameFromPath(const char* path, char* output,
                                    size_t capacity);
    static void trackLabel(const char* name, char* output, size_t capacity);

    M5GFX* display_ = nullptr;
    PlayerRuntime* player_ = nullptr;
    LibraryRuntime* libraryRuntime_ = nullptr;
    LibraryCatalog catalog_;
    UiPage page_ = UiPage::Library;
    PendingNavigation pendingNavigation_ = PendingNavigation::None;
    PendingIntent pendingIntent_ = PendingIntent::None;

    bool uncategorized_ = false;
    size_t playlistSelected_ = 0;
    char libraryName_[kTrackPathCapacity] = {};
    char libraryRoot_[kTrackPathCapacity] = {};
    char lastPlaylistPath_[kTrackPathCapacity] = {};
    char pendingTrackPath_[kTrackPathCapacity] = {};
    char selectedMetadataPath_[kTrackPathCapacity] = {};
    char selectedMetadataTitle_[kMetadataTitleCapacity] = {};
    bool metadataRequested_ = false;

    char hint_[64] = {};
    char externalError_[96] = {};
    bool dirty_ = true;
    bool renderRetryRequested_ = false;
    uint32_t lastRenderAtMs_ = 0;
    uint32_t lastLibraryGeneration_ = 0;
    LibraryState lastLibraryState_ = LibraryState::Idle;
    PlayerState lastPlayerState_ = PlayerState::Empty;
    uint32_t lastPlayerSecond_ = UINT32_MAX;
    char lastCurrentTrack_[kTrackPathCapacity] = {};
    UiStats stats_{};
};

}  // namespace player
}  // namespace adv_walkman
