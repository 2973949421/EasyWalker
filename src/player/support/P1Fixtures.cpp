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
    "4d8743d2c6f65eba019ca0163f8a7c6bbc176be803297db524ec5454cf39525a",
    "565a7068e4b2e2997a70a43f5615a45005825eae694a114f9277d905d7ff8478",
    "691b7c6f9ec52cd3ee0fd6c2188650e276818799ddba51f7479d88230b159afc",
};
constexpr uint32_t kValidSampleRates[] = {44100, 44100, 48000};

const char* const kSingle0[] = {kValidPaths[0]};
const char* const kSingle1[] = {kValidPaths[1]};
const char* const kSingle2[] = {kValidPaths[2]};
const char* const kCorrupt[] = {
    "/Music/ADVWalkmanTest/corrupt-truncated.mp3",
};
constexpr char kCorruptHash[] =
    "8d272c7e0c90a66aaa6b004976f0e894c3b0e19b67f2ead5d94bfc025dbbbbd9";
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
