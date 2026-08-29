#pragma once
namespace adv_walkman { namespace player {
enum class PlaybackPageRoute { None, CurrentFolder, Player };
// Navigation only. Transport is deliberately absent from the return type.
constexpr PlaybackPageRoute playbackPageRoute(bool onPlayer,bool hasCurrent) {
    return !hasCurrent ? PlaybackPageRoute::None :
           onPlayer ? PlaybackPageRoute::CurrentFolder : PlaybackPageRoute::Player;
}
} }
