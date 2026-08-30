#include "NowPlayingModel.h"

#include <algorithm>
#include <cstring>

namespace adv_walkman {
namespace player {
namespace {
template <size_t N>
bool copyText(char (&target)[N], const char* source) {
    if (source == nullptr) source = "";
    const size_t length = std::strlen(source);
    if (length >= N) return false;  // No partial UTF-8 strings.
    std::memcpy(target, source, length + 1);
    return true;
}
}  // namespace

void NowPlayingModel::resetMarquee(uint32_t nowMs) {
    marqueeEpochMs_ = animationAtMs_ = nowMs;
    titleOffsetPx = 0;
    phase = titleWidthPx > NowPlayingGeometry::textWidth
                ? MarqueePhase::StartHold : MarqueePhase::Static;
    observedPhases |= 1U << static_cast<uint8_t>(phase);
    dirty |= DirtyTitle;
}

void NowPlayingModel::setActive(bool value, uint32_t nowMs) {
    if (active == value) return;
    active = value;
    volumeVisible = false;
    ++overlayRevision;
    if (active) {
        resetMarquee(nowMs);
        dirty = DirtyAll;
        ++contentRevision;
    } else {
        dirty = 0;
    }
}

bool NowPlayingModel::setTrack(const char* value, uint32_t nowMs) {
    if (value == nullptr) value = "";
    if (std::strcmp(path, value) == 0) return false;
    if (!copyText(path, value)) return false;
    const char* name = std::strrchr(path, '/');
    copyText(title, name == nullptr ? path : name + 1);
    char* dot = std::strrchr(title, '.');
    if (dot != nullptr) *dot = '\0';
    artist[0] = '\0';
    metadataState = DisplayMetadataState::Fallback;
    metadataWarning = 0;
    titleWidthPx = 0;
    resetMarquee(nowMs);
    dirty |= DirtyArtist;
    return true;
}

bool NowPlayingModel::applyMetadata(const char* requestPath,
                                    const char* newTitle,
                                    const char* newArtist, uint32_t nowMs) {
    if (requestPath == nullptr || std::strcmp(path, requestPath) != 0) {
        ++rejectedMetadataResults;
        return false;
    }
    if (newTitle != nullptr && newTitle[0] != '\0' &&
        std::strcmp(title, newTitle) != 0 && copyText(title, newTitle)) {
        titleWidthPx = 0;
        resetMarquee(nowMs);
    }
    if (newArtist == nullptr) newArtist = "";
    if (std::strcmp(artist, newArtist) != 0 && copyText(artist, newArtist)) {
        dirty |= DirtyArtist;
    }
    metadataState = DisplayMetadataState::Ready;
    return true;
}

void NowPlayingModel::updatePlayback(PlayerState newState, uint32_t newPosition,
                                     uint32_t newDuration, RepeatMode newRepeat,
                                     bool newShuffle, SoundPreset newSoundPreset) {
    const int16_t oldProgress = progressPixels();
    if (positionMs / 1000 != newPosition / 1000 || durationMs != newDuration) {
        dirty |= DirtyTime;
    }
    positionMs = newPosition;
    durationMs = newDuration;
    if (progressPixels() != oldProgress) dirty |= DirtyProgress;
    if (state != newState || repeat != newRepeat || shuffle != newShuffle ||
        soundPreset != newSoundPreset) {
        dirty |= DirtyStatus;
    }
    state = newState;
    repeat = newRepeat;
    shuffle = newShuffle;
    soundPreset = newSoundPreset;
}

void NowPlayingModel::setTitleWidth(int32_t widthPx, uint32_t nowMs) {
    if (titleWidthPx == widthPx) return;
    titleWidthPx = std::max<int32_t>(0, widthPx);
    resetMarquee(nowMs);
}

void NowPlayingModel::tick(uint32_t nowMs) {
    if (!active) return;
    if (volumeVisible && nowMs - volumeAtMs_ >= kVolumeDurationMs) {
        volumeVisible = false;
        dirty |= DirtyOverlay;
        ++overlayRevision;
    }
    if (!headerVisible || titleWidthPx <= NowPlayingGeometry::textWidth ||
        nowMs - animationAtMs_ < kAnimationIntervalMs) return;
    animationAtMs_ = nowMs;
    const uint32_t travelMs = scrollMs(titleWidthPx);
    const uint32_t cycleMs = 2 * kHoldMs + travelMs;
    uint32_t elapsed = nowMs - marqueeEpochMs_;
    if (elapsed >= cycleMs) {
        marqueeCycles += elapsed / cycleMs;
        marqueeEpochMs_ += (elapsed / cycleMs) * cycleMs;
        elapsed %= cycleMs;
    }
    phase = phaseAt(titleWidthPx, elapsed);
    const int32_t offset = offsetAt(titleWidthPx, elapsed);
    observedPhases |= 1U << static_cast<uint8_t>(phase);
    if (offset != titleOffsetPx) {
        titleOffsetPx = offset;
        dirty |= DirtyTitle;
    }
}

void NowPlayingModel::notifyVolumeAdjusted(uint8_t value, uint32_t nowMs) {
    if (!active) return;
    volume = value;
    volumeAtMs_ = nowMs;
    volumeVisible = true;
    ++overlayRevision;
    dirty |= DirtyOverlay;
}

void NowPlayingModel::setContent(const char* newHint, const char* newError) {
    if (newHint == nullptr) newHint = "";
    if (newError == nullptr) newError = "";
    if (std::strcmp(hint, newHint) == 0 && std::strcmp(error, newError) == 0) return;
    copyText(hint, newHint);
    copyText(error, newError);
    ++contentRevision;
    dirty |= DirtyContent;
}

const char* NowPlayingModel::modeLabel() const {
    if (shuffle) return repeat == RepeatMode::Off ? "SHUF" : "MODE?";
    switch (repeat) {
        case RepeatMode::Off: return "NORM";
        case RepeatMode::One: return "ONE";
        case RepeatMode::All: return "ALL";
    }
    return "MODE?";
}

int16_t NowPlayingModel::progressPixels() const {
    if (durationMs == 0) return -1;
    return static_cast<uint64_t>(std::min(positionMs, durationMs)) *
           NowPlayingGeometry::textWidth / durationMs;
}

}  // namespace player
}  // namespace adv_walkman
