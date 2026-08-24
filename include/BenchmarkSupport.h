#pragma once

#include <Arduino.h>

namespace adv_walkman {

constexpr uint32_t kBenchmarkBitrateBitsPerSecond = 320000;

int16_t downmixStereoToMono(int16_t left, int16_t right);
uint32_t benchmarkByteOffsetForSeconds(uint32_t seconds, uint32_t fileSize);
bool mountBenchmarkSd();
bool computeBenchmarkFileSha256(const char* path, char output[65]);

}  // namespace adv_walkman
