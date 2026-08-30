#include "CoreTypes.h"

namespace adv_walkman {
namespace player {

const char* playerStateName(PlayerState state) {
    switch (state) {
        case PlayerState::Empty:
            return "EMPTY";
        case PlayerState::Stopped:
            return "STOPPED";
        case PlayerState::Playing:
            return "PLAYING";
        case PlayerState::Paused:
            return "PAUSED";
        case PlayerState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* repeatModeName(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::Off:
            return "OFF";
        case RepeatMode::All:
            return "ALL";
        case RepeatMode::One:
            return "ONE";
    }
    return "UNKNOWN";
}

const char* soundPresetName(SoundPreset preset) {
    switch (preset) {
        case SoundPreset::Original:
            return "ORIGINAL";
        case SoundPreset::Tape:
            return "TAPE";
        case SoundPreset::Radio:
            return "RADIO";
        case SoundPreset::VocalClear:
            return "VOCAL_CLEAR";
    }
    return "UNKNOWN";
}

const char* playerErrorName(PlayerError error) {
    switch (error) {
        case PlayerError::None:
            return "NONE";
        case PlayerError::InvalidArgument:
            return "INVALID_ARGUMENT";
        case PlayerError::QueueTooLarge:
            return "QUEUE_TOO_LARGE";
        case PlayerError::InvalidQueueSnapshot:
            return "INVALID_QUEUE_SNAPSHOT";
        case PlayerError::TrackPathUnavailable:
            return "TRACK_PATH_UNAVAILABLE";
        case PlayerError::AudioOpenFailed:
            return "AUDIO_OPEN_FAILED";
        case PlayerError::AudioOperationFailed:
            return "AUDIO_OPERATION_FAILED";
        case PlayerError::AudioEngineError:
            return "AUDIO_ENGINE_ERROR";
    }
    return "UNKNOWN";
}

}  // namespace player
}  // namespace adv_walkman
