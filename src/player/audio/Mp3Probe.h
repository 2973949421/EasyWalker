#pragma once

#include <Arduino.h>
#include <FS.h>

namespace adv_walkman {
namespace player {

enum class Mp3ProbeError : uint8_t {
    None,
    OpenFailed,
    IoError,
    Unsupported,
};

enum class Mp3SeekIndex : uint8_t {
    None,
    Xing,
    Vbri,
};

struct Mp3Info {
    static constexpr size_t kXingTocSize = 100;

    uint32_t fileSize = 0;
    uint32_t audioStartOffset = 0;
    uint32_t audioEndOffset = 0;
    uint32_t firstFrameOffset = 0;
    uint32_t firstFrameLength = 0;
    uint32_t durationMs = 0;
    uint32_t sampleRateHz = 0;
    uint32_t totalFrames = 0;
    uint32_t indexedAudioBytes = 0;
    uint16_t bitrateKbps = 0;
    uint16_t samplesPerFrame = 0;
    bool variableBitrate = false;
    Mp3SeekIndex seekIndex = Mp3SeekIndex::None;

    uint8_t xingToc[kXingTocSize]{};
    bool hasXingToc = false;

    uint32_t vbriTableOffset = 0;
    uint16_t vbriEntryCount = 0;
    uint16_t vbriScale = 0;
    uint16_t vbriEntryBytes = 0;
    uint16_t vbriFramesPerEntry = 0;
};

struct Mp3SeekPoint {
    uint32_t byteOffset = 0;
    uint32_t positionMs = 0;
};

class Mp3Probe final {
  public:
    static bool probe(fs::FS& filesystem, const char* path, Mp3Info& info,
                      Mp3ProbeError& error);

    static bool seekPoint(fs::FS& filesystem, const char* path,
                          const Mp3Info& info, uint32_t targetMs,
                          Mp3SeekPoint& point, Mp3ProbeError& error,
                          uint32_t sourceOffsetHint = 0);
};

}  // namespace player
}  // namespace adv_walkman
