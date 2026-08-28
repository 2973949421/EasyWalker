#pragma once

#include <M5GFX.h>
#include "NowPlayingModel.h"
#include "player/app/LibraryRuntime.h"
#include "media/NowPlayingMedia.h"

namespace adv_walkman {
namespace player {

struct P3BCheckResult;

struct NowPlayingRenderStats {
    uint32_t titleDraws = 0;
    uint32_t artistDraws = 0;
    uint32_t timeDraws = 0;
    uint32_t progressDraws = 0;
    uint32_t statusDraws = 0;
    uint32_t contentSlices = 0;
    uint32_t overlaySlices = 0;
    uint32_t pageClears = 0;
    uint32_t overlayPatches = 0;
    uint32_t renderMaxUs = 0;
    uint32_t minimumHeap = UINT32_MAX;
};

class NowPlayingPresenter final {
  public:
    void begin();
    void bindMedia(FontCache& fonts) { fonts_=&fonts; media_.begin(fonts); }
    const char* bootMediaSelfCheck(){return media_.bootSelfCheck();}
    const char* bootFontSelfCheck(M5GFX& display);
    void serviceMedia() { media_.service(); }
    void setPreferredView(uint8_t view) { media_.setPreferred(view); }
    bool toggleView() { return media_.toggleView(); }
    NowPlayingMediaStatus mediaStatus() const { return media_.status(); }
    const LyricsTimeline& lyrics() const { return media_.timeline(); }
    const CoverRenderer& cover() const { return media_.cover(); }
    bool presentingLyrics() const {return media_.presentingLyrics();}
    void preloadTrack(const char* path) {media_.selectTrack(path);}
    void releasePreload() {media_.release();}
    void resetMediaDiagnostics() {media_.resetDiagnostics();}
    void invalidateDisplay() {clearPage_=true;model_.dirty=DirtyAll;media_.requestRedraw();}
    void setActive(bool active, uint32_t nowMs);
    void update(const PlayerSnapshot& snapshot, const char* path,
                LibraryRuntime& library, uint32_t nowMs);
    void setContent(const char* hint, const char* error);
    void notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs);
    void notifyLogSaved(bool success,uint32_t nowMs){logNote_=success?1:2;logNoteAt_=nowMs;model_.dirty|=DirtyStatus;}
    // One row/stripe per call; no file access or full-screen Sprite.
    bool renderOne(M5GFX& display);
    const NowPlayingModel& model() const { return model_; }
    const NowPlayingRenderStats& stats() const { return stats_; }

  private:
    friend P3BCheckResult checkP3BOverlayRestoration(NowPlayingPresenter& presenter);
    friend P3BCheckResult checkP3BPresenterDrawing(NowPlayingPresenter& presenter);
    void measureTitle(uint32_t nowMs);
    void prepareRow(int height, uint16_t background, float textSize);
    void drawContentSlice(int screenY, int height);
    void pushRow(M5GFX& display, int y);
    void drawStateIcon(PlayerState state);
    bool renderContentOne(M5GFX& display);
    FontCache* fonts_=nullptr;
    NowPlayingMedia media_;
    bool preferContent_=false;
    uint32_t frameOverlayRevision_=0,frameContentRevision_=0;
    bool frameVolumeVisible_=false;
    bool overlayPending_=false,framePartial_=false;
    int frameContentY_=NowPlayingGeometry::contentY;
    int contentEnd_=NowPlayingGeometry::contentHeight;
    uint8_t frameVolume_=128;
    uint8_t logNote_=0;
    uint32_t logNoteAt_=0;
    NowPlayingModel model_;
    NowPlayingRenderStats stats_;
    // Owned buffer must outlive the Sprite which borrows it.
    uint16_t pixels_[NowPlayingGeometry::width * NowPlayingGeometry::rowHeight]{};
    M5Canvas row_;
    bool clearPage_ = true;
    int contentRow_ = 0;
    int overlayRow_ = 0;
    uint32_t contentRevision_ = UINT32_MAX;
    uint32_t overlayRevision_ = UINT32_MAX;
};

}  // namespace player
}  // namespace adv_walkman
