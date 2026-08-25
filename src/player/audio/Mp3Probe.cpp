#include "player/audio/Mp3Probe.h"

#include <algorithm>
#include <cstring>

namespace adv_walkman {
namespace player {
namespace {

constexpr uint32_t kInitialFrameScanLimit = 256 * 1024;
constexpr uint32_t kSeekResyncLimit = 64 * 1024;
// The decoder reads only a small amount ahead of submitted PCM. A checkpoint
// hint that differs by seconds is therefore stale (or belongs to a replaced
// file) and must fall back to the MP3 seek tables/proportional resync path.
constexpr uint32_t kSourceHintMaximumTimeDeltaMs = 2500;
constexpr size_t kScanBlockSize = 4096;
constexpr uint16_t kMaxVbriEntries = 4096;

struct FrameHeader {
    uint8_t versionBits = 0;
    uint16_t bitrateKbps = 0;
    uint32_t sampleRateHz = 0;
    uint16_t samplesPerFrame = 0;
    uint16_t frameLength = 0;
    uint8_t channels = 0;
    bool hasCrc = false;
};

uint16_t readBe16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                                 data[1]);
}

uint32_t readBe32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint32_t readLe32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

bool readExact(File& file, uint32_t offset, void* destination, size_t length) {
    if (!file.seek(offset, SeekSet)) {
        return false;
    }
    return file.read(static_cast<uint8_t*>(destination), length) == length;
}

bool parseFrameHeader(const uint8_t bytes[4], FrameHeader& header) {
    const uint32_t raw = readBe32(bytes);
    if ((raw & 0xFFE00000U) != 0xFFE00000U) {
        return false;
    }

    const uint8_t version = static_cast<uint8_t>((raw >> 19) & 0x03U);
    const uint8_t layer = static_cast<uint8_t>((raw >> 17) & 0x03U);
    const uint8_t bitrateIndex = static_cast<uint8_t>((raw >> 12) & 0x0FU);
    const uint8_t sampleRateIndex =
        static_cast<uint8_t>((raw >> 10) & 0x03U);
    if (version == 0x01 || layer != 0x01 || bitrateIndex == 0 ||
        bitrateIndex == 0x0F || sampleRateIndex == 0x03) {
        return false;
    }

    static constexpr uint16_t kMpeg1Layer3Bitrates[14] = {
        32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320,
    };
    static constexpr uint16_t kMpeg2Layer3Bitrates[14] = {
        8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160,
    };
    static constexpr uint32_t kMpeg1SampleRates[3] = {44100, 48000, 32000};

    uint32_t sampleRate = kMpeg1SampleRates[sampleRateIndex];
    if (version == 0x02) {
        sampleRate /= 2;
    } else if (version == 0x00) {
        sampleRate /= 4;
    }
    const bool mpeg1 = version == 0x03;
    const uint16_t bitrate =
        mpeg1 ? kMpeg1Layer3Bitrates[bitrateIndex - 1]
              : kMpeg2Layer3Bitrates[bitrateIndex - 1];
    const uint32_t coefficient = mpeg1 ? 144000U : 72000U;
    const uint32_t frameLength =
        (coefficient * bitrate) / sampleRate + ((raw >> 9) & 0x01U);
    if (frameLength < 24 || frameLength > UINT16_MAX) {
        return false;
    }

    header.versionBits = version;
    header.bitrateKbps = bitrate;
    header.sampleRateHz = sampleRate;
    header.samplesPerFrame = mpeg1 ? 1152 : 576;
    header.frameLength = static_cast<uint16_t>(frameLength);
    header.channels = ((raw >> 6) & 0x03U) == 0x03 ? 1 : 2;
    header.hasCrc = ((raw >> 16) & 0x01U) == 0;
    return true;
}

bool readFrameHeader(File& file, uint32_t offset, FrameHeader& header) {
    uint8_t bytes[4];
    return readExact(file, offset, bytes, sizeof(bytes)) &&
           parseFrameHeader(bytes, header);
}

bool compatibleFrames(const FrameHeader& first, const FrameHeader& next) {
    return first.versionBits == next.versionBits &&
           first.sampleRateHz == next.sampleRateHz &&
           first.samplesPerFrame == next.samplesPerFrame;
}

bool findFrame(File& file, uint32_t start, uint32_t end,
               uint32_t maximumBytes, bool requireSecond,
               uint32_t& frameOffset, FrameHeader& frameHeader) {
    if (end <= start + 4) {
        return false;
    }
    const uint32_t scanEnd =
        std::min(end, start + std::min(maximumBytes, end - start));
    // Single-threaded player probe; keep the scan window out of the 8 KiB
    // Arduino loop-task stack on this no-PSRAM target.
    static uint8_t buffer[kScanBlockSize + 3];
    uint32_t blockStart = start;

    while (blockStart + 4 <= scanEnd) {
        const size_t requested = static_cast<size_t>(
            std::min<uint32_t>(kScanBlockSize + 3, scanEnd - blockStart));
        if (!file.seek(blockStart, SeekSet)) {
            return false;
        }
        const size_t received = file.read(buffer, requested);
        if (received < 4) {
            return false;
        }

        for (size_t index = 0; index + 4 <= received; ++index) {
            FrameHeader candidate;
            if (!parseFrameHeader(&buffer[index], candidate)) {
                continue;
            }
            const uint32_t candidateOffset =
                blockStart + static_cast<uint32_t>(index);
            const uint32_t nextOffset = candidateOffset + candidate.frameLength;
            if (nextOffset > end) {
                continue;
            }

            if (requireSecond || nextOffset + 4 <= end) {
                FrameHeader next;
                if (nextOffset + 4 > end ||
                    !readFrameHeader(file, nextOffset, next) ||
                    !compatibleFrames(candidate, next)) {
                    continue;
                }
            }
            frameOffset = candidateOffset;
            frameHeader = candidate;
            return true;
        }

        if (received <= 3) {
            break;
        }
        blockStart += static_cast<uint32_t>(received - 3);
    }
    return false;
}

uint32_t id3v2End(File& file, uint32_t fileSize) {
    if (fileSize < 10) {
        return 0;
    }
    uint8_t header[10];
    if (!readExact(file, 0, header, sizeof(header)) ||
        std::memcmp(header, "ID3", 3) != 0) {
        return 0;
    }
    if ((header[6] | header[7] | header[8] | header[9]) & 0x80U) {
        return fileSize;
    }
    const uint32_t payload = (static_cast<uint32_t>(header[6]) << 21) |
                             (static_cast<uint32_t>(header[7]) << 14) |
                             (static_cast<uint32_t>(header[8]) << 7) |
                             static_cast<uint32_t>(header[9]);
    const uint32_t footer = (header[5] & 0x10U) ? 10U : 0U;
    const uint64_t end = 10ULL + payload + footer;
    return end <= fileSize ? static_cast<uint32_t>(end) : fileSize;
}

uint32_t audioEndBeforeTrailingTags(File& file, uint32_t start,
                                    uint32_t fileSize) {
    uint32_t end = fileSize;
    uint8_t tag[128];
    if (end >= start + sizeof(tag) &&
        readExact(file, end - sizeof(tag), tag, sizeof(tag)) &&
        std::memcmp(tag, "TAG", 3) == 0) {
        end -= sizeof(tag);
    }

    uint8_t apeFooter[32];
    if (end >= start + sizeof(apeFooter) &&
        readExact(file, end - sizeof(apeFooter), apeFooter,
                  sizeof(apeFooter)) &&
        std::memcmp(apeFooter, "APETAGEX", 8) == 0) {
        const uint32_t apeSize = readLe32(apeFooter + 12);
        if (apeSize >= sizeof(apeFooter) && apeSize <= end - start) {
            end -= apeSize;
        }
    }
    return end;
}

bool parseXing(File& file, uint32_t frameOffset,
               const FrameHeader& frame, Mp3Info& info,
               bool& tagSaysVbr) {
    const uint32_t sideInfo = frame.versionBits == 0x03
                                  ? (frame.channels == 1 ? 17U : 32U)
                                  : (frame.channels == 1 ? 9U : 17U);
    const uint32_t tagOffset = frameOffset + 4U +
                               (frame.hasCrc ? 2U : 0U) + sideInfo;
    if (tagOffset + 8 > frameOffset + frame.frameLength) {
        return false;
    }

    uint8_t fixed[8];
    if (!readExact(file, tagOffset, fixed, sizeof(fixed))) {
        return false;
    }
    const bool isXing = std::memcmp(fixed, "Xing", 4) == 0;
    const bool isInfo = std::memcmp(fixed, "Info", 4) == 0;
    if (!isXing && !isInfo) {
        return false;
    }
    tagSaysVbr = isXing;

    const uint32_t flags = readBe32(fixed + 4);
    uint32_t cursor = tagOffset + 8;
    uint8_t value[4];
    if (flags & 0x01U) {
        if (!readExact(file, cursor, value, sizeof(value))) {
            return false;
        }
        info.totalFrames = readBe32(value);
        cursor += 4;
    }
    if (flags & 0x02U) {
        if (!readExact(file, cursor, value, sizeof(value))) {
            return false;
        }
        info.indexedAudioBytes = readBe32(value);
        cursor += 4;
    }
    if (flags & 0x04U) {
        if (!readExact(file, cursor, info.xingToc, Mp3Info::kXingTocSize)) {
            return false;
        }
        info.hasXingToc = true;
        info.seekIndex = Mp3SeekIndex::Xing;
    }
    return true;
}

bool parseVbriAt(File& file, uint32_t tagOffset, uint32_t frameEnd,
                 Mp3Info& info) {
    uint8_t header[26];
    if (tagOffset + sizeof(header) > frameEnd ||
        !readExact(file, tagOffset, header, sizeof(header)) ||
        std::memcmp(header, "VBRI", 4) != 0) {
        return false;
    }

    const uint32_t bytes = readBe32(header + 10);
    const uint32_t frames = readBe32(header + 14);
    const uint16_t entries = readBe16(header + 18);
    const uint16_t scale = readBe16(header + 20);
    const uint16_t entryBytes = readBe16(header + 22);
    const uint16_t framesPerEntry = readBe16(header + 24);
    if (bytes == 0 || frames == 0 || entries == 0 ||
        entries > kMaxVbriEntries || scale == 0 || entryBytes == 0 ||
        entryBytes > 4 || framesPerEntry == 0) {
        return false;
    }
    const uint64_t tableEnd = static_cast<uint64_t>(tagOffset) +
                              sizeof(header) +
                              static_cast<uint64_t>(entries) * entryBytes;
    if (tableEnd > frameEnd) {
        return false;
    }

    info.totalFrames = frames;
    info.indexedAudioBytes = bytes;
    info.vbriTableOffset = tagOffset + sizeof(header);
    info.vbriEntryCount = entries;
    info.vbriScale = scale;
    info.vbriEntryBytes = entryBytes;
    info.vbriFramesPerEntry = framesPerEntry;
    info.seekIndex = Mp3SeekIndex::Vbri;
    return true;
}

bool parseVbri(File& file, uint32_t frameOffset,
               const FrameHeader& frame, Mp3Info& info) {
    const uint32_t frameEnd = frameOffset + frame.frameLength;
    // The Fraunhofer header is normally 32 bytes after the MPEG header
    // (absolute +36). Accept +32 as a bounded compatibility fallback.
    return parseVbriAt(file, frameOffset + 36, frameEnd, info) ||
           parseVbriAt(file, frameOffset + 32, frameEnd, info);
}

bool readVariableWidthBe(File& file, uint32_t offset, uint16_t width,
                         uint32_t& value) {
    uint8_t bytes[4]{};
    if (width == 0 || width > sizeof(bytes) ||
        !readExact(file, offset, bytes, width)) {
        return false;
    }
    value = 0;
    for (uint16_t index = 0; index < width; ++index) {
        value = (value << 8) | bytes[index];
    }
    return true;
}

uint32_t estimatePositionMs(File& file, const Mp3Info& info,
                            uint32_t byteOffset) {
    if (byteOffset <= info.firstFrameOffset || info.durationMs == 0) {
        return 0;
    }
    const uint32_t boundedOffset = std::min(byteOffset, info.audioEndOffset);
    const uint64_t relative = boundedOffset - info.firstFrameOffset;

    if (!info.variableBitrate && info.bitrateKbps != 0) {
        return static_cast<uint32_t>(std::min<uint64_t>(
            relative * 8ULL / info.bitrateKbps, info.durationMs));
    }

    if (info.seekIndex == Mp3SeekIndex::Xing && info.hasXingToc) {
        const uint32_t indexedBytes =
            info.indexedAudioBytes != 0
                ? info.indexedAudioBytes
                : info.audioEndOffset - info.firstFrameOffset;
        if (indexedBytes != 0) {
            const double scaled = std::min(
                256.0, static_cast<double>(relative) * 256.0 / indexedBytes);
            size_t index = 0;
            while (index < 99 && info.xingToc[index + 1] <= scaled) {
                ++index;
            }
            const double lower = info.xingToc[index];
            const double upper = index < 99 ? info.xingToc[index + 1] : 256.0;
            const double fraction = upper > lower
                                        ? (scaled - lower) / (upper - lower)
                                        : 0.0;
            const double percent = std::min(100.0, index + fraction);
            return static_cast<uint32_t>(percent * info.durationMs / 100.0);
        }
    }

    if (info.seekIndex == Mp3SeekIndex::Vbri &&
        info.vbriEntryCount > 0 && info.totalFrames > 0) {
        uint64_t accumulatedBytes = 0;
        uint64_t estimatedFrames = 0;
        for (uint32_t index = 0; index < info.vbriEntryCount; ++index) {
            uint32_t entry = 0;
            if (!readVariableWidthBe(
                    file, info.vbriTableOffset + index * info.vbriEntryBytes,
                    info.vbriEntryBytes, entry)) {
                break;
            }
            const uint64_t entryBytes =
                static_cast<uint64_t>(entry) * info.vbriScale;
            if (relative <= accumulatedBytes + entryBytes) {
                const uint64_t within = relative - accumulatedBytes;
                estimatedFrames += entryBytes == 0
                                       ? 0
                                       : within * info.vbriFramesPerEntry /
                                             entryBytes;
                break;
            }
            accumulatedBytes += entryBytes;
            estimatedFrames += info.vbriFramesPerEntry;
        }
        return static_cast<uint32_t>(std::min<uint64_t>(
            estimatedFrames * info.samplesPerFrame * 1000ULL /
                info.sampleRateHz,
            info.durationMs));
    }

    const uint64_t audioBytes =
        info.audioEndOffset - info.firstFrameOffset;
    return audioBytes == 0
               ? 0
               : static_cast<uint32_t>(std::min<uint64_t>(
                     relative * info.durationMs / audioBytes,
                     info.durationMs));
}

}  // namespace

