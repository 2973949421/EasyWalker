#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
struct MediaLayout {
    static constexpr int height=188, top=6, bottom=8;
    static constexpr int lyricHeight=height-top-bottom, cell=14, pitch=16;
    static constexpr int columns=7, bilingualGap=6;
    static constexpr uint8_t cjkFace=1;
    static constexpr int coverTop=(height-144)/2;
};
// A ready cover must never monopolize the cooperative resource worker.
enum class MediaWork : uint8_t { None, Font, Lyrics, Cover };
constexpr MediaWork chooseMediaWork(unsigned turn,bool font,bool lyrics,bool cover) {
    return turn%3==0 ? (font?MediaWork::Font:lyrics?MediaWork::Lyrics:cover?MediaWork::Cover:MediaWork::None)
         : turn%3==1 ? (lyrics?MediaWork::Lyrics:cover?MediaWork::Cover:font?MediaWork::Font:MediaWork::None)
         : (cover?MediaWork::Cover:font?MediaWork::Font:lyrics?MediaWork::Lyrics:MediaWork::None);
}
constexpr bool rotateVerticalPunctuation(uint32_t c) {
    return c==0x300A||c==0x300B||c==0x300C||c==0x300D||c==0x300E||c==0x300F||
           c==0x201C||c==0x201D||c==0x2018||c==0x2019||c==0xFF08||c==0xFF09||c==0x2014;
}
constexpr bool smallVerticalPunctuation(uint32_t c) {
    return c==0xFF0C||c==0x3001||c==0x3002;
}
} }
