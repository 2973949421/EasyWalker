#include "player/library/Mp3MetadataReader.h"

#include <algorithm>
#include <cstring>

namespace adv_walkman {
namespace player {
namespace {

uint32_t readBe32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

bool readSyncsafe32(const uint8_t* bytes, uint32_t& value) {
    if ((bytes[0] | bytes[1] | bytes[2] | bytes[3]) & 0x80U) {
        return false;
    }
    value = (static_cast<uint32_t>(bytes[0]) << 21) |
            (static_cast<uint32_t>(bytes[1]) << 14) |
            (static_cast<uint32_t>(bytes[2]) << 7) |
            static_cast<uint32_t>(bytes[3]);
    return true;
}

bool isFrameId(const uint8_t* bytes) {
    for (size_t index = 0; index < 4; ++index) {
        if (!((bytes[index] >= 'A' && bytes[index] <= 'Z') ||
              (bytes[index] >= '0' && bytes[index] <= '9'))) {
            return false;
        }
    }
    return true;
}

bool isFrame(const uint8_t* bytes, const char id[5]) {
    return std::memcmp(bytes, id, 4) == 0;
}

bool asciiEqualIgnoreCase(char left, char right) {
    if (left >= 'A' && left <= 'Z') {
        left = static_cast<char>(left + ('a' - 'A'));
    }
    if (right >= 'A' && right <= 'Z') {
        right = static_cast<char>(right + ('a' - 'A'));
    }
    return left == right;
}

template <size_t Capacity>
void appendCodepoint(MetadataText<Capacity>& output, uint32_t codepoint) {
    if (codepoint == 0) {
        return;
    }
    if (codepoint > 0x10FFFFU ||
        (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        codepoint = 0xFFFDU;
    }

    uint8_t encoded[4];
    size_t encodedLength = 0;
    if (codepoint <= 0x7FU) {
        encoded[encodedLength++] = static_cast<uint8_t>(codepoint);
    } else if (codepoint <= 0x7FFU) {
        encoded[encodedLength++] =
            static_cast<uint8_t>(0xC0U | (codepoint >> 6));
        encoded[encodedLength++] =
            static_cast<uint8_t>(0x80U | (codepoint & 0x3FU));
    } else if (codepoint <= 0xFFFFU) {
        encoded[encodedLength++] =
            static_cast<uint8_t>(0xE0U | (codepoint >> 12));
        encoded[encodedLength++] =
            static_cast<uint8_t>(0x80U | ((codepoint >> 6) & 0x3FU));
        encoded[encodedLength++] =
            static_cast<uint8_t>(0x80U | (codepoint & 0x3FU));
    } else {
        encoded[encodedLength++] =
            static_cast<uint8_t>(0xF0U | (codepoint >> 18));
        encoded[encodedLength++] =
            static_cast<uint8_t>(0x80U | ((codepoint >> 12) & 0x3FU));
        encoded[encodedLength++] =
            static_cast<uint8_t>(0x80U | ((codepoint >> 6) & 0x3FU));
        encoded[encodedLength++] =
            static_cast<uint8_t>(0x80U | (codepoint & 0x3FU));
    }

    const size_t currentLength = std::strlen(output.value);
    if (currentLength + encodedLength >= Capacity) {
        output.truncated = true;
        return;
    }
    std::memcpy(output.value + currentLength, encoded, encodedLength);
    output.value[currentLength + encodedLength] = '\0';
    output.present = true;
}

template <size_t Capacity>
void appendValidatedUtf8(MetadataText<Capacity>& output,
                         const uint8_t* bytes, size_t length) {
    uint8_t expected = 0;
    uint32_t codepoint = 0;
    uint32_t minimum = 0;

    size_t index = 0;
    while (index < length) {
        const uint8_t byte = bytes[index++];
        if (expected == 0) {
            if (byte <= 0x7F) {
                appendCodepoint(output, byte);
            } else if (byte >= 0xC2 && byte <= 0xDF) {
                expected = 1;
                codepoint = byte & 0x1FU;
                minimum = 0x80U;
            } else if (byte >= 0xE0 && byte <= 0xEF) {
                expected = 2;
                codepoint = byte & 0x0FU;
                minimum = 0x800U;
            } else if (byte >= 0xF0 && byte <= 0xF4) {
                expected = 3;
                codepoint = byte & 0x07U;
                minimum = 0x10000U;
            } else {
                appendCodepoint(output, 0xFFFDU);
            }
            continue;
        }

        if ((byte & 0xC0U) != 0x80U) {
            appendCodepoint(output, 0xFFFDU);
            expected = 0;
            --index;
            continue;
        }
        codepoint = (codepoint << 6) | (byte & 0x3FU);
        if (--expected == 0) {
            if (codepoint < minimum || codepoint > 0x10FFFFU ||
                (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                appendCodepoint(output, 0xFFFDU);
            } else {
                appendCodepoint(output, codepoint);
            }
        }
    }
    if (expected != 0) {
        appendCodepoint(output, 0xFFFDU);
    }
}

template <size_t DestinationCapacity, size_t SourceCapacity>
void copyMetadataText(const MetadataText<SourceCapacity>& source,
                      MetadataText<DestinationCapacity>& destination) {
    destination = MetadataText<DestinationCapacity>{};
    const size_t sourceLength = std::strlen(source.value);
    size_t copyLength = std::min(sourceLength, DestinationCapacity - 1);
    if (copyLength < sourceLength) {
        while (copyLength > 0 &&
               (static_cast<uint8_t>(source.value[copyLength]) & 0xC0U) ==
                   0x80U) {
            --copyLength;
        }
    }
    std::memcpy(destination.value, source.value, copyLength);
    destination.value[copyLength] = '\0';
    destination.present = copyLength != 0;
    destination.truncated = source.truncated || copyLength < sourceLength;
}

}  // namespace

bool Mp3MetadataReader::begin(fs::FS& filesystem, const char* path) {
    resetRequest();
    if (path == nullptr || path[0] == '\0') {
        finishError(Mp3MetadataError::InvalidArgument);
        return false;
    }
    const size_t length = std::strlen(path);
    if (length > kMaxTrackPathBytes) {
        finishError(Mp3MetadataError::InvalidArgument);
        return false;
    }

    fs_ = &filesystem;
    std::memcpy(path_, path, length + 1);
    makeFilenameFallback();
    phase_ = Phase::Open;
    updatePublicState();
    return true;
}

void Mp3MetadataReader::service() {
    if (!pending()) {
        return;
    }
    const uint32_t startedUs = micros();
    switch (phase_) {
        case Phase::Open:
            serviceOpen();
            break;
        case Phase::Header:
            serviceHeader();
            break;
        case Phase::ExtendedSize:
            serviceExtendedSize();
            break;
        case Phase::ExtendedSkip:
            serviceExtendedSkip();
            break;
        case Phase::FrameHeader:
            serviceFrameHeader();
            break;
        case Phase::FramePayload:
            serviceFramePayload();
            break;
        case Phase::FrameSkip:
            serviceFrameSkip();
            break;
        case Phase::Idle:
        case Phase::Ready:
        case Phase::Error:
            break;
    }
    const uint32_t elapsedUs = micros() - startedUs;
    status_.serviceMaxUs = std::max(status_.serviceMaxUs, elapsedUs);
    updatePublicState();
}

void Mp3MetadataReader::cancel() {
    resetRequest();
}

bool Mp3MetadataReader::pending() const {
    return phase_ != Phase::Idle && phase_ != Phase::Ready &&
           phase_ != Phase::Error;
}

bool Mp3MetadataReader::ready() const {
    return phase_ == Phase::Ready;
}

bool Mp3MetadataReader::failed() const {
    return phase_ == Phase::Error;
}

const Mp3Metadata& Mp3MetadataReader::metadata() const {
    return metadata_;
}

Mp3MetadataStatus Mp3MetadataReader::status() const {
    return status_;
}

void Mp3MetadataReader::resetRequest() {
    if (file_) {
        file_.close();
    }
    fs_ = nullptr;
    std::memset(path_, 0, sizeof(path_));
    fileSize_ = 0;
    phase_ = Phase::Idle;
    status_ = Mp3MetadataStatus{};
    metadata_ = Mp3Metadata{};
    tagMajorVersion_ = 0;
    tagUnsynchronised_ = false;
    tagBodyUnsynchronised_ = false;
    tagUnsyncPreviousWasFf_ = false;
    tagRemainingRaw_ = 0;
    extendedRemaining_ = 0;
    frameTarget_ = TextTarget::None;
    frameRemaining_ = 0;
    frameUnsynchronised_ = false;
    framePrefixRemaining_ = 0;
    frameUnsyncPreviousWasFf_ = false;
    std::memset(structureBuffer_, 0, sizeof(structureBuffer_));
    structureProgress_ = 0;
    ioBufferOffset_ = 0;
    ioBufferLength_ = 0;
    startTextDecoder();
    phase_ = Phase::Idle;
    updatePublicState();
}

void Mp3MetadataReader::serviceOpen() {
    file_ = fs_->open(path_, FILE_READ);
    if (!file_) {
        finishError(Mp3MetadataError::OpenFailed);
        return;
    }
    fileSize_ = static_cast<uint32_t>(file_.size());
    if (fileSize_ < 10) {
        finishReady();
        return;
    }
    phase_ = Phase::Header;
}

void Mp3MetadataReader::serviceHeader() {
    uint8_t header[10];
    if (!readExact(header, sizeof(header))) {
        finishError(Mp3MetadataError::IoError);
        return;
    }
    if (std::memcmp(header, "ID3", 3) != 0) {
        finishReady();
        return;
    }

    metadata_.hasId3v2 = true;
    metadata_.id3v2MajorVersion = header[3];
    tagMajorVersion_ = header[3];
    if (tagMajorVersion_ != 3 && tagMajorVersion_ != 4) {
        finishReady(Mp3MetadataError::UnsupportedTag);
        return;
    }

    uint32_t tagSize = 0;
    if (!readSyncsafe32(header + 6, tagSize)) {
        finishReady(Mp3MetadataError::MalformedTag);
        return;
    }
    if (tagSize > kMaximumTagBytes) {
        finishReady(Mp3MetadataError::TagTooLarge);
        return;
    }
    if (tagSize > fileSize_ - 10U) {
        finishReady(Mp3MetadataError::MalformedTag);
        return;
    }

    tagRemainingRaw_ = tagSize;
    tagUnsynchronised_ = (header[5] & 0x80U) != 0;
    // ID3v2.3 tag-level unsynchronisation is applied to the complete tag body,
    // including extended headers and frame headers. All body phases therefore
    // consume one shared, stateful de-unsynchronised stream.
    tagBodyUnsynchronised_ = tagMajorVersion_ == 3 && tagUnsynchronised_;
    tagUnsyncPreviousWasFf_ = false;
    ioBufferOffset_ = 0;
    ioBufferLength_ = 0;
    structureProgress_ = 0;
    if (tagRemainingRaw_ == 0) {
        finishReady();
    } else if ((header[5] & 0x40U) != 0) {
        phase_ = Phase::ExtendedSize;
    } else {
        phase_ = Phase::FrameHeader;
    }
}

void Mp3MetadataReader::serviceExtendedSize() {
    const CollectResult result = collectTagBytes(
        structureBuffer_, 4, structureProgress_);
    if (result == CollectResult::Pending) {
        return;
    }
    if (result == CollectResult::End) {
        finishReady(Mp3MetadataError::MalformedTag);
        return;
    }
    if (result == CollectResult::Error) {
        return;
    }
    structureProgress_ = 0;

    uint32_t size = 0;
    if (tagMajorVersion_ == 4) {
        if (!readSyncsafe32(structureBuffer_, size) || size < 6) {
            finishReady(Mp3MetadataError::MalformedTag);
            return;
        }
        extendedRemaining_ = size - 4;
    } else {
        size = readBe32(structureBuffer_);
        if (size < 6) {
            finishReady(Mp3MetadataError::MalformedTag);
            return;
        }
        extendedRemaining_ = size;
    }
    // De-unsynchronisation can only reduce the logical byte count. A logical
    // v2.3 extended header may therefore be smaller than the raw remainder,
    // but it can never be larger.
    if (extendedRemaining_ > tagRemainingRaw_) {
        finishReady(Mp3MetadataError::MalformedTag);
        return;
    }
    phase_ = extendedRemaining_ == 0 ? Phase::FrameHeader
                                    : Phase::ExtendedSkip;
}

void Mp3MetadataReader::serviceExtendedSkip() {
    if (extendedRemaining_ == 0) {
        phase_ = Phase::FrameHeader;
        return;
    }
    bool reachedEnd = false;
    const size_t consumed = consumeTagBytes(
        std::min<uint32_t>(extendedRemaining_, kIoChunkBytes), false,
        reachedEnd);
    extendedRemaining_ -= static_cast<uint32_t>(consumed);
    if (extendedRemaining_ == 0) {
        phase_ = Phase::FrameHeader;
    } else if (reachedEnd) {
        finishReady(Mp3MetadataError::MalformedTag);
    }
}

void Mp3MetadataReader::serviceFrameHeader() {
    if (tagRemainingRaw_ == 0 && structureProgress_ == 0) {
        finishReady();
        return;
    }
    const CollectResult result = collectTagBytes(
        structureBuffer_, sizeof(structureBuffer_), structureProgress_);
    if (result == CollectResult::Pending) {
        return;
    }
    if (result == CollectResult::Error) {
        return;
    }
    if (result == CollectResult::End) {
        bool paddingOnly = true;
        for (size_t index = 0; index < structureProgress_; ++index) {
            paddingOnly = paddingOnly && structureBuffer_[index] == 0;
        }
        structureProgress_ = 0;
        finishReady(paddingOnly ? Mp3MetadataError::None
                                : Mp3MetadataError::MalformedTag);
        return;
    }
    structureProgress_ = 0;

    bool allZero = true;
    for (size_t index = 0; index < sizeof(structureBuffer_); ++index) {
        allZero = allZero && structureBuffer_[index] == 0;
    }
    if (allZero || std::memcmp(structureBuffer_, "3DI", 3) == 0) {
        finishReady();
        return;
    }
    if (!isFrameId(structureBuffer_)) {
        finishReady(Mp3MetadataError::MalformedTag);
        return;
    }
    startFrame(structureBuffer_);
}

void Mp3MetadataReader::startFrame(const uint8_t header[10]) {
    uint32_t frameSize = 0;
    if (tagMajorVersion_ == 4) {
        if (!readSyncsafe32(header + 4, frameSize)) {
            finishReady(Mp3MetadataError::MalformedTag);
            return;
        }
    } else {
        frameSize = readBe32(header + 4);
    }
    if (frameSize > tagRemainingRaw_) {
        finishReady(Mp3MetadataError::MalformedTag);
        return;
    }
    if (status_.framesVisited >= kMaximumFrames) {
        finishReady(Mp3MetadataError::TooManyFrames);
        return;
    }
    ++status_.framesVisited;

    frameTarget_ = TextTarget::None;
    if (isFrame(header, "TIT2")) {
        frameTarget_ = TextTarget::Title;
    } else if (isFrame(header, "TPE1")) {
        frameTarget_ = TextTarget::Artist;
    } else if (isFrame(header, "TALB")) {
        frameTarget_ = TextTarget::Album;
    } else if (isFrame(header, "TRCK")) {
        frameTarget_ = TextTarget::TrackNumber;
    }

    bool compressed = false;
    bool encrypted = false;
    framePrefixRemaining_ = 0;
    uint8_t requiredPrefixBytes = 0;
    if (tagMajorVersion_ == 3) {
        compressed = (header[9] & 0x80U) != 0;
        encrypted = (header[9] & 0x40U) != 0;
        if (compressed) {
            requiredPrefixBytes =
                static_cast<uint8_t>(requiredPrefixBytes + 4);
        }
        if (encrypted) {
            ++requiredPrefixBytes;
        }
        if ((header[9] & 0x20U) != 0) {
            framePrefixRemaining_ = 1;
            ++requiredPrefixBytes;
        }
    } else {
        compressed = (header[9] & 0x08U) != 0;
        encrypted = (header[9] & 0x04U) != 0;
        if ((header[9] & 0x40U) != 0) {
            ++framePrefixRemaining_;
            ++requiredPrefixBytes;
        }
        if (encrypted) {
            ++requiredPrefixBytes;
        }
        if ((header[9] & 0x01U) != 0) {
            framePrefixRemaining_ =
                static_cast<uint8_t>(framePrefixRemaining_ + 4);
            requiredPrefixBytes =
                static_cast<uint8_t>(requiredPrefixBytes + 4);
        } else if (compressed) {
            // ID3v2.4 compression requires a data-length indicator.
            recordWarning(Mp3MetadataError::MalformedTag);
        }
    }

    frameRemaining_ = frameSize;
    // v2.3 tag unsync was already removed by readTagByte(). ID3v2.4 keeps
    // frame sizes in their stored form, so per-frame/tag unsync is decoded
    // only while consuming the payload.
    frameUnsynchronised_ =
        tagMajorVersion_ == 4 &&
        (tagUnsynchronised_ || (header[9] & 0x02U) != 0);
    frameUnsyncPreviousWasFf_ = false;

    const bool skip = compressed || encrypted || frameTarget_ == TextTarget::None;
    if (requiredPrefixBytes > frameSize ||
        (!skip && framePrefixRemaining_ >= frameSize)) {
        recordWarning(Mp3MetadataError::MalformedTag);
        phase_ = frameRemaining_ == 0 ? Phase::FrameHeader
                                     : Phase::FrameSkip;
        return;
    }
    if (frameRemaining_ == 0) {
        phase_ = Phase::FrameHeader;
    } else if (skip) {
        phase_ = Phase::FrameSkip;
    } else {
        startTextDecoder();
        phase_ = Phase::FramePayload;
    }
}

void Mp3MetadataReader::serviceFramePayload() {
    bool reachedEnd = false;
    const size_t consumed = consumeTagBytes(
        std::min<uint32_t>(frameRemaining_, kIoChunkBytes), true,
        reachedEnd);
    frameRemaining_ -= static_cast<uint32_t>(consumed);
    if (phase_ == Phase::Error || phase_ == Phase::Ready) {
        return;
    }
    if (frameRemaining_ == 0) {
        finishTextDecoder();
        commitFrameText();
        phase_ = Phase::FrameHeader;
    } else if (reachedEnd) {
        finishReady(Mp3MetadataError::MalformedTag);
    }
}

void Mp3MetadataReader::serviceFrameSkip() {
    bool reachedEnd = false;
    const size_t consumed = consumeTagBytes(
        std::min<uint32_t>(frameRemaining_, kIoChunkBytes), false,
        reachedEnd);
    frameRemaining_ -= static_cast<uint32_t>(consumed);
    if (phase_ == Phase::Error || phase_ == Phase::Ready) {
        return;
    }
    if (frameRemaining_ == 0) {
        phase_ = Phase::FrameHeader;
    } else if (reachedEnd) {
        finishReady(Mp3MetadataError::MalformedTag);
    }
}

bool Mp3MetadataReader::readExact(void* destination, size_t length) {
    if (length > kIoChunkBytes) {
        return false;
    }
    const size_t received =
        file_.read(static_cast<uint8_t*>(destination), length);
    status_.bytesRead += static_cast<uint32_t>(received);
    return received == length;
}

Mp3MetadataReader::TagByteResult Mp3MetadataReader::readTagByte(
    uint8_t& byte, bool& readPerformed) {
    while (true) {
        if (ioBufferOffset_ >= ioBufferLength_) {
            if (tagRemainingRaw_ == 0) {
                return TagByteResult::End;
            }
            if (readPerformed) {
                return TagByteResult::Wait;
            }
            const size_t requested = static_cast<size_t>(std::min<uint32_t>(
                kIoChunkBytes, tagRemainingRaw_));
            const size_t received = file_.read(ioBuffer_, requested);
            status_.bytesRead += static_cast<uint32_t>(received);
            readPerformed = true;
            if (received != requested) {
                finishError(Mp3MetadataError::IoError);
                return TagByteResult::Error;
            }
            ioBufferOffset_ = 0;
            ioBufferLength_ = received;
        }

        const uint8_t raw = ioBuffer_[ioBufferOffset_++];
        --tagRemainingRaw_;
        if (tagBodyUnsynchronised_ && tagUnsyncPreviousWasFf_ && raw == 0) {
            tagUnsyncPreviousWasFf_ = false;
            continue;
        }
        tagUnsyncPreviousWasFf_ =
            tagBodyUnsynchronised_ && raw == 0xFFU;
        byte = raw;
        return TagByteResult::Byte;
    }
}

Mp3MetadataReader::CollectResult Mp3MetadataReader::collectTagBytes(
    uint8_t* destination, size_t length, size_t& progress) {
    bool readPerformed = false;
    while (progress < length) {
        uint8_t byte = 0;
        switch (readTagByte(byte, readPerformed)) {
            case TagByteResult::Byte:
                destination[progress++] = byte;
                break;
            case TagByteResult::Wait:
                return CollectResult::Pending;
            case TagByteResult::End:
                return CollectResult::End;
            case TagByteResult::Error:
                return CollectResult::Error;
        }
    }
    return CollectResult::Complete;
}

size_t Mp3MetadataReader::consumeTagBytes(uint32_t maximumBytes,
                                          bool decodeText,
                                          bool& reachedEnd) {
    reachedEnd = false;
    bool readPerformed = false;
    size_t consumed = 0;
    while (consumed < maximumBytes) {
        uint8_t byte = 0;
        const TagByteResult result = readTagByte(byte, readPerformed);
        if (result == TagByteResult::Byte) {
            ++consumed;
            processFrameByte(byte, decodeText);
        } else if (result == TagByteResult::End) {
            reachedEnd = true;
            break;
        } else if (result == TagByteResult::Wait ||
                   result == TagByteResult::Error) {
            break;
        }
    }
    return consumed;
}

void Mp3MetadataReader::startTextDecoder() {
    frameText_ = MetadataText<kMetadataTitleCapacity>{};
    textEncodingKnown_ = false;
    textEncoding_ = 0;
    textTerminated_ = false;
    utf8Expected_ = 0;
    utf8Codepoint_ = 0;
    utf8Minimum_ = 0;
    utf16LittleEndian_ = false;
    utf16BomPending_ = false;
    utf16HaveByte_ = false;
    utf16FirstByte_ = 0;
    utf16HighSurrogate_ = 0;
}

void Mp3MetadataReader::processFrameByte(uint8_t byte, bool decodeText) {
    if (frameUnsynchronised_ && frameUnsyncPreviousWasFf_ && byte == 0) {
        frameUnsyncPreviousWasFf_ = false;
        return;
    }
    frameUnsyncPreviousWasFf_ = frameUnsynchronised_ && byte == 0xFFU;
    if (!decodeText) {
        return;
    }
    if (framePrefixRemaining_ != 0) {
        --framePrefixRemaining_;
        return;
    }
    processTextByte(byte);
}

void Mp3MetadataReader::processTextByte(uint8_t byte) {
    if (textTerminated_) {
        return;
    }
    if (!textEncodingKnown_) {
        textEncodingKnown_ = true;
        textEncoding_ = byte;
        if (textEncoding_ > 3) {
            recordWarning(Mp3MetadataError::MalformedTag);
            textTerminated_ = true;
        } else if (textEncoding_ == 1) {
            utf16BomPending_ = true;
        } else if (textEncoding_ == 2) {
            utf16LittleEndian_ = false;
        }
        return;
    }

    if (textEncoding_ == 0) {
        if (byte == 0) {
            textTerminated_ = true;
        } else {
            appendCodepoint(frameText_, byte);
        }
    } else if (textEncoding_ == 1 || textEncoding_ == 2) {
        processUtf16Byte(byte);
    } else {
        processUtf8Byte(byte);
    }
}

void Mp3MetadataReader::processUtf8Byte(uint8_t byte) {
    bool retry = true;
    while (retry && !textTerminated_) {
        retry = false;
        if (utf8Expected_ == 0) {
            if (byte == 0) {
                textTerminated_ = true;
            } else if (byte <= 0x7F) {
                appendCodepoint(frameText_, byte);
            } else if (byte >= 0xC2 && byte <= 0xDF) {
                utf8Expected_ = 1;
                utf8Codepoint_ = byte & 0x1FU;
                utf8Minimum_ = 0x80U;
            } else if (byte >= 0xE0 && byte <= 0xEF) {
                utf8Expected_ = 2;
                utf8Codepoint_ = byte & 0x0FU;
                utf8Minimum_ = 0x800U;
            } else if (byte >= 0xF0 && byte <= 0xF4) {
                utf8Expected_ = 3;
                utf8Codepoint_ = byte & 0x07U;
                utf8Minimum_ = 0x10000U;
            } else {
                appendCodepoint(frameText_, 0xFFFDU);
                recordWarning(Mp3MetadataError::MalformedTag);
            }
        } else if ((byte & 0xC0U) != 0x80U) {
            appendCodepoint(frameText_, 0xFFFDU);
            recordWarning(Mp3MetadataError::MalformedTag);
            utf8Expected_ = 0;
            retry = true;
        } else {
            utf8Codepoint_ = (utf8Codepoint_ << 6) | (byte & 0x3FU);
            if (--utf8Expected_ == 0) {
                if (utf8Codepoint_ < utf8Minimum_ ||
                    utf8Codepoint_ > 0x10FFFFU ||
                    (utf8Codepoint_ >= 0xD800U &&
                     utf8Codepoint_ <= 0xDFFFU)) {
                    appendCodepoint(frameText_, 0xFFFDU);
                    recordWarning(Mp3MetadataError::MalformedTag);
                } else {
                    appendCodepoint(frameText_, utf8Codepoint_);
                }
            }
        }
    }
}

void Mp3MetadataReader::processUtf16Byte(uint8_t byte) {
    if (!utf16HaveByte_) {
        utf16FirstByte_ = byte;
        utf16HaveByte_ = true;
        return;
    }

    const uint8_t first = utf16FirstByte_;
    utf16HaveByte_ = false;
    if (utf16BomPending_) {
        utf16BomPending_ = false;
        if (first == 0xFFU && byte == 0xFEU) {
            utf16LittleEndian_ = true;
            return;
        }
        if (first == 0xFEU && byte == 0xFFU) {
            utf16LittleEndian_ = false;
            return;
        }
        // Encoding 1 requires a BOM. Treat a missing BOM as big endian while
        // retaining usable text and exposing a parse warning.
        utf16LittleEndian_ = false;
        recordWarning(Mp3MetadataError::MalformedTag);
    }

    const uint16_t unit = utf16LittleEndian_
                              ? static_cast<uint16_t>(first | (byte << 8))
                              : static_cast<uint16_t>((first << 8) | byte);
    processUtf16Unit(unit);
}

void Mp3MetadataReader::processUtf16Unit(uint16_t unit) {
    if (unit == 0) {
        if (utf16HighSurrogate_ != 0) {
            appendCodepoint(frameText_, 0xFFFDU);
            recordWarning(Mp3MetadataError::MalformedTag);
            utf16HighSurrogate_ = 0;
        }
        textTerminated_ = true;
        return;
    }

    if (utf16HighSurrogate_ != 0) {
        if (unit >= 0xDC00U && unit <= 0xDFFFU) {
            const uint32_t codepoint =
                0x10000U +
                ((static_cast<uint32_t>(utf16HighSurrogate_) - 0xD800U)
                 << 10) +
                (unit - 0xDC00U);
            appendCodepoint(frameText_, codepoint);
            utf16HighSurrogate_ = 0;
            return;
        }
        appendCodepoint(frameText_, 0xFFFDU);
        recordWarning(Mp3MetadataError::MalformedTag);
        utf16HighSurrogate_ = 0;
    }

    if (unit >= 0xD800U && unit <= 0xDBFFU) {
        utf16HighSurrogate_ = unit;
    } else if (unit >= 0xDC00U && unit <= 0xDFFFU) {
        appendCodepoint(frameText_, 0xFFFDU);
        recordWarning(Mp3MetadataError::MalformedTag);
    } else {
        appendCodepoint(frameText_, unit);
    }
}

void Mp3MetadataReader::finishTextDecoder() {
    if (textEncodingKnown_ && textEncoding_ == 3 && utf8Expected_ != 0) {
        appendCodepoint(frameText_, 0xFFFDU);
        recordWarning(Mp3MetadataError::MalformedTag);
        utf8Expected_ = 0;
    }
    if (textEncodingKnown_ && (textEncoding_ == 1 || textEncoding_ == 2)) {
        if (textEncoding_ == 1 && utf16BomPending_) {
            recordWarning(Mp3MetadataError::MalformedTag);
        }
        if (utf16HaveByte_ || utf16HighSurrogate_ != 0) {
            appendCodepoint(frameText_, 0xFFFDU);
            recordWarning(Mp3MetadataError::MalformedTag);
        }
        utf16BomPending_ = false;
        utf16HaveByte_ = false;
        utf16HighSurrogate_ = 0;
    }
}

void Mp3MetadataReader::commitFrameText() {
    if (!frameText_.present) {
        return;
    }
    switch (frameTarget_) {
        case TextTarget::Title:
            if (metadata_.titleFromFilename || !metadata_.title.present) {
                copyMetadataText(frameText_, metadata_.title);
                metadata_.titleFromFilename = false;
            }
            break;
        case TextTarget::Artist:
            if (!metadata_.artist.present) {
                copyMetadataText(frameText_, metadata_.artist);
            }
            break;
        case TextTarget::Album:
            if (!metadata_.album.present) {
                copyMetadataText(frameText_, metadata_.album);
            }
            break;
        case TextTarget::TrackNumber:
            if (!metadata_.trackNumber.present) {
                copyMetadataText(frameText_, metadata_.trackNumber);
            }
            break;
        case TextTarget::None:
            break;
    }
}

void Mp3MetadataReader::makeFilenameFallback() {
    const char* basename = path_;
    for (const char* cursor = path_; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            basename = cursor + 1;
        }
    }
    size_t length = std::strlen(basename);
    if (length >= 4 && basename[length - 4] == '.' &&
        asciiEqualIgnoreCase(basename[length - 3], 'm') &&
        asciiEqualIgnoreCase(basename[length - 2], 'p') &&
        asciiEqualIgnoreCase(basename[length - 1], '3')) {
        length -= 4;
    }
    appendValidatedUtf8(metadata_.title,
                        reinterpret_cast<const uint8_t*>(basename), length);
    metadata_.titleFromFilename = metadata_.title.present;
}

void Mp3MetadataReader::recordWarning(Mp3MetadataError warning) {
    if (warning != Mp3MetadataError::None &&
        status_.error == Mp3MetadataError::None) {
        status_.error = warning;
    }
}

void Mp3MetadataReader::finishReady(Mp3MetadataError warning) {
    recordWarning(warning);
    if (file_) {
        file_.close();
    }
    phase_ = Phase::Ready;
    updatePublicState();
}

void Mp3MetadataReader::finishError(Mp3MetadataError error) {
    if (file_) {
        file_.close();
    }
    status_.error = error;
    phase_ = Phase::Error;
    updatePublicState();
}

void Mp3MetadataReader::updatePublicState() {
    switch (phase_) {
        case Phase::Idle:
            status_.state = Mp3MetadataState::Idle;
            break;
        case Phase::Open:
            status_.state = Mp3MetadataState::Opening;
            break;
        case Phase::Header:
            status_.state = Mp3MetadataState::ReadingHeader;
            break;
        case Phase::ExtendedSize:
        case Phase::ExtendedSkip:
            status_.state = Mp3MetadataState::ReadingExtendedHeader;
            break;
        case Phase::FrameHeader:
            status_.state = Mp3MetadataState::ReadingFrames;
            break;
        case Phase::FramePayload:
            status_.state = Mp3MetadataState::ReadingText;
            break;
        case Phase::FrameSkip:
            status_.state = Mp3MetadataState::SkippingFrame;
            break;
        case Phase::Ready:
            status_.state = Mp3MetadataState::Ready;
            break;
        case Phase::Error:
            status_.state = Mp3MetadataState::Error;
            break;
    }
}

const char* mp3MetadataStateName(Mp3MetadataState state) {
    switch (state) {
        case Mp3MetadataState::Idle:
            return "IDLE";
        case Mp3MetadataState::Opening:
            return "OPENING";
        case Mp3MetadataState::ReadingHeader:
            return "READING_HEADER";
        case Mp3MetadataState::ReadingExtendedHeader:
            return "READING_EXTENDED_HEADER";
        case Mp3MetadataState::ReadingFrames:
            return "READING_FRAMES";
        case Mp3MetadataState::ReadingText:
            return "READING_TEXT";
        case Mp3MetadataState::SkippingFrame:
            return "SKIPPING_FRAME";
        case Mp3MetadataState::Ready:
            return "READY";
        case Mp3MetadataState::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

const char* mp3MetadataErrorName(Mp3MetadataError error) {
    switch (error) {
        case Mp3MetadataError::None:
            return "NONE";
        case Mp3MetadataError::InvalidArgument:
            return "INVALID_ARGUMENT";
        case Mp3MetadataError::OpenFailed:
            return "OPEN_FAILED";
        case Mp3MetadataError::IoError:
            return "IO_ERROR";
        case Mp3MetadataError::UnsupportedTag:
            return "UNSUPPORTED_TAG";
        case Mp3MetadataError::TagTooLarge:
            return "TAG_TOO_LARGE";
        case Mp3MetadataError::TooManyFrames:
            return "TOO_MANY_FRAMES";
        case Mp3MetadataError::MalformedTag:
            return "MALFORMED_TAG";
    }
    return "UNKNOWN";
}

}  // namespace player
}  // namespace adv_walkman
