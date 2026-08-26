#pragma once

#include <cstddef>
#include <cstdint>

#include "player/app/PlayerRuntime.h"
#include "player/ui/UiCoordinator.h"
#include "player/ui/UiTypes.h"

namespace adv_walkman {
namespace player {

class P3AGate final {
  public:
    void begin(int16_t displayWidth, int16_t displayHeight,
               uint8_t displayRotation);
    // Returns true only when the Gate consumes an action instead of forwarding
    // it to UiCoordinator (the initial orientation confirmation).
    bool beforeAction(UiAction action, const RawKeyEvent& raw, UiPage page);
    void service(UiCoordinator& ui, PlayerRuntime& player);

    const char* hint() const;
    bool finished() const;
    bool passed() const;

  private:
    enum class Step : uint8_t {
        Orientation,
        LibraryLeft,
        LibraryRight,
        LibraryEnter,
        WaitPlaylist,
        PlaylistUp,
        PlaylistDown,
        PlaylistEnter,
        WaitPlayer,
        PlayWait,
        PlayerBack,
        WaitPlaylistBack,
        PlaylistBack,
        WaitLibrary,
        SettingsOpen,
        WaitSettings,
        SettingsBack,
        WaitFinal,
        Passed,
        Failed,
    };

    static constexpr uint32_t kPlaybackObservationMs = 10000;
    static constexpr uint32_t kGateTimeoutMs = 300000;

    void setStep(Step step);
    void appendEvent(UiAction action, const RawKeyEvent& raw, UiPage page);
    void finish(bool pass, const char* reason, UiCoordinator& ui,
                PlayerRuntime& player);
    void writeLog(const char* reason, const UiCoordinator& ui,
                  const PlayerRuntime& player);

    Step step_ = Step::Orientation;
    uint32_t startedAtMs_ = 0;
    uint32_t playbackStartedAtMs_ = 0;
    int16_t displayWidth_ = 0;
    int16_t displayHeight_ = 0;
    uint8_t displayRotation_ = 0;
    uint32_t minimumHeap_ = UINT32_MAX;
    bool diagnosticsReset_ = false;
    bool logWritten_ = false;
    char reason_[64] = "running";
    char events_[768] = {};
    size_t eventLength_ = 0;
};

}  // namespace player
}  // namespace adv_walkman
