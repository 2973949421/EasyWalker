#include "player/support/AdvStorage.h"

#include <SD.h>
#include <SPI.h>
#include <ff.h>

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

// ESP-IDF allocates this global mount table, separately from the media work
// set. Include its o_append byte per slot. FIL contains a 4096-byte cache in
// the pinned SDK, even for 512-byte SD sectors: do not call this 'only 7 KiB'.
size_t additionalSdFileSlotsBytes(){return (kAdvSdMaxFiles-5)*(sizeof(FIL)+sizeof(bool));}
static_assert(sizeof(FIL)==4136,"Re-audit global FatFs cost if SDK layout changes");

bool mountAdvSd() {
    if (mounted) {
        return true;
    }
    SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
    mounted = SD.begin(kSdCs, SPI, kSdFrequencyHz, "/sd", kAdvSdMaxFiles);
    return mounted;
}

}  // namespace player
}  // namespace adv_walkman
