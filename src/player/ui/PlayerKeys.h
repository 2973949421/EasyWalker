#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
enum class PlayerKey : uint8_t {None, VolumeUp, VolumeDown, TogglePlayback, View};
// Portrait rows are official keyboard x=13,12,11; columns are y=0..3.
// M5Cardputer 1.1.1 Keyboard.h keymap + PRD physical 3x4 positions.
constexpr PlayerKey playerKeyAt(int x,int y) {
    return x==13&&y==0?PlayerKey::VolumeUp:
           x==12&&y==0?PlayerKey::VolumeDown:
           x==13&&(y==1||y==2)?PlayerKey::TogglePlayback:
           x==12&&y==1?PlayerKey::View:PlayerKey::None;
}
constexpr uint8_t adjustedVolume(uint8_t current,int delta) {
    return current+delta<0?0:current+delta>255?255:current+delta;
}
} }
