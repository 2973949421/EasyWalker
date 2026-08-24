#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>

#include <cstdlib>
#include <cstring>

#include "BenchmarkBackend.h"

#ifndef BENCH_BACKEND_ID
#define BENCH_BACKEND_ID 0
#endif

#if BENCH_BACKEND_ID == 0
#include "CandidateABackend.h"
#elif BENCH_BACKEND_ID == 1
#include "CandidateBBackend.h"
#elif BENCH_BACKEND_ID == 2
#include "CandidateCBackend.h"
#else
#error "Unsupported BENCH_BACKEND_ID"
#endif

namespace {

using adv_walkman::BackendState;
using adv_walkman::BenchmarkBackend;
using adv_walkman::BenchmarkStats;

constexpr char kBenchmarkPath[] =
    "/Music/ADVWalkmanBenchmark/benchmark.mp3";
constexpr uint32_t kStatsIntervalMs = 5000;
constexpr uint32_t kUiStressIntervalMs = 1000 / 30;
constexpr uint32_t kSdStressIntervalMs = 8;
constexpr size_t kCommandCapacity = 64;
constexpr size_t kSdStressChunkSize = 4096;

#if BENCH_BACKEND_ID == 0
adv_walkman::CandidateABackend backendInstance;
constexpr char kDependencyVersions[] =
    "platform=espressif32-6.7.0 arduino=2.0.16 "
    "audio=ESP8266Audio-1.9.7";
#elif BENCH_BACKEND_ID == 1
adv_walkman::CandidateBBackend backendInstance;
constexpr char kDependencyVersions[] =
    "platform=espressif32-6.7.0 arduino=2.0.16 "
    "audio=ESP8266Audio-1.9.7";
#elif BENCH_BACKEND_ID == 2
adv_walkman::CandidateCBackend backendInstance;
constexpr char kDependencyVersions[] =
    "platform=pioarduino-55.03.38-1 arduino=3.3.8 idf=5.5.4 "
    "audio=BackgroundAudio-1.4.4";
#endif

BenchmarkBackend& backend = backendInstance;
char commandBuffer[kCommandCapacity]{};
size_t commandLength = 0;
uint32_t lastStatsAtMs = 0;
uint32_t lastUiStressAtMs = 0;
uint32_t lastSdStressAtMs = 0;
uint32_t uiStressFrames = 0;
uint32_t sdStressBytes = 0;
BackendState lastRenderedState = BackendState::BOOT;
bool loopEnabled = false;
bool uiStressEnabled = false;
bool sdStressEnabled = false;
File sdStressFile;
uint8_t sdStressBuffer[kSdStressChunkSize]{};

void formatOptionalMetric(int32_t value, char output[16]) {
    if (value < 0) {
        strcpy(output, "NA");
    } else {
        snprintf(output, 16, "%ld", static_cast<long>(value));
    }
}

void printStats() {
    const BenchmarkStats stats = backend.stats();
    char decoderErrors[16];
    char decoderUnderflows[16];
    char outputUnderflows[16];
    formatOptionalMetric(stats.decoderErrors, decoderErrors);
    formatOptionalMetric(stats.decoderUnderflows, decoderUnderflows);
    formatOptionalMetric(stats.outputUnderflows, outputUnderflows);

    Serial.printf(
        "stats backend=%s state=%s sample_rate=%lu free_heap=%lu "
        "minimum_heap=%lu elapsed_ms=%lu decode_calls=%lu "
        "backpressure=%lu bytes_read=%lu track_loops=%lu "
        "decoder_errors=%s decoder_underflows=%s output_underflows=%s "
        "service_max_us=%lu ui_stress_frames=%lu sd_stress_bytes=%lu "
        "error=%s\n",
        backend.name(), adv_walkman::backendStateName(stats.state),
        static_cast<unsigned long>(stats.sampleRate),
        static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(stats.elapsedMs),
        static_cast<unsigned long>(stats.decodeServiceCalls),
        static_cast<unsigned long>(stats.backpressureEvents),
        static_cast<unsigned long>(stats.bytesRead),
        static_cast<unsigned long>(stats.trackLoops), decoderErrors,
        decoderUnderflows, outputUnderflows,
        static_cast<unsigned long>(stats.serviceMaxUs),
        static_cast<unsigned long>(uiStressFrames),
        static_cast<unsigned long>(sdStressBytes), stats.error);
    Serial.printf(
        "fixture path=%s sha256=%s loop=%s ui_stress=%s sd_stress=%s\n",
        kBenchmarkPath, stats.fileSha256, loopEnabled ? "on" : "off",
        uiStressEnabled ? "on" : "off", sdStressEnabled ? "on" : "off");
}

void renderState(bool force = false) {
    if (uiStressEnabled) {
        return;
    }
    const BenchmarkStats stats = backend.stats();
    if (!force && stats.state == lastRenderedState) {
        return;
    }
    lastRenderedState = stats.state;

    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(8, 8);
    display.setTextColor(GREEN, BLACK);
    display.setTextSize(1.5f);
    display.println("ADV Walkman P0");
    display.setTextSize(1.0f);
    display.printf("Backend: %s\n", backend.name());
    display.printf("State: %s\n", adv_walkman::backendStateName(stats.state));
    display.printf("Loops: %lu\n", static_cast<unsigned long>(stats.trackLoops));
    if (strcmp(stats.error, "none") != 0) {
        display.setTextColor(ORANGE, BLACK);
        display.printf("Error: %s\n", stats.error);
    }
}

void serviceUiStress(uint32_t now) {
    if (!uiStressEnabled || now - lastUiStressAtMs < kUiStressIntervalMs) {
        return;
    }
    lastUiStressAtMs = now;
    ++uiStressFrames;
    auto& display = M5Cardputer.Display;
    const uint16_t color = static_cast<uint16_t>((uiStressFrames * 977u) & 0xFFFFu);
    display.fillScreen(color);
    display.setTextColor(static_cast<uint16_t>(~color), color);
    display.setCursor(8, 8);
    display.printf("%s\nframe=%lu", backend.name(),
                   static_cast<unsigned long>(uiStressFrames));
}

bool setSdStress(bool enabled) {
    if (!enabled) {
        if (sdStressFile) {
            sdStressFile.close();
        }
        sdStressEnabled = false;
        return true;
    }
    if (!sdStressFile) {
        sdStressFile = SD.open(kBenchmarkPath, FILE_READ);
    }
    sdStressEnabled = static_cast<bool>(sdStressFile);
    return sdStressEnabled;
}

void serviceSdStress(uint32_t now) {
    if (!sdStressEnabled || now - lastSdStressAtMs < kSdStressIntervalMs) {
        return;
    }
    lastSdStressAtMs = now;
    size_t count = sdStressFile.read(sdStressBuffer, sizeof(sdStressBuffer));
    if (count == 0) {
        sdStressFile.seek(0);
        count = sdStressFile.read(sdStressBuffer, sizeof(sdStressBuffer));
    }
    sdStressBytes += static_cast<uint32_t>(count);
}

bool parseOnOff(const char* value, bool& enabled) {
    if (strcmp(value, "on") == 0) {
        enabled = true;
        return true;
    }
    if (strcmp(value, "off") == 0) {
        enabled = false;
        return true;
    }
    return false;
}

void executeCommand(char* command) {
    for (char* cursor = command; *cursor != '\0'; ++cursor) {
        if (*cursor >= 'A' && *cursor <= 'Z') {
            *cursor = static_cast<char>(*cursor - 'A' + 'a');
        }
    }

    bool success = true;
    if (strcmp(command, "play") == 0) {
        success = backend.begin(kBenchmarkPath);
        backend.setLoop(loopEnabled);
    } else if (strcmp(command, "pause") == 0) {
        success = backend.pause();
    } else if (strcmp(command, "resume") == 0) {
        success = backend.resume();
    } else if (strcmp(command, "stop") == 0) {
        backend.stop();
    } else if (strcmp(command, "restart") == 0) {
        success = backend.restart();
    } else if (strncmp(command, "seek ", 5) == 0) {
        char* end = nullptr;
        const unsigned long seconds = strtoul(command + 5, &end, 10);
        success = end != command + 5 && *end == '\0' &&
                  backend.seekSeconds(static_cast<uint32_t>(seconds));
    } else if (strncmp(command, "loop ", 5) == 0) {
        success = parseOnOff(command + 5, loopEnabled);
        if (success) {
            backend.setLoop(loopEnabled);
        }
    } else if (strncmp(command, "stress ui ", 10) == 0) {
        success = parseOnOff(command + 10, uiStressEnabled);
        if (!uiStressEnabled) {
            renderState(true);
        }
    } else if (strncmp(command, "stress sd ", 10) == 0) {
        bool enabled = false;
        success = parseOnOff(command + 10, enabled) && setSdStress(enabled);
    } else if (strcmp(command, "status") == 0) {
        printStats();
    } else if (*command != '\0') {
        Serial.printf("command result=ERROR command=%s reason=unknown_command\n",
                      command);
        return;
    }

    Serial.printf("command result=%s command=%s\n",
                  success ? "OK" : "ERROR", command);
    renderState(true);
}

void serviceSerial() {
    while (Serial.available() > 0) {
        const char input = static_cast<char>(Serial.read());
        if (input == '\r' || input == '\n') {
            if (commandLength > 0) {
                commandBuffer[commandLength] = '\0';
                executeCommand(commandBuffer);
            }
            commandLength = 0;
            continue;
        }
        if (commandLength + 1 < kCommandCapacity) {
            commandBuffer[commandLength++] = input;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t serialWaitStarted = millis();
    while (!Serial && millis() - serialWaitStarted < 1500) {
        delay(10);
    }

    auto config = M5.config();
#if BENCH_BACKEND_ID != 0
    config.internal_spk = false;
    config.internal_mic = false;
#endif
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);

    Serial.printf(
        "boot app=adv-walkman-p0 version=%s backend=%s path=%s\n",
        ADV_WALKMAN_VERSION, backend.name(), kBenchmarkPath);
    Serial.printf("dependencies %s\n", kDependencyVersions);
    backend.begin(kBenchmarkPath);
    backend.setLoop(loopEnabled);
    printStats();
    renderState(true);
    lastStatsAtMs = millis();
}

void loop() {
    M5Cardputer.update();
    serviceSerial();
    backend.service();

    const uint32_t now = millis();
    serviceUiStress(now);
    serviceSdStress(now);
    renderState();

    if (now - lastStatsAtMs >= kStatsIntervalMs) {
        lastStatsAtMs = now;
        printStats();
    }
    delay(1);
}
