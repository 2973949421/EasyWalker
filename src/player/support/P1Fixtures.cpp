#include "player/support/P1Fixtures.h"

#include "player/support/FixedTrackSource.h"

namespace adv_walkman {
namespace player {
namespace {

const char* const kValidPaths[] = {
    "/Music/ADVWalkmanTest/cbr320-44100.mp3",
    "/Music/ADVWalkmanTest/vbr-v0-44100.mp3",
    "/Music/ADVWalkmanTest/cbr320-48000.mp3",
};
const char* const kValidHashes[] = {
    "321c2e32214560c5328fa9d08d1961a6113ca02647bdf8116ae9536da443c8f9",
    "d5372e555b1e3fe0aa8d6c1c278ce0fd3c43a024a02390671f8a5dcadc87b4bf",
    "a83a3725cf1bad40decb22a4da2a4854d468700d219cd3eda2221573da3ab3a7",
};
constexpr uint32_t kValidSampleRates[] = {44100, 44100, 48000};

const char* const kSingle0[] = {kValidPaths[0]};
const char* const kSingle1[] = {kValidPaths[1]};
const char* const kSingle2[] = {kValidPaths[2]};
const char* const kCorrupt[] = {
    "/Music/ADVWalkmanTest/corrupt-truncated.mp3",
};
constexpr char kCorruptHash[] =
    "f3e164967fad1e502ba7979ae44fa626189c257b4cb58c6027fb6b1f89c98c7b";
const char* const kMissing[] = {
    "/Music/ADVWalkmanTest/missing-by-design.mp3",
};

FixedTrackSource validSource(kValidPaths, kP1ValidFixtureCount);
FixedTrackSource singleSources[] = {
    FixedTrackSource(kSingle0, 1),
    FixedTrackSource(kSingle1, 1),
    FixedTrackSource(kSingle2, 1),
};
FixedTrackSource corruptSource(kCorrupt, 1);
FixedTrackSource missingSource(kMissing, 1);

}  // namespace

TrackSource& p1AllValidFixtures() {
    return validSource;
}

TrackSource& p1SingleValidFixture(size_t index) {
    return singleSources[index < kP1ValidFixtureCount ? index : 0];
}

TrackSource& p1CorruptFixture() {
    return corruptSource;
}

TrackSource& p1MissingFixture() {
    return missingSource;
}

const char* p1FixturePath(size_t index) {
    return index < kP1ValidFixtureCount ? kValidPaths[index] : "";
}

const char* p1FixtureSha256(size_t index) {
    return index < kP1ValidFixtureCount ? kValidHashes[index] : "";
}

uint32_t p1FixtureSampleRate(size_t index) {
    return index < kP1ValidFixtureCount ? kValidSampleRates[index] : 0;
}

const char* p1CorruptFixturePath() {
    return kCorrupt[0];
}

const char* p1CorruptFixtureSha256() {
    return kCorruptHash;
}

}  // namespace player
}  // namespace adv_walkman
