#pragma once

#include <M5GFX.h>
#include "NowPlayingModel.h"
#include "player/app/LibraryRuntime.h"

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
    uint32_t renderMaxUs = 0;
    uint32_t minimumHeap = UINT32_MAX;
};

class NowPlayingPresenter final {
  public:
    void begin();
    void setActive(bool active, uint32_t nowMs);
    void update(const PlayerSnapshot& snapshot, const char* path,
                LibraryRuntime& library, uint32_t nowMs);
    void setContent(const char* hint, const char* error);
    void notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs);
    // One row/stripe per call; no file access or full-screen Sprite.
    bool renderOne(M5GFX& display);
    const NowPlayingModel& model() const { return model_; }
    const NowPlayingRenderStats& stats() const { return stats_; }

  private:
    friend P3BCheckResult checkP3BOverlayRestoration(NowPlayingPresenter& presenter);
    void measureTitle(uint32_t nowMs);
    void prepareRow(int height, uint16_t background, float textSize);
    void drawContentSlice(int screenY, int height);
    void pushRow(M5GFX& display, int y);
    void drawStateIcon(PlayerState state);
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
