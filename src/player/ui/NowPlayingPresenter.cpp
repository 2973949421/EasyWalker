#include "NowPlayingPresenter.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "UiTextLayout.h"

namespace adv_walkman {
namespace player {
namespace {
using G = NowPlayingGeometry;
constexpr uint16_t kBackground = 0x0861;
constexpr uint16_t kPanel = 0x10E3;
constexpr uint16_t kAccent = 0xFBE0;
constexpr uint16_t kMuted = 0x8410;
constexpr uint16_t kText = 0xFFFF;
constexpr float kTitleSize = 1.75f;  // Font0: 8px * 1.75 = 14px
constexpr float kSmallSize = 1.5f;   // Font0: 12px

void formatTime(char* output, size_t capacity, uint32_t milliseconds) {
    const uint32_t seconds = milliseconds / 1000U;
    std::snprintf(output, capacity, "%lu:%02lu",
                  static_cast<unsigned long>(seconds / 60U),
                  static_cast<unsigned long>(seconds % 60U));
}
}  // namespace

void NowPlayingPresenter::begin() {
    row_.setBuffer(pixels_, G::width, G::rowHeight, 16);
    row_.setTextWrap(false);
}

void NowPlayingPresenter::setActive(bool active, uint32_t nowMs) {
    if (model_.active == active) return;
    model_.setActive(active, nowMs);
    if (active) {
        clearPage_ = true;
        contentRow_ = overlayRow_ = 0;
    }
}

void NowPlayingPresenter::measureTitle(uint32_t nowMs) {
    row_.setTextSize(kTitleSize);
    model_.setTitleWidth(UiTextLayout::singleLineWidth(row_, model_.title), nowMs);
}

void NowPlayingPresenter::update(const PlayerSnapshot& snapshot,
                                  const char* path, LibraryRuntime& library,
                                  uint32_t nowMs) {
    if (model_.setTrack(path, nowMs)) measureTitle(nowMs);
    model_.updatePlayback(snapshot.state, snapshot.positionMs, snapshot.durationMs,
                           snapshot.repeatMode, snapshot.shuffleEnabled);
    if (!model_.active) return;
    // Only the visible client requests the shared reader. A Playlist request
    // can replace an in-flight one; keyed results and the model's copy prevent
    // stale metadata from being painted on another track.
    if (model_.path[0] != '\0' &&
        (model_.metadataState == DisplayMetadataState::Fallback ||
         (model_.metadataState == DisplayMetadataState::Pending &&
          std::strcmp(library.metadataRequestPath(), model_.path) != 0))) {
        const auto result = library.requestMetadataPath(model_.path);
        model_.metadataState = result == LibraryResult::Error
            ? DisplayMetadataState::Error : DisplayMetadataState::Pending;
    }
    if (model_.metadataState == DisplayMetadataState::Pending &&
        std::strcmp(library.metadataRequestPath(), model_.path) == 0) {
        Mp3Metadata metadata;
        const auto status = library.metadataStatus();
        if (library.metadataForPath(model_.path, metadata)) {
            const bool validTags = status.error == Mp3MetadataError::None;
            model_.applyMetadata(model_.path,
                                  validTags && metadata.title.present && !metadata.titleFromFilename
                                      ? metadata.title.value : nullptr,
                                  validTags && metadata.artist.present ? metadata.artist.value : "", nowMs);
            model_.metadataWarning = static_cast<uint8_t>(status.error);
            measureTitle(nowMs);
        } else if (status.state == Mp3MetadataState::Error) {
            model_.metadataState = DisplayMetadataState::Error;
            model_.metadataWarning = static_cast<uint8_t>(status.error);
        }
    }
    model_.tick(nowMs);
}

void NowPlayingPresenter::setContent(const char* hint, const char* error) {
    model_.setContent(hint, error);
}

void NowPlayingPresenter::notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs) {
    model_.notifyVolumeAdjusted(volume, nowMs);
}

void NowPlayingPresenter::prepareRow(int height, uint16_t background,
                                     float textSize) {
    row_.setBuffer(pixels_, G::width, height, 16);
    row_.clearClipRect();
    row_.fillScreen(background);
    row_.setTextSize(textSize);
    row_.setTextColor(kText, background);
    row_.setTextWrap(false);
}

void NowPlayingPresenter::pushRow(M5GFX& display, int y) {
    row_.pushSprite(&display, 0, y);
}

void NowPlayingPresenter::drawContentSlice(int screenY, int height) {
    prepareRow(height, kBackground, kSmallSize);
    // All coordinates refer to the Content Stage; row clipping handles its
    // small stripe. P3C can replace this background painter without reflowing
    // either chrome or the volume overlay.
    row_.setTextColor(kMuted, kBackground);
    row_.drawString("P3B", 6, 85 - screenY);
    row_.drawString("MEDIA IN P3C", 6, 105 - screenY);
    const char* hint = model_.hint[0] ? model_.hint : "FN+ESC\nBACK TO LIST";
    const char* newline = std::strchr(hint, '\n');
    // A glyph can span two stripes. The common layout helper accepts a
    // negative baseline and clips to this row without splitting UTF-8.
    auto line = [&](const char* text, size_t bytes, int baseline, uint16_t color) {
        row_.setTextColor(color, kBackground);
        UiTextLayout::drawClippedLabel(row_, text, G::margin, baseline - screenY,
                                        G::textWidth, bytes);
    };
    line(hint, newline ? static_cast<size_t>(newline - hint) : std::strlen(hint),
          135, kAccent);
    if (newline) line(newline + 1, std::strlen(newline + 1), 153, kText);
    if (model_.error[0]) line(model_.error, std::strlen(model_.error), 181, TFT_ORANGE);
    if (model_.volumeVisible) {
        row_.fillRect(G::overlayX, G::overlayY - screenY,
                       G::overlayWidth, G::overlayHeight, kPanel);
        row_.drawRect(9, 70 - screenY, 7, 70, kMuted);
        const int fill = (static_cast<unsigned>(model_.volume) * 68 + 127) / 255;
        row_.fillRect(10, 139 - screenY - fill, 5, fill, kAccent);
        char percent[8] = {};
        std::snprintf(percent, sizeof(percent), "%u%%",
                      NowPlayingModel::volumePercent(model_.volume));
        row_.setTextColor(kText, kPanel);
        row_.drawString(percent, G::overlayX, 147 - screenY);
    }
}

void NowPlayingPresenter::drawStateIcon(PlayerState state) {
    switch (state) {
        case PlayerState::Playing: row_.fillTriangle(6, 3, 6, 12, 13, 7, kAccent); break;
        case PlayerState::Paused:
            row_.fillRect(6, 3, 3, 10, kAccent);
            row_.fillRect(11, 3, 3, 10, kAccent); break;
        case PlayerState::Stopped: row_.fillRect(6, 4, 8, 8, kMuted); break;
        case PlayerState::Error:
            row_.drawLine(6, 3, 14, 12, TFT_ORANGE);
            row_.drawLine(14, 3, 6, 12, TFT_ORANGE); break;
        case PlayerState::Empty: row_.drawRect(6, 4, 8, 8, kMuted); break;
    }
}

bool NowPlayingPresenter::renderOne(M5GFX& display) {
    if (!model_.active) return false;
    const uint32_t started = micros();
    stats_.minimumHeap = std::min(stats_.minimumHeap, ESP.getFreeHeap());
    if (clearPage_) {
        display.clearClipRect();
        display.fillScreen(kBackground);
        clearPage_ = false;
        ++stats_.pageClears;
    } else if (model_.dirty & DirtyTitle) {
        prepareRow(18, kBackground, kTitleSize);
        row_.setTextColor(kAccent, kBackground);
        UiTextLayout::drawScrolledLine(row_, model_.title[0] ? model_.title : "No track",
                                       {6, 1, 123, 17, 1, 0, false}, model_.titleOffsetPx);
        pushRow(display, 0);
        model_.clearDirty(DirtyTitle);
        ++stats_.titleDraws;
    } else if (model_.dirty & DirtyArtist) {
        prepareRow(16, kBackground, kSmallSize);
        UiTextLayout::draw(row_, model_.artist, {6, 0, 123, 16, 1, 0, true});
        pushRow(display, 18);
        model_.clearDirty(DirtyArtist);
        ++stats_.artistDraws;
    } else if (model_.dirty & DirtyTime) {
        prepareRow(18, kPanel, kSmallSize);
        char position[16], duration[16], text[40];
        formatTime(position, sizeof(position), model_.positionMs);
        if (model_.durationMs) formatTime(duration, sizeof(duration), model_.durationMs);
        else std::strcpy(duration, "--:--");
        std::snprintf(text, sizeof(text), "%s / %s", position, duration);
        UiTextLayout::draw(row_, text, {6, 2, 123, 16, 1, 0, true});
        pushRow(display, G::footerY);
        model_.clearDirty(DirtyTime);
        ++stats_.timeDraws;
    } else if (model_.dirty & DirtyProgress) {
        prepareRow(3, kPanel, kSmallSize);
        row_.drawFastHLine(6, 1, 123, kMuted);
        const int progress = model_.progressPixels();
        if (progress > 0) row_.drawFastHLine(6, 1, progress, kAccent);
        pushRow(display, 220);
        model_.clearDirty(DirtyProgress);
        ++stats_.progressDraws;
    } else if (model_.dirty & DirtyStatus) {
        prepareRow(17, kPanel, kSmallSize);
        drawStateIcon(model_.state);
        // MODE? is a diagnostic for legacy combinations, not a new mode.
        // Slightly smaller type keeps that full label plus Original readable.
        if (std::strcmp(model_.modeLabel(), "MODE?") == 0) row_.setTextSize(1.25f);
        UiTextLayout::draw(row_, model_.modeLabel(), {17, 2, 39, 15, 1, 0, true});
        row_.setTextSize(kSmallSize);
        UiTextLayout::draw(row_, "Original", {57, 2, 72, 15, 1, 0, true});
        pushRow(display, 223);
        model_.clearDirty(DirtyStatus);
        ++stats_.statusDraws;
    } else if (model_.dirty & DirtyContent) {
        if (contentRevision_ != model_.contentRevision) {
            contentRevision_ = model_.contentRevision;
            contentRow_ = 0;
        }
        const int height = std::min(G::rowHeight, G::contentHeight - contentRow_);
        drawContentSlice(G::contentY + contentRow_, height);
        pushRow(display, G::contentY + contentRow_);
        contentRow_ += height;
        if (contentRow_ == G::contentHeight) {
            contentRow_ = 0;
            model_.clearDirty(DirtyContent);
        }
        ++stats_.contentSlices;
    } else if (model_.dirty & DirtyOverlay) {
        if (overlayRevision_ != model_.overlayRevision) {
            overlayRevision_ = model_.overlayRevision;
            overlayRow_ = 0;
        }
        const int height = std::min(G::rowHeight, G::overlayHeight - overlayRow_);
        drawContentSlice(G::overlayY + overlayRow_, height);
        display.setClipRect(G::overlayX, G::overlayY, G::overlayWidth, G::overlayHeight);
        pushRow(display, G::overlayY + overlayRow_);
        display.clearClipRect();
        overlayRow_ += height;
        if (overlayRow_ == G::overlayHeight) {
            overlayRow_ = 0;
            model_.clearDirty(DirtyOverlay);
        }
        ++stats_.overlaySlices;
    } else {
        return false;
    }
    stats_.renderMaxUs = std::max<uint32_t>(stats_.renderMaxUs, micros() - started);
    return true;
}

}  // namespace player
}  // namespace adv_walkman
