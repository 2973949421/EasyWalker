#include "player/support/AdvStorage.h"

#include <SD.h>
#include <SPI.h>

namespace adv_walkman {
namespace player {
namespace {

constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
constexpr int kSdMosi = 14;
constexpr int kSdCs = 12;
constexpr uint32_t kSdFrequencyHz = 25000000UL;
bool mounted = false;

}  // namespace

bool mountAdvSd() {
    if (mounted) {
        return true;
    }
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    mounted = SD.begin(kSdCs, SPI, kSdFrequencyHz);
    return mounted;
}

}  // namespace player
}  // namespace adv_walkman