bool Mp3Probe::probe(fs::FS& filesystem, const char* path, Mp3Info& info,
                     Mp3ProbeError& error) {
    info = Mp3Info{};
    error = Mp3ProbeError::None;
    if (path == nullptr || path[0] == '\0') {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    File file = filesystem.open(path, FILE_READ);
    if (!file) {
        error = Mp3ProbeError::OpenFailed;
        return false;
    }
    info.fileSize = static_cast<uint32_t>(file.size());
    if (info.fileSize < 8) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    info.audioStartOffset = id3v2End(file, info.fileSize);
    info.audioEndOffset =
        audioEndBeforeTrailingTags(file, info.audioStartOffset, info.fileSize);
    if (info.audioStartOffset >= info.audioEndOffset) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    FrameHeader first;
    if (!findFrame(file, info.audioStartOffset, info.audioEndOffset,
                   kInitialFrameScanLimit, true, info.firstFrameOffset,
                   first)) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }
    info.firstFrameLength = first.frameLength;
    info.sampleRateHz = first.sampleRateHz;
    info.samplesPerFrame = first.samplesPerFrame;
    info.bitrateKbps = first.bitrateKbps;

    uint32_t offset = info.firstFrameOffset;
    uint32_t bitrateSum = 0;
    uint16_t scannedFrames = 0;
    bool bitrateChanged = false;
    while (scannedFrames < 64 && offset + 4 <= info.audioEndOffset) {
        FrameHeader current;
        if (!readFrameHeader(file, offset, current) ||
            !compatibleFrames(first, current) ||
            offset + current.frameLength > info.audioEndOffset) {
            break;
        }
        bitrateSum += current.bitrateKbps;
        bitrateChanged = bitrateChanged ||
                         current.bitrateKbps != first.bitrateKbps;
        ++scannedFrames;
        offset += current.frameLength;
    }
    if (scannedFrames < 2) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    bool xingSaysVbr = false;
    const bool hasXing =
        parseXing(file, info.firstFrameOffset, first, info, xingSaysVbr);
    const bool hasVbri = parseVbri(file, info.firstFrameOffset, first, info);
    const uint32_t availableAudioBytes =
        info.audioEndOffset - info.firstFrameOffset;
    // A truncated file can retain the original Xing/Info/VBRI header. Reject
    // it before playback when that header promises more audio than exists.
    // This lets the decoder treat libmad's normal terminal BUFLEN as EOF
    // without turning our deliberately truncated fixture into TrackEnded.
    if (info.indexedAudioBytes > availableAudioBytes) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }
    info.variableBitrate = xingSaysVbr || hasVbri || bitrateChanged;

    if (info.totalFrames > 0) {
        const uint64_t duration =
            static_cast<uint64_t>(info.totalFrames) * info.samplesPerFrame *
            1000ULL / info.sampleRateHz;
        info.durationMs = static_cast<uint32_t>(
            std::min<uint64_t>(duration, UINT32_MAX));
    } else {
        const uint32_t averageBitrate =
            scannedFrames == 0 ? first.bitrateKbps
                               : bitrateSum / scannedFrames;
        const uint64_t audioBytes =
            info.audioEndOffset - info.firstFrameOffset;
        info.durationMs = averageBitrate == 0
                              ? 0
                              : static_cast<uint32_t>(std::min<uint64_t>(
                                    audioBytes * 8ULL / averageBitrate,
                                    UINT32_MAX));
    }
    if (info.durationMs == 0) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    if (info.variableBitrate) {
        const uint64_t bytes = info.indexedAudioBytes != 0
                                   ? info.indexedAudioBytes
                                   : info.audioEndOffset - info.firstFrameOffset;
        const uint64_t average = bytes * 8ULL / info.durationMs;
        if (average > 0 && average <= UINT16_MAX) {
            info.bitrateKbps = static_cast<uint16_t>(average);
        }
    }

    // An Info header may carry a useful TOC for a CBR file. Keep the index
    // while correctly reporting variableBitrate=false.
    if (hasXing && info.hasXingToc) {
        info.seekIndex = Mp3SeekIndex::Xing;
    }
    error = Mp3ProbeError::None;
    return true;
}

