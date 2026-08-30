#pragma once

#include "PlayerKeys.h"
#include "UiTypes.h"
#include "player/core/CoreTypes.h"

namespace adv_walkman {
namespace player {

struct PlaybackModeValue {
    RepeatMode repeat;
    bool shuffle;
    constexpr PlaybackModeValue(RepeatMode value, bool shuffled)
        : repeat(value), shuffle(shuffled) {}
};

enum class PlaybackModeIcon : uint8_t {
    ListLoop,
    RepeatOne,
    ShuffleLoop,
    ListOnce,
    Invalid,
};

constexpr PlaybackModeIcon playbackModeIcon(RepeatMode repeat, bool shuffle) {
    return shuffle ? (repeat == RepeatMode::Off ? PlaybackModeIcon::ShuffleLoop
                                                : PlaybackModeIcon::Invalid)
                   : repeat == RepeatMode::All ? PlaybackModeIcon::ListLoop
                   : repeat == RepeatMode::One ? PlaybackModeIcon::RepeatOne
                   : repeat == RepeatMode::Off ? PlaybackModeIcon::ListOnce
                                               : PlaybackModeIcon::Invalid;
}

constexpr char playbackModeIconLabel(PlaybackModeIcon icon) {
    return icon == PlaybackModeIcon::RepeatOne ? '1'
         : icon == PlaybackModeIcon::ShuffleLoop ? 'R'
         : icon == PlaybackModeIcon::ListOnce ? 'S'
         : icon == PlaybackModeIcon::Invalid ? '?' : '\0';
}

constexpr PlaybackModeValue nextPlaybackMode(RepeatMode repeat, bool shuffle) {
    // A legacy Repeat+Shuffle combination is intentionally left untouched
    // until the user presses Play Mode; that explicit action normalizes it.
    return shuffle
               ? PlaybackModeValue(RepeatMode::Off, false)
               : repeat == RepeatMode::Off
               ? PlaybackModeValue{RepeatMode::One, false}
               : repeat == RepeatMode::One
                     ? PlaybackModeValue{RepeatMode::All, false}
                     : repeat == RepeatMode::All
                           ? PlaybackModeValue{RepeatMode::Off, true}
                           : PlaybackModeValue{RepeatMode::Off, false};
}

constexpr UiAction routedActionAt(UiPage page, int x, int y) {
    return x == 0 && y == 0
               ? UiAction::Back
               : x == 5 && y == 1
                     ? UiAction::SaveDiagnostics
                     : x == 0 && y == 1
                           ? UiAction::ToggleCurrentPlaybackPage
                           : page == UiPage::Player
                                 ? (playerKeyAt(x, y) == PlayerKey::VolumeUp
                   ? UiAction::VolumeUp
                   : playerKeyAt(x, y) == PlayerKey::VolumeDown
                         ? UiAction::VolumeDown
                         : playerKeyAt(x, y) == PlayerKey::TogglePlayback
                               ? UiAction::TogglePlayback
                               : playerKeyAt(x, y) == PlayerKey::View
                                     ? UiAction::ToggleView
                                     : playerKeyAt(x, y) == PlayerKey::PreviousTrack
                                           ? UiAction::PreviousTrack
                                           : playerKeyAt(x, y) == PlayerKey::NextTrack
                                                 ? UiAction::NextTrack
                                                 : playerKeyAt(x, y) == PlayerKey::CyclePlayMode
                                                       ? UiAction::CyclePlayMode
                                                       : UiAction::None)
                                 : page == UiPage::Playlist
                                       ? (x == 11 && y == 2
                   ? UiAction::Up
                   : x == 11 && y == 3
                         ? UiAction::Down
                         : x == 13 && y == 2 ? UiAction::Confirm : UiAction::None)
                                       : page == UiPage::Library
                                             ? (x == 10 && y == 3
                   ? UiAction::Left
                   : x == 12 && y == 3
                         ? UiAction::Right
                         : x == 13 && y == 2
                               ? UiAction::Confirm
                               : x == 3 && y == 2 ? UiAction::OpenSettings
                                                  : UiAction::None)
                                             : x == 11 && y == 2
               ? UiAction::Up
               : x == 10 && y == 3
                     ? UiAction::Left
                     : x == 11 && y == 3
                           ? UiAction::Down
                           : x == 12 && y == 3
                                 ? UiAction::Right
                                 : x == 13 && y == 2 ? UiAction::Confirm
                                                    : UiAction::None;
}

static_assert(playerKeyAt(13, 0) == PlayerKey::VolumeUp, "P4 key 1");
static_assert(playerKeyAt(13, 1) == PlayerKey::TogglePlayback, "P4 key 2");
static_assert(playerKeyAt(13, 2) == PlayerKey::TogglePlayback, "P4 key 3");
static_assert(playerKeyAt(13, 3) == PlayerKey::PreviousTrack, "P4 key 4");
static_assert(playerKeyAt(12, 0) == PlayerKey::VolumeDown, "P4 key 5");
static_assert(playerKeyAt(12, 1) == PlayerKey::View, "P4 key 6");
static_assert(playerKeyAt(12, 2) == PlayerKey::CyclePlayMode, "P4 key 7");
static_assert(playerKeyAt(12, 3) == PlayerKey::NextTrack, "P4 key 8");
static_assert(playerKeyAt(11, 0) == PlayerKey::None &&
                  playerKeyAt(11, 1) == PlayerKey::None &&
                  playerKeyAt(11, 2) == PlayerKey::None &&
                  playerKeyAt(11, 3) == PlayerKey::None,
              "P4 keys 9-12 remain disabled");
static_assert(nextPlaybackMode(RepeatMode::Off, false).repeat == RepeatMode::One,
              "Normal to Repeat One");
static_assert(nextPlaybackMode(RepeatMode::One, false).repeat == RepeatMode::All,
              "Repeat One to Repeat All");
static_assert(nextPlaybackMode(RepeatMode::All, false).shuffle,
              "Repeat All to Shuffle");
static_assert(!nextPlaybackMode(RepeatMode::Off, true).shuffle,
              "Shuffle to Normal");
static_assert(nextPlaybackMode(RepeatMode::One, true).repeat == RepeatMode::Off &&
                  !nextPlaybackMode(RepeatMode::One, true).shuffle,
              "legacy invalid mode normalizes only on explicit action");
static_assert(playbackModeIcon(RepeatMode::All, false) == PlaybackModeIcon::ListLoop &&
                  playbackModeIconLabel(PlaybackModeIcon::ListLoop) == '\0',
              "list loop uses the unlabelled loop icon");
static_assert(playbackModeIcon(RepeatMode::One, false) == PlaybackModeIcon::RepeatOne &&
                  playbackModeIconLabel(PlaybackModeIcon::RepeatOne) == '1',
              "repeat one uses the 1 loop icon");
static_assert(playbackModeIcon(RepeatMode::Off, true) == PlaybackModeIcon::ShuffleLoop &&
                  playbackModeIconLabel(PlaybackModeIcon::ShuffleLoop) == 'R',
              "shuffle loop uses the R loop icon");
static_assert(playbackModeIcon(RepeatMode::Off, false) == PlaybackModeIcon::ListOnce &&
                  playbackModeIconLabel(PlaybackModeIcon::ListOnce) == 'S',
              "list once uses the S loop icon");
static_assert(routedActionAt(UiPage::Player, 12, 3) == UiAction::NextTrack,
              "player context owns blind key 8");
static_assert(routedActionAt(UiPage::Library, 12, 3) == UiAction::Right,
              "library context owns right navigation");
static_assert(routedActionAt(UiPage::Playlist, 12, 3) == UiAction::None,
              "playlist does not leak key 8");
static_assert(routedActionAt(UiPage::Settings, 12, 3) == UiAction::Right,
              "settings keeps right navigation");
static_assert(routedActionAt(UiPage::Settings, 0, 1) == UiAction::ToggleCurrentPlaybackPage,
              "settings Tab returns to Player");
static_assert(routedActionAt(UiPage::Settings, 3, 2) == UiAction::None,
              "settings does not own S");

}  // namespace player
}  // namespace adv_walkman
