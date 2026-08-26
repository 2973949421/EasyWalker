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
        char reason[64] = {};
        std::snprintf(reason, sizeof(reason), "timeout_%s", stepName(step_));
        finish(false, reason, ui, player);
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
            return "STEP 01 / 11\nPRESS ENTER";
        case Step::LibraryLeft:
            return "STEP 02 / 11\nFN + LEFT";
        case Step::LibraryRight:
            return "STEP 03 / 11\nFN + RIGHT";
        case Step::LibraryEnter:
            return "STEP 04 / 11\nPRESS ENTER";
        case Step::WaitPlaylist:
            return "PLEASE WAIT\nOPENING LIST";
        case Step::PlaylistUp:
            return "STEP 05 / 11\nFN + UP";
        case Step::PlaylistDown:
            return "STEP 06 / 11\nFN + DOWN";
        case Step::PlaylistEnter:
            return "STEP 07 / 11\nPRESS ENTER";
        case Step::WaitPlayer:
            return "PLEASE WAIT\nSTARTING AUDIO";
        case Step::PlayWait:
            return "AUTO CHECK\nPLAYING 10 SEC";
        case Step::PlayerBack:
            return "STEP 08 / 11\nFN + ESC";
        case Step::WaitPlaylistBack:
            return "PLEASE WAIT\nRESTORE LIST";
        case Step::PlaylistBack:
            return "STEP 09 / 11\nFN + ESC";
        case Step::WaitLibrary:
            return "PLEASE WAIT\nOPEN LIBRARY";
        case Step::SettingsOpen:
            return "STEP 10 / 11\nPRESS S";
        case Step::WaitSettings:
            return "PLEASE WAIT\nOPEN SETTINGS";
        case Step::SettingsBack:
            return "STEP 11 / 11\nFN + ESC";
        case Step::WaitFinal:
            return "PLEASE WAIT\nFINAL CHECK";
        case Step::Passed:
            return "P3A PASS\nSD TO PC";
        case Step::Failed:
            return "P3A FAIL\nSD TO PC";
    }
    return "";
}

void P3AGate::renderResult(M5GFX& display) {
    if (resultRendered_) {
        return;
    }
    display.fillScreen(TFT_BLACK);
    display.setTextWrap(false);
    display.setTextColor(passed() ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    display.setTextSize(1.4f);
    display.setCursor(23, 24);
    display.print("P3A GATE");
    display.setTextSize(3.0f);
    display.setCursor(passed() ? 22 : 20, 72);
    display.print(passed() ? "PASS" : "FAIL");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1.25f);
    if (!passed()) {
        display.setCursor(7, 126);
        display.printf("%.17s", reason_);
    }
    display.setCursor(19, 166);
    display.print("POWER OFF");
    display.setCursor(23, 188);
    display.print("SD -> PC");
    resultRendered_ = true;
}

const char* P3AGate::stepName(Step step) {
    switch (step) {
        case Step::Orientation: return "orientation";
        case Step::LibraryLeft: return "library_left";
        case Step::LibraryRight: return "library_right";
        case Step::LibraryEnter: return "library_enter";
        case Step::WaitPlaylist: return "wait_playlist";
        case Step::PlaylistUp: return "playlist_up";
        case Step::PlaylistDown: return "playlist_down";
        case Step::PlaylistEnter: return "playlist_enter";
        case Step::WaitPlayer: return "wait_player";
        case Step::PlayWait: return "play_wait";
        case Step::PlayerBack: return "player_back";
        case Step::WaitPlaylistBack: return "wait_playlist_back";
        case Step::PlaylistBack: return "playlist_back";
        case Step::WaitLibrary: return "wait_library";
        case Step::SettingsOpen: return "settings_open";
        case Step::WaitSettings: return "wait_settings";
        case Step::SettingsBack: return "settings_back";
        case Step::WaitFinal: return "wait_final";
        case Step::Passed: return "passed";
        case Step::Failed: return "failed";
    }
    return "unknown";
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
    file.printf("final_step=%s\n", stepName(step_));
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
