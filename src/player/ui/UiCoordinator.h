#pragma once

#include <M5GFX.h>

#include <cstddef>
#include <cstdint>

#include "LibraryCatalog.h"
#include "PageRenderers.h"
#include "NowPlayingPresenter.h"
#include "UiTypes.h"
#include "LibraryVisual.h"
#include "SettingsPanel.h"
#include "InputEdges.h"
#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"

namespace adv_walkman {
namespace player {
constexpr size_t kP3DMediaBudgetBytes=kMediaBudgetBytes+sizeof(LibraryVisual);
static_assert(kP3DMediaBudgetBytes<=48*1024,"P3D media memory exceeds 48 KiB");
constexpr size_t kP3DMediaAndEventsBytes=kP3DMediaBudgetBytes+sizeof(InputEdges);
static_assert(kP3DMediaAndEventsBytes<=48*1024,"media plus new input event queue exceeds 48 KiB");
extern const uint32_t p3MemoryReport[6];

class UiCoordinator final {
  public:
    bool begin(M5GFX& display, PlayerRuntime& player,
               LibraryRuntime& libraryRuntime);
    void service();
    void serviceBackground(bool logIdle);
    bool physicalActivity(uint64_t mask,uint32_t now);
    void recordSuppressedAction(){++suppressedActions_;}
    uint32_t suppressedActions()const{return suppressedActions_;}
    const ScreenPowerController& screenPower()const{return power_;}
    const SettingsPanel& settings()const{return settings_;}
    const LibraryVisual& libraryVisual()const{return libraryVisual_;}
    bool settingsBusy()const{return settings_.store.writing();}
    bool displaySettingsSaved()const{return settings_.store.idle()&&settings_.store.error()[0]=='n';}
    bool readyToReturn()const{return settings_.readyToReturn();}
    void finishLauncherReturn(bool logOk){settings_.finishReturn(logOk);}
    bool handleAction(UiAction action);
    // Deterministic test entry; changes only the visible page and browser.
    // It does not stop or alter the restored Player state.
    void showLibrary();
    void showPlayer() { setPage(UiPage::Player); }
    FontCache& fonts() { return fonts_; }
    const FontCache& fonts() const {return fonts_;}
    void notifyLogSaved(bool success){nowPlaying_.notifyLogSaved(success,millis());}
    NowPlayingPresenter& presenterForValidation() { return nowPlaying_; }
    void setHint(const char* hint);
    // Gate-only card: never text over a partially visible media frame.
    void setGateCard(const char* text);
    void setExternalError(const char* error);

    UiPage page() const;
    uint32_t inputEpoch()const{return stats_.pageTransitions;}
    void recordInputLatency(uint32_t ms){inputLatencyMaxMs_=std::max(inputLatencyMaxMs_,ms);}
    uint32_t inputLatencyMaxMs()const{return inputLatencyMaxMs_;}
    void recordInputQueue(uint32_t overflow,uint32_t stale){inputOverflow_=overflow;inputStale_=stale;}
    uint32_t inputOverflow()const{return inputOverflow_;}
    uint32_t inputStale()const{return inputStale_;}
    UiStats stats() const;
    const char* navigationTarget() const { return navigationTarget_; }
    const char* navigationError() const { return navigationError_; }
    const MusicLibrary& browser() const { return libraryRuntime_->library(); }
    bool currentTrackPath(char* output, size_t capacity) const;
    void notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs);
    const NowPlayingPresenter& nowPlaying() const { return nowPlaying_; }

  private:
    enum class PendingNavigation : uint8_t {
        None,
        SelectTrack,
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
    bool openBrowser(const char* path, UiPage page, bool locateCurrent = false);
    void cancelNavigation();
    void navigationFailed(const char* reason);
    void prepareBrowser();
    void invalidateBrowser();
    void movePlaylistSelection(size_t next);
    void selectVisibleMetadata();
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
    char gateCard_[160] = {};
    bool gateCardDirty_ = false;
    char externalError_[96] = {};
    bool dirty_ = true;
    bool renderRetryRequested_ = false;
    bool resourceTurn_ = false;
    uint32_t lastRenderAtMs_ = 0;
    uint32_t lastLibraryGeneration_ = 0;
    LibraryState lastLibraryState_ = LibraryState::Idle;
    PlayerState lastPlayerState_ = PlayerState::Empty;
    char lastCurrentTrack_[kTrackPathCapacity] = {};
    UiStats stats_{};
    // Fixed six-row scratch, not an all-library cache. Keep complete UTF-8
    // names off the small Arduino loop stack and alive through render().
    UiRenderContext renderContext_{};
    NavigationLoad navigation_{};
    char navigationTarget_[kTrackPathCapacity] = {};
    char navigationError_[64] = {};
    uint32_t browserGeneration_ = 0, browserRequest_ = 0, browserProgress_ = 0;
    uint32_t browserProgressAt_ = 0;
    size_t savedPlaylistSelected_ = 0, locateIndex_ = 0;
    bool locateCurrent_ = false, openRequested_ = false;
    bool pageClearRequested_ = true, browserContextReady_ = false;
    bool retainedPlaylist_ = false, loadingDrawn_ = false;
    uint8_t dirtyRegions_ = 255;
    uint32_t pageRequestedAt_ = 0;
    uint32_t selectionRequestedAt_ = 0;
    uint8_t feedbackRegions_ = 0;
    bool warmReturnPending_ = false;
    uint8_t prepareRow_ = 0, drawRegion_ = 0;
    NowPlayingPresenter nowPlaying_;
    FontCache fonts_;
    LibraryVisual libraryVisual_;
    SettingsPanel settings_;
    ScreenPowerController power_;
    bool p3dPrepared_=false,libraryVisualDirty_=true;
    int libraryMoveDirection_=0;
    uint8_t appliedBrightness_=255;
    uint32_t suppressedActions_=0;
    uint32_t inputLatencyMaxMs_=0;
    uint32_t inputOverflow_=0,inputStale_=0;
};

}  // namespace player
}  // namespace adv_walkman
