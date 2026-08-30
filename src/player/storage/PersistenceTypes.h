#pragma once

#include <Arduino.h>

#include <stddef.h>
#include <stdint.h>

#include "../core/CoreTypes.h"

namespace adv_walkman {
namespace player {

constexpr size_t kPersistedQueueMaxTracks = kMaxQueueTracks;
constexpr size_t kPersistedPathMaxBytes = kMaxTrackPathBytes;
constexpr size_t kPersistedQueueMaxPayloadBytes = 256 * 1024;
constexpr size_t kPersistedHistoryMaxTracks = kPreviousHistoryCapacity;
constexpr uint16_t kPersistedInvalidTrackIndex = 0xFFFF;

enum class PersistenceResult : uint8_t {
    Ok,
    Pending,
    NotFound,
    Busy,
    InvalidArgument,
    UnsupportedVersion,
    Corrupt,
    IoError,
};

enum class PersistenceRecordKind : uint8_t {
    None,
    Queue,
    Session,
};

// Storage intentionally uses a byte for RepeatMode so the persistence layer
// does not depend on PlayerController. Values are Off=0, All=1, One=2.
struct PersistedSession {
    uint32_t queueGeneration = 0;
    uint16_t currentIndex = kPersistedInvalidTrackIndex;
    uint32_t positionMs = 0;
    uint32_t sourceOffset = 0;
    uint8_t repeatMode = 0;
    bool shuffleEnabled = false;
    // Session v1 reserved byte 21: 0=Lyrics, 1=Cover. Not audio state.
    uint8_t preferredNowPlayingView = 0;
    // Session v1 reserved byte 22: SoundPreset 0..3. Byte 23 remains reserved.
    // Decode preserves an invalid raw value so Runtime can record the fallback
    // without rejecting the otherwise recoverable Session.
    uint8_t soundPreset = static_cast<uint8_t>(SoundPreset::Original);

    uint16_t orderCount = 0;
    uint16_t orderCursor = 0;
    uint16_t order[kPersistedQueueMaxTracks] = {};

    uint8_t historyCount = 0;
    uint16_t history[kPersistedHistoryMaxTracks] = {};
};

const char* persistenceResultName(PersistenceResult result);

}  // namespace player
}  // namespace adv_walkman
