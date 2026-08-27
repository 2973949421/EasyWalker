#pragma once
#include <cstdint>
#include <cstddef>

namespace adv_walkman { namespace player {
enum class MediaState : uint8_t { Idle, Loading, Ready, Missing, Error };
enum class MediaView : uint8_t { Lyrics = 0, Cover = 1 };
inline uint16_t mediaU16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
inline uint32_t mediaU32(const uint8_t* p) { return mediaU16(p) | (uint32_t(mediaU16(p+2)) << 16); }
// Returns a scalar and advances without splitting UTF-8. Invalid input is
// reported, never silently accepted as a valid glyph.
inline uint32_t mediaCodepoint(const char*& p, bool& invalid) {
    const auto* s = reinterpret_cast<const uint8_t*>(p);
    if (!*s) return 0;
    uint32_t cp = *s; unsigned n = 1;
    if (*s >= 0xC2 && *s <= 0xDF) { cp = *s & 31; n = 2; }
    else if (*s >= 0xE0 && *s <= 0xEF) { cp = *s & 15; n = 3; }
    else if (*s >= 0xF0 && *s <= 0xF4) { cp = *s & 7; n = 4; }
    else if (*s >= 0x80) { ++p; invalid = true; return '?'; }
    for (unsigned i=1;i<n;++i) {
        if ((s[i]&0xC0)!=0x80) { ++p; invalid=true; return '?'; }
        cp=(cp<<6)|(s[i]&63);
    }
    if ((n==3 && cp<0x800) || (n==4 && cp<0x10000) || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF)) {
        ++p; invalid=true; return '?';
    }
    p+=n; return cp;
}
inline uint32_t mediaCrc(uint32_t crc, const uint8_t* bytes, size_t length) {
    while (length--) { crc ^= *bytes++; for (int b=0;b<8;++b) crc=(crc>>1)^(0xEDB88320U & (0U-(crc&1U))); }
    return crc;
}
bool mediaResourcePath(const char* track, const char* root, const char* suffix,
                       char* output, size_t capacity);
} }
