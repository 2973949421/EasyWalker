#pragma once

#include <cstdint>
#include "player/core/CoreTypes.h"

namespace adv_walkman {
namespace player {

struct NowPlayingGeometry {
    static constexpr int width = 135;
    static constexpr int height = 240;
    static constexpr int headerHeight = 28;
    static constexpr int contentY = 28;
    static constexpr int contentHeight = 188;
    static constexpr int contentTop(bool lyrics) { return lyrics?0:headerHeight; }
    static constexpr int contentExtent(bool lyrics) { return footerY-contentTop(lyrics); }
    static constexpr int footerY = 216;
    static constexpr int footerHeight = 24;
    static constexpr int margin = 6;
    static constexpr int textWidth = 123;
    static constexpr int rowHeight = 18;
    static constexpr int overlayX = 6;
    static constexpr int overlayY = 68;
    static constexpr int overlayWidth = 25;
    static constexpr int overlayHeight = 82;
};

enum class MarqueePhase : uint8_t { Static, StartHold, Scrolling, EndHold };
enum class DisplayMetadataState : uint8_t { Fallback, Pending, Ready, Error };
enum NowPlayingDirty : uint8_t {
    DirtyTitle = 1, DirtyArtist = 2, DirtyTime = 4, DirtyStatus = 8,
    DirtyContent = 16, DirtyOverlay = 32, DirtyProgress = 64, DirtyAll = 127
};

// Pure display state. Clock is supplied by the caller for deterministic tests.
// Does not own or mutate playback/queue/session, or access SD/graphics APIs.
class NowPlayingModel final {
  public:
    static constexpr uint32_t kHoldMs = 5000;
    static constexpr uint32_t kAnimationIntervalMs = 50;
    static constexpr uint32_t kScrollPixelsPerSecond = 24;
    static constexpr uint32_t kVolumeDurationMs = 3000;

    void setActive(bool active, uint32_t nowMs);
    bool setTrack(const char* path, uint32_t nowMs);
    bool applyMetadata(const char* path, const char* title,
                       const char* artist, uint32_t nowMs);
    void updatePlayback(PlayerState state, uint32_t positionMs,
                        uint32_t durationMs, RepeatMode repeat, bool shuffle,
                        SoundPreset soundPreset);
    void setTitleWidth(int32_t widthPx, uint32_t nowMs);
    void tick(uint32_t nowMs);
    void notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs);
    void setContent(const char* hint, const char* error);
    static constexpr uint8_t volumePercent(uint8_t value) {
        return (static_cast<uint16_t>(value) * 100U + 127U) / 255U;
    }
    static constexpr uint32_t scrollMs(int32_t width) {
        return width <= NowPlayingGeometry::textWidth ? 0 :
            ((width - NowPlayingGeometry::textWidth) * 1000U +
             kScrollPixelsPerSecond - 1) / kScrollPixelsPerSecond;
    }
    static constexpr MarqueePhase phaseAt(int32_t width, uint32_t elapsed) {
        return width <= NowPlayingGeometry::textWidth ? MarqueePhase::Static :
            elapsed < kHoldMs ? MarqueePhase::StartHold :
            elapsed < kHoldMs + scrollMs(width) ? MarqueePhase::Scrolling :
            MarqueePhase::EndHold;
    }
    static constexpr int32_t offsetAt(int32_t width, uint32_t elapsed) {
        return width <= NowPlayingGeometry::textWidth || elapsed < kHoldMs ? 0 :
            elapsed >= kHoldMs + scrollMs(width) ? width - NowPlayingGeometry::textWidth :
            (elapsed - kHoldMs) * kScrollPixelsPerSecond / 1000U;
    }
    const char* modeLabel() const;
    int16_t progressPixels() const;  // -1 means unknown duration
    void clearDirty(uint8_t flags) { dirty &= ~flags; }

    char path[kTrackPathCapacity] = {};
    char title[kTrackPathCapacity] = {};
    char artist[128] = {};
    char hint[64] = {};
    char error[96] = {};
    PlayerState state = PlayerState::Empty;
    RepeatMode repeat = RepeatMode::Off;
    SoundPreset soundPreset = SoundPreset::Original;
    bool shuffle = false;
    uint32_t positionMs = 0;
    uint32_t durationMs = 0;
    DisplayMetadataState metadataState = DisplayMetadataState::Fallback;
    uint8_t metadataWarning = 0;
    bool active = false;
    bool headerVisible = true;
    uint8_t dirty = DirtyAll;
    uint32_t contentRevision = 0;
    uint32_t overlayRevision = 0;
    bool volumeVisible = false;
    uint8_t volume = 128;
    MarqueePhase phase = MarqueePhase::Static;
    int32_t titleWidthPx = 0;
    int32_t titleOffsetPx = 0;
    uint32_t rejectedMetadataResults = 0;
    uint32_t marqueeCycles = 0;
    uint8_t observedPhases = 0;

  private:
    void resetMarquee(uint32_t nowMs);
    uint32_t marqueeEpochMs_ = 0;
    uint32_t animationAtMs_ = 0;
    uint32_t volumeAtMs_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
