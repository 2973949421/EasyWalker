#pragma once

#include <cstddef>
#include <cstdint>

namespace adv_walkman {
namespace player {
namespace library_cache {

constexpr uint32_t kIndexMagic = 0x494C5741u;  // "AWLI" little-endian.
constexpr uint16_t kIndexVersion = 1;
constexpr size_t kIndexHeaderSize = 20;
constexpr size_t kDataRecordHeaderSize = 4;

inline uint16_t readLe16(const uint8_t* input) {
    return static_cast<uint16_t>(input[0]) |
           (static_cast<uint16_t>(input[1]) << 8);
}

inline uint32_t readLe32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8) |
           (static_cast<uint32_t>(input[2]) << 16) |
           (static_cast<uint32_t>(input[3]) << 24);
}

inline void writeLe16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}

inline void writeLe32(uint8_t* output, uint32_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
    output[2] = static_cast<uint8_t>(value >> 16);
    output[3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace library_cache
}  // namespace player
}  // namespace adv_walkman
