#pragma once

#include <cstddef>
#include <cstdint>

namespace adv_walkman {
namespace player {

constexpr size_t kMaxQueueTracks = 1024;
constexpr size_t kMaxTrackPathBytes = 511;
constexpr size_t kTrackPathCapacity = kMaxTrackPathBytes + 1;
constexpr size_t kPreviousHistoryCapacity = 32;
constexpr uint32_t kPreviousRestartThresholdMs = 5000;

enum class PlayerState : uint8_t {
    Empty,
    Stopped,
    Playing,
    Paused,
    Error,
};

enum class RepeatMode : uint8_t {
    Off = 0,
    All = 1,
    One = 2,
};

enum class SoundPreset : uint8_t {
    Original = 0,
    Tape = 1,
    Radio = 2,
    VocalClear = 3,
};

enum class PlayerError : uint8_t {
    None,
    InvalidArgument,
    QueueTooLarge,
    InvalidQueueSnapshot,
    TrackPathUnavailable,
    AudioOpenFailed,
    AudioOperationFailed,
    AudioEngineError,
};

const char* playerStateName(PlayerState state);
const char* repeatModeName(RepeatMode mode);
const char* soundPresetName(SoundPreset preset);
const char* playerErrorName(PlayerError error);

}  // namespace player
}  // namespace adv_walkman
