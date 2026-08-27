#include "P3BChecks.h"

#include <algorithm>
#include <cstring>
#include "UiTextLayout.h"

namespace adv_walkman {
namespace player {
namespace {
using G = NowPlayingGeometry;
static_assert(G::headerHeight + G::contentHeight + G::footerHeight == G::height,
              "All regions must fit portrait");
static_assert(G::footerY == G::contentY + G::contentHeight, "No region overlap");
static_assert(G::overlayX >= 0 && G::overlayX + G::overlayWidth <= G::width &&
              G::overlayY >= G::contentY &&
              G::overlayY + G::overlayHeight <= G::footerY, "Overlay inside content");
static_assert(G::width * G::rowHeight * sizeof(uint16_t) == 4860,
              "No full-screen framebuffer");
// These exercise the SAME constexpr functions used by tick()/the renderer at
// compile time, without installing a host compiler or waiting on the device.
static_assert(NowPlayingModel::volumePercent(0) == 0 &&
              NowPlayingModel::volumePercent(128) == 50 &&
              NowPlayingModel::volumePercent(255) == 100, "Volume conversion");
static_assert(NowPlayingModel::scrollMs(243) == 5000, "24px/s travel");
static_assert(NowPlayingModel::phaseAt(60, 9999) == MarqueePhase::Static,
              "Short title never scrolls");
static_assert(NowPlayingModel::phaseAt(243, 4999) == MarqueePhase::StartHold &&
              NowPlayingModel::offsetAt(243, 4999) == 0, "Hold start for 5s");
static_assert(NowPlayingModel::offsetAt(243, 7500) == 60, "Scroll midpoint");
static_assert(NowPlayingModel::phaseAt(243, 10000) == MarqueePhase::EndHold &&
              NowPlayingModel::offsetAt(243, 14999) == 120, "Tail remains visible");

void check(P3BCheckResult& result, bool condition, const char* failure) {
    ++result.checks;
    if (!condition && result.failure == nullptr) result.failure = failure;
}
}  // namespace

P3BCheckResult checkP3BModel() {
    P3BCheckResult r;
    NowPlayingModel model;
    model.setActive(true, 0);
    model.setTrack("/Music/Test/Track.mp3", 0);
    check(r, std::strcmp(model.title, "Track") == 0 && model.artist[0] == '\0',
          "filename_fallback");
    model.setTitleWidth(60, 0);
    model.tick(10000);
    check(r, model.phase == MarqueePhase::Static && model.titleOffsetPx == 0,
          "short_title_static");
    model.setTitleWidth(243, 0);  // 120px / 24px/s = exactly 5s travel
    model.tick(4999);
    check(r, model.phase == MarqueePhase::StartHold && model.titleOffsetPx == 0,
          "marquee_start_hold");
    model.tick(5050);
    check(r, model.phase == MarqueePhase::Scrolling && model.titleOffsetPx == 1,
          "marquee_start_scroll");
    model.tick(7500);
    check(r, model.titleOffsetPx == 60, "marquee_speed");
    model.updatePlayback(PlayerState::Paused, 43000, 100000, RepeatMode::Off, false);
    model.tick(10000);
    check(r, model.titleOffsetPx == 120 && model.phase == MarqueePhase::EndHold,
          "paused_marquee_end");
    model.tick(14900);
    check(r, model.titleOffsetPx == 120, "marquee_tail_hold");
    model.tick(15000);
    check(r, model.titleOffsetPx == 0 && model.marqueeCycles == 1,
          "marquee_cycle");
    model.clearDirty(DirtyAll);
    model.tick(15020);
    check(r, model.dirty == 0, "marquee_rate_cap");
    model.setActive(false, 16000);
    model.tick(24000);
    check(r, model.dirty == 0, "hidden_animation_idle");
    model.setActive(true, 24000);
    check(r, model.titleOffsetPx == 0 && !model.volumeVisible, "page_reentry");
    check(r, !model.applyMetadata("/Music/Other.mp3", "WRONG", "WRONG", 24000) &&
             std::strcmp(model.title, "Track") == 0, "metadata_path_isolation");
    check(r, model.applyMetadata(model.path, "曲名-日本語", "歌手", 25000) &&
             std::strcmp(model.title, "曲名-日本語") == 0, "metadata_utf8_copy");
    model.updatePlayback(PlayerState::Paused, 43000, 0, RepeatMode::Off, false);
    check(r, model.positionMs == 43000 && model.progressPixels() == -1,
          "restored_unknown_duration");
    model.tick(29000);
    check(r, model.positionMs == 43000, "no_ui_position_clock");
    model.updatePlayback(PlayerState::Playing, 50000, 100000, RepeatMode::Off, true);
    check(r, model.progressPixels() == 61 && std::strcmp(model.modeLabel(), "SHUF") == 0,
          "progress_and_standard_shuffle");
    model.updatePlayback(PlayerState::Playing, 110000, 100000, RepeatMode::All, true);
    check(r, model.progressPixels() == 123 && std::strcmp(model.modeLabel(), "MODE?") == 0 &&
             model.repeat == RepeatMode::All && model.shuffle, "legacy_mode_readonly");
    check(r, NowPlayingModel::volumePercent(0) == 0 &&
             NowPlayingModel::volumePercent(128) == 50 &&
             NowPlayingModel::volumePercent(255) == 100, "volume_percent");
    model.notifyVolumeAdjusted(0, 30000);
    model.tick(32999);
    check(r, model.volumeVisible, "overlay_duration");
    model.notifyVolumeAdjusted(255, 32999);
    model.tick(35000);
    check(r, model.volumeVisible && model.volume == 255, "overlay_extend");
    model.tick(35999);
    check(r, !model.volumeVisible && (model.dirty & DirtyOverlay), "overlay_restore_dirty");
    model.setActive(false, 36000);
    model.notifyVolumeAdjusted(128, 36001);
    model.setActive(true, 36002);
    check(r, !model.volumeVisible, "hidden_volume_event_ignored");
    model.notifyVolumeAdjusted(128, UINT32_MAX - 1000U);
    model.tick(1999);
    check(r, !model.volumeVisible, "overlay_clock_wrap");
    check(r, LibraryRuntime::isMetadataPath("/Music/日文/Track.MP3") &&
             !LibraryRuntime::isMetadataPath("/Music/../outside.mp3") &&
             !LibraryRuntime::isMetadataPath("/Music//alias.mp3") &&
             !LibraryRuntime::isMetadataPath("/Music2/track.mp3"), "metadata_canonical_path");
    return r;
}

P3BCheckResult checkP3BDrawing(lgfx::LovyanGFX& row) {
    P3BCheckResult r;
    if (row.width() != 135 || row.height() != 18) {
        check(r, false, "scratch_geometry");
        return r;
    }
    row.setFont(&fonts::Font0);
    row.setTextSize(1.75f);
    row.setTextColor(TFT_WHITE, TFT_BLACK);
    char longTitle[192];
    std::memset(longTitle, 'W', sizeof(longTitle) - 1);
    longTitle[191] = '\0';
    const int32_t fullWidth = UiTextLayout::singleLineWidth(row, longTitle);
    longTitle[127] = '\0';
    check(r, fullWidth > UiTextLayout::singleLineWidth(row, longTitle),
          "whole_title_beyond_128_bytes");
    longTitle[127] = 'W';
    row.fillScreen(TFT_BLACK);
    UiTextLayout::drawScrolledLine(row, longTitle, {6, 1, 123, 17, 1, 0, false},
                                    fullWidth - 123);
    bool marginClean = true, ink = false;
    for (int y = 0; y < 18; ++y) {
        for (int x = 0; x < 135; ++x) {
            const bool painted = row.readPixel(x, y) != TFT_BLACK;
            if (x < 6 || x >= 129) marginClean &= !painted;
            else ink |= painted;
        }
    }
    check(r, marginClean && ink, "marquee_tail_pixel_clip");
    row.setTextSize(1.5f);
    const auto unicode = UiTextLayout::draw(row, "曲名-日本語-Artist-LongLongLong",
                                             {6, 0, 123, 18, 1, 0, true});
    check(r, !unicode.invalidUtf8 && !unicode.layoutError &&
             unicode.maxLineWidthPx <= 123, "utf8_layout_boundary_not_glyph_coverage");
    const auto invalid = UiTextLayout::measure(row, "bad\xFF" "name",
                                                {6, 0, 123, 18, 1, 0, true});
    check(r, invalid.invalidUtf8 && !invalid.layoutError, "invalid_utf8_substitution");
    const auto original = UiTextLayout::measure(row, "Original",
                                                 {57, 2, 72, 16, 1, 0, true});
    check(r, !original.truncated && !original.layoutError, "footer_original_fits");
    row.setTextSize(1.25f);
    const auto mode = UiTextLayout::measure(row, "MODE?", {17, 2, 39, 16, 1, 0, true});
    check(r, !mode.truncated && !mode.layoutError, "footer_legacy_mode_fits");
    return r;
}

P3BCheckResult checkP3BOverlayRestoration(NowPlayingPresenter& presenter) {
    P3BCheckResult r;
    // Reuse the presenter's existing row, not another framebuffer. Called
    // before a test track starts. Only scratch pixels and display-only fields
    // are temporarily touched; actual speaker volume is never changed.
    const bool visible = presenter.model_.volumeVisible;
    const uint8_t savedVolume = presenter.model_.volume;
    auto rowHash = [&](int height) {
        uint32_t hash = 2166136261U;
        for (int i = 0; i < G::width * height; ++i) {
            hash = (hash ^ presenter.pixels_[i]) * 16777619U;
        }
        return hash;
    };
    for (uint8_t volume : {0, 128, 255}) {
        bool changed = false, restored = true;
        for (int offset = 0; offset < G::overlayHeight; offset += G::rowHeight) {
            const int height = std::min(G::rowHeight, G::overlayHeight - offset);
            presenter.model_.volumeVisible = false;
            presenter.drawContentSlice(G::overlayY + offset, height);
            const uint32_t original = rowHash(height);
            presenter.model_.volumeVisible = true;
            presenter.model_.volume = volume;
            presenter.drawContentSlice(G::overlayY + offset, height);
            changed |= original != rowHash(height);
            presenter.model_.volumeVisible = false;
            presenter.drawContentSlice(G::overlayY + offset, height);
            restored &= original == rowHash(height);
        }
        check(r, changed && restored, "overlay_background_restore");
    }
    presenter.model_.volumeVisible = visible;
    presenter.model_.volume = savedVolume;
    return r;
}

P3BCheckResult checkP3BPresenterDrawing(NowPlayingPresenter& presenter) {
    presenter.prepareRow(18,TFT_BLACK,1.0f);
    return checkP3BDrawing(presenter.row_);
}

void P3BValidation::begin(const char* path, uint32_t nowMs) {
    *this = P3BValidation{};
    if (!LibraryRuntime::isMetadataPath(path)) {
        audioFailure_ = "measurement_path";
        return;
    }
    std::strcpy(path_, path);
    started_ = true;
    startedMs_ = nowMs;
}

void P3BValidation::sample(const PlayerSnapshot& audio,
                            const NowPlayingPresenter& presenter, uint32_t nowMs) {
    if (!started_) return;
    const auto& model = presenter.model();
    if (audioFailure_ == nullptr) {
        if (!model.active || std::strcmp(path_, model.path) != 0) audioFailure_ = "measurement_track";
        else if (audio.state != PlayerState::Playing || audio.sampleRateHz != 44100 ||
                 audio.error != PlayerError::None || audio.audioError != AudioError::None ||
                 audio.audioErrorEvents || audio.backpressureEvents ||
                 audio.trackEndedEvents || audio.pcmSubmitGapMaxUs > 70000 ||
                 audio.pcmLastSubmitAgeUs > 70000) audioFailure_ = "audio_continuity";
        else if (samples_ && audio.pcmBuffersSinceReset < lastBufferCount_)
            audioFailure_ = "diagnostics_reset_inside_window";
    }
    ++samples_;
    elapsedMs_ = nowMs - startedMs_;
    lastBufferCount_ = audio.pcmBuffersSinceReset;
    audio_ = audio;
    render_ = presenter.stats();
    metadataState_ = model.metadataState;
    metadataWarning_ = model.metadataWarning;
    phase_ = model.phase;
    observedPhases_ = model.observedPhases;
    cycles_ = model.marqueeCycles;
    staleResults_ = model.rejectedMetadataResults;
    rawRepeat_ = static_cast<uint8_t>(model.repeat);
    rawShuffle_ = model.shuffle;
}

bool P3BValidation::writeLog(fs::FS& fs, const P3BCheckResult& model,
                              const P3BCheckResult& drawing,
                              const P3BCheckResult& overlay,
                              bool displayConfirmed, bool overlayConfirmed) const {
    const bool executed = started_ && samples_ > 1 && elapsedMs_ >= 10000;
    const bool displayPass = model.passed() && drawing.passed() && overlay.passed() &&
                             displayConfirmed && overlayConfirmed;
    const bool audioPass = audioFailure_ == nullptr && audio_.pcmBuffersSinceReset > 10;
    const char* failure = !executed ? "not_executed" :
                          !displayPass ? "p3b_display" :
                          !audioPass ? "p3b_audio" : "none";
    if (!fs.exists("/ADVWalkman")) fs.mkdir("/ADVWalkman");
    if (!fs.exists("/ADVWalkman/logs")) fs.mkdir("/ADVWalkman/logs");
    auto file = fs.open("/ADVWalkman/logs/p3b-last.txt", "w");
    if (!file) return false;
    file.printf("result=%s\nversion=%s\ntask_executed=%u\nprimary_failure=%s\n",
                 !executed ? "SKIPPED" : displayPass && audioPass ? "PASS" : "FAIL",
                 ADV_WALKMAN_VERSION, executed, failure);
    file.printf("track=%s\nmeasurement_ms=%lu\nsamples=%lu\n", path_,
                 static_cast<unsigned long>(elapsedMs_), static_cast<unsigned long>(samples_));
    file.printf("model_checks=%u\nmodel_failure=%s\ndraw_checks=%u\ndraw_failure=%s\n",
                 model.checks, model.failure ? model.failure : "none", drawing.checks,
                 drawing.failure ? drawing.failure : "none");
    file.printf("display_confirmed=%u\noverlay_confirmed=%u\nmetadata_state=%u\nmetadata_warning=%u\n",
                 displayConfirmed, overlayConfirmed, static_cast<unsigned>(metadataState_), metadataWarning_);
    file.printf("overlay_checks=%u\noverlay_failure=%s\n", overlay.checks,
                 overlay.failure ? overlay.failure : "none");
    file.printf("marquee_phase=%u\nmarquee_observed_phases=%u\nmarquee_cycles=%lu\nmetadata_stale_rejected=%lu\n",
                 static_cast<unsigned>(phase_), observedPhases_, static_cast<unsigned long>(cycles_),
                 static_cast<unsigned long>(staleResults_));
    file.printf("repeat_raw=%u\nshuffle_raw=%u\n", rawRepeat_, rawShuffle_);
    file.printf("draw_title=%lu\ndraw_artist=%lu\ndraw_time=%lu\ndraw_progress=%lu\ndraw_status=%lu\n",
                 static_cast<unsigned long>(render_.titleDraws), static_cast<unsigned long>(render_.artistDraws),
                 static_cast<unsigned long>(render_.timeDraws), static_cast<unsigned long>(render_.progressDraws),
                 static_cast<unsigned long>(render_.statusDraws));
    file.printf("content_slices=%lu\noverlay_slices=%lu\npage_clears=%lu\nrender_max_us=%lu\nminimum_heap=%lu\n",
                 static_cast<unsigned long>(render_.contentSlices), static_cast<unsigned long>(render_.overlaySlices),
                 static_cast<unsigned long>(render_.pageClears), static_cast<unsigned long>(render_.renderMaxUs),
                 static_cast<unsigned long>(render_.minimumHeap));
    file.printf("audio_failure=%s\naudio_state=%s\naudio_error=%s\naudio_error_events=%lu\nbackpressure=%lu\n",
                 audioFailure_ ? audioFailure_ : "none", playerStateName(audio_.state),
                 audioErrorName(audio_.audioError), static_cast<unsigned long>(audio_.audioErrorEvents),
                 static_cast<unsigned long>(audio_.backpressureEvents));
    file.printf("sample_rate=%lu\npcm_buffers=%lu\npcm_gap_max_us=%lu\n",
                 static_cast<unsigned long>(audio_.sampleRateHz), static_cast<unsigned long>(audio_.pcmBuffersSinceReset),
                 static_cast<unsigned long>(audio_.pcmSubmitGapMaxUs));
    const bool success = file.getWriteError() == 0;
    file.flush();
    file.close();
    return success;
}

}  // namespace player
}  // namespace adv_walkman
