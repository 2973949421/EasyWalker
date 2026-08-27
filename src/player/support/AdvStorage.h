#pragma once
#include <cstdint>
#include <cstddef>

namespace adv_walkman {
namespace player {

// Uses the Cardputer ADV microSD mapping from the official M5Cardputer ADV
// examples: SCK 40, MISO 39, MOSI 14, CS 12 at 25 MHz.
constexpr uint8_t kAdvSdMaxFiles = 12;
size_t additionalSdFileSlotsBytes();
bool mountAdvSd();

}  // namespace player
}  // namespace adv_walkman
