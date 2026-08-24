#include <Arduino.h>
#include <M5Cardputer.h>

#include <cstring>

#include "BenchmarkBackend.h"
#include "PlaceholderBackend.h"

#ifndef BENCH_BACKEND_ID
#define BENCH_BACKEND_ID 0
#endif

#if BENCH_BACKEND_ID == 0
#include "CandidateABackend.h"
#endif

namespace {

using adv_walkman::BackendId;
using adv_walkman::BackendState;
using adv_walkman::BenchmarkBackend;
using adv_walkman::BenchmarkStats;

constexpr char kBenchmarkPath[] =
    "/Music/ADVWalkmanBenchmark/benchmark.mp3";
constexpr uint32_t kStatsIntervalMs = 5000;
constexpr size_t kCommandCapacity = 32;

#if BENCH_BACKEND_ID == 0
adv_walkman::CandidateABackend backendInstance;
#elif BENCH_BACKEND_ID == 1
adv_walkman::PlaceholderBackend backendInstance(
    BackendId::B_DirectI2S, "B_DIRECT_I2S");
#elif BENCH_BACKEND_ID == 2
adv_walkman::PlaceholderBackend backendInstance(
    BackendId::C_BackgroundAudio, "C_BACKGROUND_AUDIO");
#else
#error "Unsupported BENCH_BACKEND_ID"
#endif

BenchmarkBackend& backend = backendInstance;
char commandBuffer[kCommandCapacity]{};
size_t commandLength = 0;
uint32_t lastStatsAtMs = 0;
BackendState lastRenderedState = BackendState::BOOT;

void printStats() {
    const BenchmarkStats stats = backend.stats();
    char underrunValue[16];
    if (stats.underruns < 0) {
        strcpy(underrunValue, "NA");
    } else {
        snprintf(underrunValue, sizeof(underrunValue), "%ld",
                 static_cast<long>(stats.underruns));
    }
    Serial.printf(
        "stats backend=%s state=%s sample_rate=%lu free_heap=%lu "
        "minimum_heap=%lu elapsed_ms=%lu decode_calls=%lu "
        "backpressure=%lu underrun=%s error=%s\n",
        backend.name(), adv_walkman::backendStateName(stats.state),
        static_cast<unsigned long>(stats.sampleRate),
        static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(stats.elapsedMs),
        static_cast<unsigned long>(stats.decodeServiceCalls),
        static_cast<unsigned long>(stats.backpressureEvents),
        underrunValue, stats.error);
    Serial.printf("fixture path=%s sha256=%s\n", kBenchmarkPath,
                  stats.fileSha256);
}

void renderState(bool force = false) {
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
    display.println("ADV Walkman P0-01");
    display.setTextSize(1.0f);
    display.printf("Backend: %s\n", backend.name());
    display.printf("State: %s\n", adv_walkman::backendStateName(stats.state));
    if (strcmp(stats.error, "none") != 0) {
        display.setTextColor(ORANGE, BLACK);
        display.printf("Error: %s\n", stats.error);
    }
}

void executeCommand(char* command) {
    for (char* cursor = command; *cursor != '\0'; ++cursor) {
        if (*cursor >= 'A' && *cursor <= 'Z') {
            *cursor = static_cast<char>(*cursor - 'A' + 'a');
        }
    }

    if (strcmp(command, "play") == 0) {
        backend.begin(kBenchmarkPath);
    } else if (strcmp(command, "pause") == 0) {
        backend.pause();
    } else if (strcmp(command, "resume") == 0) {
        backend.resume();
    } else if (strcmp(command, "stop") == 0) {
        backend.stop();
    } else if (strcmp(command, "status") == 0) {
        printStats();
    } else if (*command != '\0') {
        Serial.printf("error unknown_command=%s\n", command);
    }
    renderState(true);
}

void serviceSerial() {
    while (Serial.available() > 0) {
        const char input = static_cast<char>(Serial.read());
        if (input == '\r' || input == '\n') {
            commandBuffer[commandLength] = '\0';
            executeCommand(commandBuffer);
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
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);

    Serial.printf(
        "boot app=adv-walkman-p0-01 version=%s backend=%s path=%s\n",
        ADV_WALKMAN_VERSION, backend.name(), kBenchmarkPath);
    backend.begin(kBenchmarkPath);
    printStats();
    renderState(true);
    lastStatsAtMs = millis();
}

void loop() {
    M5Cardputer.update();
    serviceSerial();
    backend.service();
    renderState();

    const uint32_t now = millis();
    if (now - lastStatsAtMs >= kStatsIntervalMs) {
        lastStatsAtMs = now;
        printStats();
    }
    delay(1);
}
