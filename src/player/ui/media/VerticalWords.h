#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
// English words, contractions and hyphenated terms. CJK / spaces / other
// punctuation end the token. No allocation and lookahead stops past one column.
constexpr bool englishWordChar(uint32_t c) {
    return (c>='A' && c<='Z') || (c>='a' && c<='z') ||
           (c>='0' && c<='9') || c=='\'' || c=='-';
}
template<class Advance>
constexpr int englishWordHeight(const char* text,int height,Advance advance) {
    int pixels=0;
    while(englishWordChar(static_cast<unsigned char>(*text)) && pixels<=height)
        pixels+=advance(static_cast<unsigned char>(*text++));
    return pixels;
}
constexpr bool nextVerticalColumn(int y,int advance,int wordHeight,int height) {
    return y+advance>height ||
        (y>0 && wordHeight>0 && wordHeight<=height && y+wordHeight>height);
}
} }
