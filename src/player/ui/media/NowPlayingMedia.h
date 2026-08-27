#pragma once
#include "CoverRenderer.h"
#include "LyricsRenderer.h"
namespace adv_walkman { namespace player {
struct NowPlayingMediaStatus {
    MediaState lyrics=MediaState::Idle,cover=MediaState::Idle;
    MediaView preferred=MediaView::Lyrics,view=MediaView::Cover;
    int current=-1;
    uint32_t frames=0,viewChanges=0,cancellations=0,reads=0,serviceMaxUs=0;
    uint8_t page=0,pages=1;
    bool layoutError=false,invalidUtf8=false;
    const char* error="none";
};
class NowPlayingMedia final {
  public:
    void begin(FontCache& fonts) { fonts_=&fonts; }
    void selectTrack(const char* path);
    void release();
    void service();
    void updatePosition(uint32_t positionMs,uint32_t durationMs,bool paused);
    void setPreferred(uint8_t value);
    bool toggleView();
    bool wantsFrame(uint32_t nowMs) const;
    bool beginFrame(uint32_t nowMs);
    bool prepareStripe(int y,int height);
    void drawStripe(lgfx::LGFXBase& canvas,int y,int height);
    void endFrame();
    int stripeHeight() const { return frameView_==MediaView::Cover?2:18; }
    NowPlayingMediaStatus status() const;
    const LyricsTimeline& timeline() const {return timeline_;}
    bool frameInProgress() const {return frameInProgress_;}
  private:
    FontCache* fonts_=nullptr;
    LyricsTimeline timeline_;
    CoverRenderer cover_;
    LyricsRenderer renderer_;
    MediaView preferred_=MediaView::Lyrics,frameView_=MediaView::Cover;
    uint32_t positionMs_=0,durationMs_=0,shownLyricRevision_=UINT32_MAX,shownCoverRevision_=UINT32_MAX;
    uint32_t frameAtMs_=0,animationStartedAtMs_=0,frames_=0,viewChanges_=0,cancellations_=0,serviceMaxUs_=0;
    int shownCurrent_=-2,frameShift_=0;
    uint8_t shownPage_=255;
    bool active_=false,dirty_=true,paused_=true,frameInProgress_=false,seek_=false;
    MediaView effectiveView() const {return timeline_.hasLyrics()?preferred_:MediaView::Cover;}
};
// Arduino 2.0.16 defaults regular files to 4096-byte stdio buffers. The
// workers explicitly use 128/256/512-byte buffers; reserve also covers VFS,
// FAT handles, path strings, directory search and allocator bookkeeping.
constexpr size_t kMediaFsReserve=7*1024;
constexpr size_t kMediaBudgetBytes=FontCache::workBytes()+LyricsTimeline::workBytes()+sizeof(NowPlayingMedia)+sizeof(FontCache)+kMediaFsReserve;
static_assert(kMediaBudgetBytes<=48*1024,
              "P3C media memory exceeds 48 KiB; do not enlarge the budget");
} }