bool Mp3Probe::seekPoint(fs::FS& filesystem, const char* path,
                         const Mp3Info& info, uint32_t targetMs,
                         Mp3SeekPoint& point, Mp3ProbeError& error,
                         uint32_t sourceOffsetHint) {
    point = Mp3SeekPoint{};
    error = Mp3ProbeError::None;
    if (path == nullptr || info.durationMs == 0 ||
        info.firstFrameOffset >= info.audioEndOffset) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    File file = filesystem.open(path, FILE_READ);
    if (!file) {
        error = Mp3ProbeError::OpenFailed;
        return false;
    }

    const uint32_t clampedMs =
        std::min(targetMs, info.durationMs > 0 ? info.durationMs - 1 : 0);
    uint64_t candidate = info.firstFrameOffset;
    const uint32_t hintedPositionMs =
        estimatePositionMs(file, info, sourceOffsetHint);
    const bool useSourceHint =
        info.variableBitrate && clampedMs > 0 &&
        sourceOffsetHint >= info.firstFrameOffset &&
        info.audioEndOffset > 4 &&
        sourceOffsetHint < info.audioEndOffset - 4 &&
        (hintedPositionMs > clampedMs
             ? hintedPositionMs - clampedMs
             : clampedMs - hintedPositionMs) <=
            kSourceHintMaximumTimeDeltaMs;

    if (clampedMs == 0) {
        point.byteOffset = info.firstFrameOffset;
        point.positionMs = 0;
        return true;
    } else if (useSourceHint) {
        candidate = sourceOffsetHint;
    } else if (info.seekIndex == Mp3SeekIndex::Xing && info.hasXingToc) {
        const double percent =
            static_cast<double>(clampedMs) * 100.0 / info.durationMs;
        const size_t index = static_cast<size_t>(std::min(99.0, percent));
        const double fraction = percent - index;
        const double lower = info.xingToc[index];
        const double upper = index < 99 ? info.xingToc[index + 1] : 256.0;
        const double tocValue = lower + (upper - lower) * fraction;
        const uint32_t indexedBytes =
            info.indexedAudioBytes != 0
                ? info.indexedAudioBytes
                : info.audioEndOffset - info.firstFrameOffset;
        candidate = info.firstFrameOffset +
                    static_cast<uint64_t>(tocValue * indexedBytes / 256.0);
    } else if (info.seekIndex == Mp3SeekIndex::Vbri &&
               info.vbriEntryCount > 0) {
        const uint64_t targetFrame =
            static_cast<uint64_t>(clampedMs) * info.totalFrames /
            info.durationMs;
        const uint32_t entryIndex = static_cast<uint32_t>(std::min<uint64_t>(
            targetFrame / info.vbriFramesPerEntry,
            info.vbriEntryCount - 1));
        uint64_t accumulated = 0;
        for (uint32_t index = 0; index <= entryIndex; ++index) {
            uint32_t entry = 0;
            if (!readVariableWidthBe(
                    file,
                    info.vbriTableOffset + index * info.vbriEntryBytes,
                    info.vbriEntryBytes, entry)) {
                error = Mp3ProbeError::IoError;
                return false;
            }
            const uint64_t scaled =
                static_cast<uint64_t>(entry) * info.vbriScale;
            if (index == entryIndex) {
                const uint64_t entryStartFrame =
                    static_cast<uint64_t>(index) * info.vbriFramesPerEntry;
                const uint64_t within = targetFrame > entryStartFrame
                                            ? targetFrame - entryStartFrame
                                            : 0;
                accumulated += scaled *
                               std::min<uint64_t>(within,
                                                  info.vbriFramesPerEntry) /
                               info.vbriFramesPerEntry;
            } else {
                accumulated += scaled;
            }
        }
        candidate = info.firstFrameOffset + accumulated;
    } else if (!info.variableBitrate && info.bitrateKbps > 0) {
        candidate = info.firstFrameOffset +
                    static_cast<uint64_t>(clampedMs) * info.bitrateKbps / 8ULL;
    } else {
        const uint64_t audioBytes =
            info.audioEndOffset - info.firstFrameOffset;
        candidate = info.firstFrameOffset +
                    audioBytes * clampedMs / info.durationMs;
    }

    const uint32_t maximumCandidate = info.audioEndOffset > 4
                                          ? info.audioEndOffset - 4
                                          : info.firstFrameOffset;
    const uint32_t start = static_cast<uint32_t>(std::min<uint64_t>(
        std::max<uint64_t>(candidate, info.firstFrameOffset),
        maximumCandidate));
    FrameHeader found;
    uint32_t foundOffset = 0;
    if (!findFrame(file, start, info.audioEndOffset, kSeekResyncLimit, false,
                   foundOffset, found)) {
        error = Mp3ProbeError::Unsupported;
        return false;
    }

    point.byteOffset = foundOffset;
    point.positionMs = estimatePositionMs(file, info, foundOffset);
    return true;
}

}  // namespace player
}  // namespace adv_walkman
