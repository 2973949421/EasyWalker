#include "player/audio/AudioTypes.h"

namespace adv_walkman {
namespace player {

const char* audioStateName(AudioState state) {
    switch (state) {
        case AudioState::Empty:
            return "EMPTY";
        case AudioState::Loading:
            return "LOADING";
        case AudioState::Playing:
            return "PLAYING";
        case AudioState::Paused:
            return "PAUSED";
        case AudioState::Draining:
            return "DRAINING";
        case AudioState::Stopped:
            return "STOPPED";
        case AudioState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* audioErrorName(AudioError error) {
    switch (error) {
        case AudioError::None:
            return "none";
        case AudioError::NotInitialized:
            return "not_initialized";
        case AudioError::InvalidArgument:
            return "invalid_argument";
        case AudioError::PathTooLong:
            return "path_too_long";
        case AudioError::FileOpenFailed:
            return "file_open_failed";
        case AudioError::ProbeFailed:
            return "mp3_probe_failed";
        case AudioError::UnsupportedFormat:
            return "unsupported_mp3";
        case AudioError::SeekFailed:
            return "seek_failed";
        case AudioError::DecoderBeginFailed:
            return "decoder_begin_failed";
        case AudioError::DecoderFailed:
            return "decoder_failed";
        case AudioError::ReadFailed:
            return "sd_read_failed";
        case AudioError::SpeakerBeginFailed:
            return "speaker_begin_failed";
    }
    return "unknown_error";
}

}  // namespace player
}  // namespace adv_walkman
