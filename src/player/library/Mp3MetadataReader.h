#pragma once

#include <Arduino.h>
#include <FS.h>

#include <cstddef>
#include <cstdint>

#include "../core/CoreTypes.h"

namespace adv_walkman {
namespace player {

constexpr size_t kMetadataTitleCapacity = 192;
constexpr size_t kMetadataArtistCapacity = 128;
constexpr size_t kMetadataAlbumCapacity = 128;
constexpr size_t kMetadataTrackCapacity = 32;

template <size_t Capacity>
struct MetadataText {
    static_assert(Capacity >= 2, "Metadata text needs room for a terminator");

    char value[Capacity] = {};
    bool present = false;
    bool truncated = false;
};

struct Mp3Metadata {
    MetadataText<kMetadataTitleCapacity> title;
    MetadataText<kMetadataArtistCapacity> artist;
    MetadataText<kMetadataAlbumCapacity> album;
    MetadataText<kMetadataTrackCapacity> trackNumber;

    bool titleFromFilename = false;
    bool hasId3v2 = false;
    uint8_t id3v2MajorVersion = 0;
};

enum class Mp3MetadataState : uint8_t {
    Idle,
    Opening,
    ReadingHeader,
    ReadingExtendedHeader,
    ReadingFrames,
    ReadingText,
    SkippingFrame,
    Ready,
    Error,
};

// A parse warning may accompany Ready metadata. OpenFailed and IoError are
// terminal failures; unsupported or malformed tags keep the filename fallback.
enum class Mp3MetadataError : uint8_t {
    None,
    InvalidArgument,
    OpenFailed,
    IoError,
    UnsupportedTag,
    TagTooLarge,
    TooManyFrames,
    MalformedTag,
};

struct Mp3MetadataStatus {
    Mp3MetadataState state = Mp3MetadataState::Idle;
    Mp3MetadataError error = Mp3MetadataError::None;
    uint32_t bytesRead = 0;
    uint32_t serviceMaxUs = 0;
    uint16_t framesVisited = 0;
};

class Mp3MetadataReader final {
  public:
    static constexpr size_t kIoChunkBytes = 512;
    static constexpr uint32_t kMaximumTagBytes = 8U * 1024U * 1024U;
    static constexpr uint16_t kMaximumFrames = 256;

    // begin() only captures the request. File open and all reads happen from
    // service(), with at most one <=512-byte read operation per call.
    bool begin(fs::FS& filesystem, const char* path);
    void service();
    void cancel();

    bool pending() const;
    bool ready() const;
    bool failed() const;
    const Mp3Metadata& metadata() const;
    Mp3MetadataStatus status() const;

  private:
    enum class Phase : uint8_t {
        Idle,
        Open,
        Header,
        ExtendedSize,
        ExtendedSkip,
        FrameHeader,
        FramePayload,
        FrameSkip,
        Ready,
        Error,
    };

    enum class TextTarget : uint8_t {
        None,
        Title,
        Artist,
        Album,
        TrackNumber,
    };

    enum class TagByteResult : uint8_t {
        Byte,
        Wait,
        End,
        Error,
    };

    enum class CollectResult : uint8_t {
        Complete,
        Pending,
        End,
        Error,
    };

    void resetRequest();
    void serviceOpen();
    void serviceHeader();
    void serviceExtendedSize();
    void serviceExtendedSkip();
    void serviceFrameHeader();
    void serviceFramePayload();
    void serviceFrameSkip();

    bool readExact(void* destination, size_t length);
    TagByteResult readTagByte(uint8_t& byte, bool& readPerformed);
    CollectResult collectTagBytes(uint8_t* destination, size_t length,
                                  size_t& progress);
    size_t consumeTagBytes(uint32_t maximumBytes, bool decodeText,
                           bool& reachedEnd);
    void startFrame(const uint8_t header[10]);
    void startTextDecoder();
    void processFrameByte(uint8_t byte, bool decodeText);
    void processTextByte(uint8_t byte);
    void processUtf8Byte(uint8_t byte);
    void processUtf16Byte(uint8_t byte);
    void processUtf16Unit(uint16_t unit);
    void finishTextDecoder();
    void commitFrameText();

    void makeFilenameFallback();
    void recordWarning(Mp3MetadataError warning);
    void finishReady(Mp3MetadataError warning = Mp3MetadataError::None);
    void finishError(Mp3MetadataError error);
    void updatePublicState();

    fs::FS* fs_ = nullptr;
    fs::File file_;
    char path_[kTrackPathCapacity] = {};
    uint32_t fileSize_ = 0;

    Phase phase_ = Phase::Idle;
    Mp3MetadataStatus status_;
    Mp3Metadata metadata_;

    uint8_t tagMajorVersion_ = 0;
    bool tagUnsynchronised_ = false;
    bool tagBodyUnsynchronised_ = false;
    bool tagUnsyncPreviousWasFf_ = false;
    uint32_t tagRemainingRaw_ = 0;
    uint32_t extendedRemaining_ = 0;

    TextTarget frameTarget_ = TextTarget::None;
    uint32_t frameRemaining_ = 0;
    bool frameUnsynchronised_ = false;
    uint8_t framePrefixRemaining_ = 0;
    bool frameUnsyncPreviousWasFf_ = false;

    uint8_t structureBuffer_[10] = {};
    size_t structureProgress_ = 0;

    MetadataText<kMetadataTitleCapacity> frameText_;
    bool textEncodingKnown_ = false;
    uint8_t textEncoding_ = 0;
    bool textTerminated_ = false;

    uint8_t utf8Expected_ = 0;
    uint32_t utf8Codepoint_ = 0;
    uint32_t utf8Minimum_ = 0;

    bool utf16LittleEndian_ = false;
    bool utf16BomPending_ = false;
    bool utf16HaveByte_ = false;
    uint8_t utf16FirstByte_ = 0;
    uint16_t utf16HighSurrogate_ = 0;

    uint8_t ioBuffer_[kIoChunkBytes] = {};
    size_t ioBufferOffset_ = 0;
    size_t ioBufferLength_ = 0;
};

const char* mp3MetadataStateName(Mp3MetadataState state);
const char* mp3MetadataErrorName(Mp3MetadataError error);

}  // namespace player
}  // namespace adv_walkman
