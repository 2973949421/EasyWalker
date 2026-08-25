#pragma once

#include <cstddef>
#include <cstdint>

#include "player/core/TrackSource.h"

namespace adv_walkman {
namespace player {

constexpr size_t kP1ValidFixtureCount = 3;

TrackSource& p1AllValidFixtures();
TrackSource& p1SingleValidFixture(size_t index);
TrackSource& p1CorruptFixture();
TrackSource& p1MissingFixture();
const char* p1FixturePath(size_t index);
const char* p1FixtureSha256(size_t index);
uint32_t p1FixtureSampleRate(size_t index);
const char* p1CorruptFixturePath();
const char* p1CorruptFixtureSha256();

}  // namespace player
}  // namespace adv_walkman
