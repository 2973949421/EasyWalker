#include "P3AGate.h"

#include <Arduino.h>
#include <SD.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace adv_walkman {
namespace player {

void P3AGate::begin(int16_t displayWidth, int16_t displayHeight,
                    uint8_t displayRotation) {
    displayWidth_ = displayWidth;
    displayHeight_ = displayHeight;
    displayRotation_ = displayRotation;
    startedAtMs_ = millis();
    minimumHeap_ = ESP.getFreeHeap();
    setStep(Step::Orientation);
}

bool P3AGate::beforeAction(UiAction action, const RawKeyEvent& raw,
                           UiPage page) {
    if (finished()) {
        return false;
    }
    appendEvent(action, raw, page);
    switch (step_) {
        case Step::Orientation:
            if (action == UiAction::Confirm) {
                setStep(Step::LibraryLeft);
                return true;
            }
            break;
        case Step::LibraryLeft:
            if (page == UiPage::Library && action == UiAction::Left) {
                setStep(Step::LibraryRight);
            }
            break;
        case Step::LibraryRight:
            if (page == UiPage::Library && action == UiAction::Right) {
                setStep(Step::LibraryEnter);
            }
            break;
        case Step::LibraryEnter:
            if (page == UiPage::Library && action == UiAction::Confirm) {
                setStep(Step::WaitPlaylist);
            }
            break;
        case Step::PlaylistUp:
            if (page == UiPage::Playlist && action == UiAction::Up) {
                setStep(Step::PlaylistDown);
            }
            break;
        case Step::PlaylistDown:
            if (page == UiPage::Playlist && action == UiAction::Down) {
                setStep(Step::PlaylistEnter);
            }
            break;
        case Step::PlaylistEnter:
            if (page == UiPage::Playlist && action == UiAction::Confirm) {
                setStep(Step::WaitPlayer);
            }
            break;
        case Step::PlayerBack:
            if (page == UiPage::Player && action == UiAction::Back) {
                setStep(Step::WaitPlaylistBack);
            }
            break;
        case Step::PlaylistBack:
            if (page == UiPage::Playlist && action == UiAction::Back) {
                setStep(Step::WaitLibrary);
            }
            break;
        case Step::SettingsOpen:
            if (page == UiPage::Library && action == UiAction::OpenSettings) {
                setStep(Step::WaitSettings);
            }
            break;
        case Step::SettingsBack:
            if (page == UiPage::Settings && action == UiAction::Back) {
                setStep(Step::WaitFinal);
            }
            break;
        default:
            break;
    }
    return false;
}

void P3AGate::service(UiCoordinator& ui, PlayerRuntime& player) {
    if (finished()) {
        return;
    }
    minimumHeap_ = std::min(minimumHeap_, ESP.getFreeHeap());
    const uint32_t now = millis();
    if (now - startedAtMs_ > kGateTimeoutMs) {
        finish(false, "timeout", ui, player);
        return;
    }

    const PlayerSnapshot snapshot = player.snapshot();
    switch (step_) {
        case Step::WaitPlaylist:
            if (ui.page() == UiPage::Playlist) {
                setStep(Step::PlaylistUp);
            }
            break;
        case Step::WaitPlayer:
            if (ui.page() == UiPage::Player &&
                snapshot.state == PlayerState::Playing &&
                snapshot.sampleRateHz == 44100 &&
                snapshot.pcmBuffersSinceReset >= 5) {
                player.resetDiagnostics();
                diagnosticsReset_ = true;
                playbackStartedAtMs_ = now;
                setStep(Step::PlayWait);
            }
            break;
        case Step::PlayWait:
            if (snapshot.state != PlayerState::Playing ||
                snapshot.error != PlayerError::None ||
                snapshot.audioError != AudioError::None) {
                finish(false, "playback_error", ui, player);
            } else if (now - playbackStartedAtMs_ >= kPlaybackObservationMs) {
                setStep(Step::PlayerBack);
            }
            break;
        case Step::WaitPlaylistBack:
            if (ui.page() == UiPage::Playlist) {
                if (snapshot.state != PlayerState::Playing) {
                    finish(false, "audio_stopped_on_back", ui, player);
                } else {
                    setStep(Step::PlaylistBack);
                }
            }
            break;
        case Step::WaitLibrary:
            if (ui.page() == UiPage::Library) {
                if (snapshot.state != PlayerState::Playing) {
                    finish(false, "audio_stopped_on_library", ui, player);
                } else {
                    setStep(Step::SettingsOpen);
                }
            }
            break;
        case Step::WaitSettings:
            if (ui.page() == UiPage::Settings) {
                setStep(Step::SettingsBack);
            }
            break;
        case Step::WaitFinal:
            if (ui.page() == UiPage::Library) {
                char path[kTrackPathCapacity] = {};
                const bool pathOk = player.currentPath(path, sizeof(path)) &&
                    std::strcmp(path,
                                "/Music/ADVWalkmanBenchmark/benchmark.mp3") == 0;
                const bool audioOk = snapshot.state == PlayerState::Playing &&
                    snapshot.error == PlayerError::None &&
                    snapshot.audioError == AudioError::None &&
                    snapshot.sampleRateHz == 44100 &&
                    snapshot.backpressureEvents == 0 &&
                    snapshot.pcmSubmitGapOver100Ms == 0 &&
                    snapshot.pcmBuffersSinceReset != 0;
                finish(pathOk && audioOk && diagnosticsReset_,
                       !pathOk ? "unexpected_track"
                               : (!audioOk ? "audio_continuity" : "pass"),
                       ui, player);
            }
            break;
        default:
            break;
    }
}

const char* P3AGate::hint() const {
    switch (step_) {
        case Step::Orientation:
            return "Jack UP + upright? Enter";
        case Step::LibraryLeft:
            return "Gate: Fn+Left";
        case Step::LibraryRight:
            return "Gate: Fn+Right";
        case Step::LibraryEnter:
            return "Gate: Enter library";
        case Step::WaitPlaylist:
            return "Opening playlist...";
        case Step::PlaylistUp:
            return "Gate: Fn+Up";
        case Step::PlaylistDown:
            return "Gate: Fn+Down";
        case Step::PlaylistEnter:
            return "Gate: Enter benchmark";
        case Step::WaitPlayer:
            return "Starting 44.1k audio...";
        case Step::PlayWait:
            return "Listen/wait 10 seconds";
        case Step::PlayerBack:
            return "Gate: Fn+Esc to list";
        case Step::WaitPlaylistBack:
            return "Restoring playlist...";
        case Step::PlaylistBack:
            return "Gate: Fn+Esc to library";
        case Step::WaitLibrary:
            return "Opening library...";
        case Step::SettingsOpen:
            return "Gate: press S";
        case Step::WaitSettings:
            return "Opening settings...";
        case Step::SettingsBack:
            return "Gate: Fn+Esc";
        case Step::WaitFinal:
            return "Checking audio + log...";
        case Step::Passed:
            return "P3A PASS - return SD to PC";
        case Step::Failed:
            return "P3A FAIL - return SD to PC";
    }
    return "";
}

bool P3AGate::finished() const {
    return step_ == Step::Passed || step_ == Step::Failed;
}

bool P3AGate::passed() const {
    return step_ == Step::Passed;
}

void P3AGate::setStep(Step step) {
    step_ = step;
}

void P3AGate::appendEvent(UiAction action, const RawKeyEvent& raw,
                          UiPage page) {
    if (eventLength_ >= sizeof(events_) - 1) {
        return;
    }
    const int written = std::snprintf(
        events_ + eventLength_, sizeof(events_) - eventLength_,
        "%s:%s:x%d:y%d:fn%d:n%u|", uiPageName(page), uiActionName(action),
        static_cast<int>(raw.x), static_cast<int>(raw.y), raw.fn ? 1 : 0,
        static_cast<unsigned>(raw.keyCount));
    if (written > 0) {
        eventLength_ += std::min(static_cast<size_t>(written),
                                sizeof(events_) - eventLength_ - 1);
    }
}

void P3AGate::finish(bool pass, const char* reason, UiCoordinator& ui,
                     PlayerRuntime& player) {
    std::strncpy(reason_, reason == nullptr ? "unknown" : reason,
                 sizeof(reason_) - 1);
    setStep(pass ? Step::Passed : Step::Failed);
    writeLog(reason_, ui, player);
}

void P3AGate::writeLog(const char* reason, const UiCoordinator& ui,
                       const PlayerRuntime& player) {
    if (logWritten_) {
        return;
    }
    SD.mkdir("/ADVWalkman");
    SD.mkdir("/ADVWalkman/logs");
    constexpr const char* path = "/ADVWalkman/logs/p3a-last.txt";
    SD.remove(path);
    File file = SD.open(path, FILE_WRITE);
    if (!file) {
        return;
    }
    const PlayerSnapshot snapshot = player.snapshot();
    const UiStats uiStats = ui.stats();
    char track[kTrackPathCapacity] = {};
    player.currentPath(track, sizeof(track));
    file.printf("result=%s\n", passed() ? "PASS" : "FAIL");
    file.printf("version=%s\n", ADV_WALKMAN_VERSION);
    file.printf("reason=%s\n", reason == nullptr ? "unknown" : reason);
    file.printf("orientation=%dx%d rotation=%u\n", displayWidth_,
                displayHeight_, static_cast<unsigned>(displayRotation_));
    file.printf("events=%s\n", events_);
    file.printf("page=%s page_transitions=%lu input_events=%lu\n",
                uiPageName(ui.page()),
                static_cast<unsigned long>(uiStats.pageTransitions),
                static_cast<unsigned long>(uiStats.inputEvents));
    file.printf("selected_path=%s\n", track[0] == '\0' ? "none" : track);
    file.printf("player_state=%s player_error=%s audio_error=%s\n",
                playerStateName(snapshot.state), playerErrorName(snapshot.error),
                audioErrorName(snapshot.audioError));
    file.printf("sample_rate=%lu backpressure=%lu pcm_buffers=%lu\n",
                static_cast<unsigned long>(snapshot.sampleRateHz),
                static_cast<unsigned long>(snapshot.backpressureEvents),
                static_cast<unsigned long>(snapshot.pcmBuffersSinceReset));
    file.printf("pcm_gap_max_us=%lu pcm_gap_over_100ms=%lu\n",
                static_cast<unsigned long>(snapshot.pcmSubmitGapMaxUs),
                static_cast<unsigned long>(snapshot.pcmSubmitGapOver100Ms));
    file.printf("render_count=%lu render_max_us=%lu\n",
                static_cast<unsigned long>(uiStats.renderCount),
                static_cast<unsigned long>(uiStats.renderMaxUs));
    file.printf("minimum_heap=%lu ui_minimum_heap=%lu\n",
                static_cast<unsigned long>(minimumHeap_),
                static_cast<unsigned long>(uiStats.minimumHeap));
    file.printf("elapsed_ms=%lu\n",
                static_cast<unsigned long>(millis() - startedAtMs_));
    file.flush();
    file.close();
    logWritten_ = true;
}

}  // namespace player
}  // namespace adv_walkman
