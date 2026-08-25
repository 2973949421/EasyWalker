#pragma once

#include <Arduino.h>

namespace adv_walkman {
namespace player {

enum class AudioState : uint8_t {
    Empty,
    Loading,
    Playing,
    Paused,
    Draining,
    Stopped,
    Error,
};

enum class AudioError : uint8_t {
    None,
    NotInitialized,
    InvalidArgument,
    PathTooLong,
    FileOpenFailed,
    ProbeFailed,
    UnsupportedFormat,
    SeekFailed,
    DecoderBeginFailed,
    DecoderFailed,
    ReadFailed,
    SpeakerBeginFailed,
};

enum class AudioEventType : uint8_t {
    TrackEnded,
    Error,
};

struct AudioEvent {
    AudioEventType type = AudioEventType::Error;
    AudioError error = AudioError::None;
};

struct AudioStatus {
    AudioState state = AudioState::Empty;
    AudioError error = AudioError::None;
    uint32_t positionMs = 0;
    uint32_t durationMs = 0;
    uint32_t sourceByteOffset = 0;
    uint32_t sampleRateHz = 0;
    uint16_t bitrateKbps = 0;
    bool variableBitrate = false;
    uint32_t backpressureEvents = 0;
    uint32_t serviceMaxUs = 0;
};

const char* audioStateName(AudioState state);
const char* audioErrorName(AudioError error);

}  // namespace player
}  // namespace adv_walkman
