#pragma once

#include <Arduino.h>

namespace adv_walkman {

enum class BackendId : uint8_t {
    A_M5Speaker = 0,
    B_DirectI2S = 1,
    C_BackgroundAudio = 2,
};

enum class BackendState : uint8_t {
    BOOT,
    READY,
    PLAYING,
    PAUSED,
    STOPPED,
    ERROR,
    NOT_IMPLEMENTED,
};

struct BenchmarkStats {
    BackendId backend;
    BackendState state;
    uint32_t sampleRate;
    uint32_t elapsedMs;
    uint32_t decodeServiceCalls;
    uint32_t backpressureEvents;
    int32_t underruns;
    const char* fileSha256;
    const char* error;
};

class BenchmarkBackend {
  public:
    virtual ~BenchmarkBackend() = default;
    virtual bool begin(const char* path) = 0;
    virtual void service() = 0;
    virtual bool pause() = 0;
    virtual bool resume() = 0;
    virtual void stop() = 0;
    virtual BenchmarkStats stats() const = 0;
    virtual const char* name() const = 0;
};

const char* backendStateName(BackendState state);

}  // namespace adv_walkman
