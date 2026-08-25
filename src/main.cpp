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
constexpr uint32_t kAutoBaselineMs = 30000;
constexpr uint32_t kAutoUiMs = 60000;
constexpr uint32_t kAutoUiSdMs = 60000;
constexpr uint32_t kAutoPauseMs = 3000;
constexpr uint32_t kAutoResumeMs = 10000;
constexpr uint32_t kAutoSeekMs = 10000;
constexpr uint32_t kAutoRestartMs = 10000;
constexpr uint32_t kAutoMinimumUiFrames = 3600;
constexpr uint32_t kAutoMinimumSdBytes = 1024 * 1024;
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

enum class AutoStressPhase : uint8_t {
    IDLE,
    BASELINE,
    UI,
    UI_SD,
    PAUSE,
    RESUME,
    SEEK,
    RESTART,
    COMPLETE,
    FAILED,
};

struct AutoStressRun {
    AutoStressPhase phase = AutoStressPhase::IDLE;
    uint32_t phaseStartedAtMs = 0;
    uint32_t initialFreeHeap = 0;
    uint32_t initialBackpressure = 0;
    uint32_t initialUiFrames = 0;
    uint32_t initialSdBytes = 0;
    uint32_t trackLoopsAtSeek = 0;
    bool previousLoopEnabled = false;
    const char* failure = "none";
};

AutoStressRun autoStress;
bool lastAutoStressTriggerDown = false;

bool autoStressOwnsDisplay() {
    return autoStress.phase != AutoStressPhase::IDLE;
}

bool autoStressIsRunning() {
    return autoStress.phase != AutoStressPhase::IDLE &&
           autoStress.phase != AutoStressPhase::COMPLETE &&
           autoStress.phase != AutoStressPhase::FAILED;
}

const char* autoStressPhaseName(AutoStressPhase phase) {
    switch (phase) {
        case AutoStressPhase::IDLE:
            return "IDLE";
        case AutoStressPhase::BASELINE:
            return "BASELINE 30s";
        case AutoStressPhase::UI:
            return "UI 60s";
        case AutoStressPhase::UI_SD:
            return "UI+SD 60s";
        case AutoStressPhase::PAUSE:
            return "PAUSE 3s";
        case AutoStressPhase::RESUME:
            return "RESUME 10s";
        case AutoStressPhase::SEEK:
            return "SEEK 60s";
        case AutoStressPhase::RESTART:
            return "RESTART 10s";
        case AutoStressPhase::COMPLETE:
            return "PASS";
        case AutoStressPhase::FAILED:
            return "FAIL";
    }
    return "UNKNOWN";
}

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
    if (uiStressEnabled || autoStressOwnsDisplay()) {
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
    display.println("T: Auto Stress");
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

void renderAutoStress() {
    if (uiStressEnabled && autoStressIsRunning()) {
        return;
    }

    const BenchmarkStats stats = backend.stats();
    const uint32_t backpressureDelta =
        stats.backpressureEvents - autoStress.initialBackpressure;
    const uint32_t uiFramesDelta = uiStressFrames - autoStress.initialUiFrames;
    const uint32_t sdBytesDelta = sdStressBytes - autoStress.initialSdBytes;
    const int32_t heapDelta = static_cast<int32_t>(ESP.getFreeHeap()) -
                              static_cast<int32_t>(autoStress.initialFreeHeap);

    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(6, 5);
    display.setTextSize(1.0f);
    if (autoStress.phase == AutoStressPhase::COMPLETE) {
        display.setTextColor(GREEN, BLACK);
        display.println("AUTO STRESS: PASS");
    } else if (autoStress.phase == AutoStressPhase::FAILED) {
        display.setTextColor(RED, BLACK);
        display.println("AUTO STRESS: FAIL");
    } else {
        display.setTextColor(YELLOW, BLACK);
        display.println("AUTO STRESS RUNNING");
    }

    display.setTextColor(WHITE, BLACK);
    display.printf("Phase: %s\n", autoStressPhaseName(autoStress.phase));
    display.printf("State=%s SR=%lu\n", adv_walkman::backendStateName(stats.state),
                   static_cast<unsigned long>(stats.sampleRate));
    if (autoStress.phase == AutoStressPhase::COMPLETE) {
        display.printf("Heap d=%ld min=%lu\n", static_cast<long>(heapDelta),
                       static_cast<unsigned long>(ESP.getMinFreeHeap()));
        display.printf("BP=%lu svc=%luus\n",
                       static_cast<unsigned long>(backpressureDelta),
                       static_cast<unsigned long>(stats.serviceMaxUs));
        display.printf("UI=%lu SD=%luKiB\n",
                       static_cast<unsigned long>(uiFramesDelta),
                       static_cast<unsigned long>(sdBytesDelta / 1024));
        display.println("Listen: manual");
        display.println("Press T to rerun");
    } else if (autoStress.phase == AutoStressPhase::FAILED) {
        display.printf("Reason: %s\n", autoStress.failure);
        display.printf("Error: %s\n", stats.error);
        display.println("Press T to rerun");
    } else {
        display.println("Please wait; no keys needed");
    }
}

void stopAutoStressLoads() {
    uiStressEnabled = false;
    setSdStress(false);
    loopEnabled = autoStress.previousLoopEnabled;
    backend.setLoop(loopEnabled);
}

void failAutoStress(const char* reason) {
    stopAutoStressLoads();
    autoStress.failure = reason;
    autoStress.phase = AutoStressPhase::FAILED;
    Serial.printf("auto_stress result=FAIL reason=%s\n", reason);
    renderAutoStress();
}

void advanceAutoStress(AutoStressPhase phase, uint32_t now) {
    autoStress.phase = phase;
    autoStress.phaseStartedAtMs = now;
    Serial.printf("auto_stress phase=%s\n", autoStressPhaseName(phase));
    renderAutoStress();
}

void finishAutoStress() {
    stopAutoStressLoads();
    const BenchmarkStats stats = backend.stats();
    if (strcmp(stats.error, "none") != 0) {
        failAutoStress("backend_error");
        return;
    }
    if (stats.state != BackendState::PLAYING) {
        failAutoStress("final_state");
        return;
    }
    if (stats.sampleRate != 44100) {
        failAutoStress("sample_rate");
        return;
    }
    if (uiStressFrames - autoStress.initialUiFrames <
        kAutoMinimumUiFrames) {
        failAutoStress("ui_rate_low");
        return;
    }
    if (sdStressBytes - autoStress.initialSdBytes < kAutoMinimumSdBytes) {
        failAutoStress("sd_read_low");
        return;
    }

    autoStress.phase = AutoStressPhase::COMPLETE;
    Serial.println("auto_stress result=PASS");
    renderAutoStress();
}

void startAutoStress(uint32_t now) {
    const bool previousLoopEnabled = loopEnabled;
    uiStressEnabled = false;
    setSdStress(false);
    autoStress = AutoStressRun{};
    autoStress.previousLoopEnabled = previousLoopEnabled;
    autoStress.phase = AutoStressPhase::BASELINE;
    autoStress.phaseStartedAtMs = now;

    loopEnabled = true;
    backend.setLoop(true);
    if (!backend.restart()) {
        failAutoStress("initial_restart");
        return;
    }

    const BenchmarkStats stats = backend.stats();
    autoStress.initialFreeHeap = ESP.getFreeHeap();
    autoStress.initialBackpressure = stats.backpressureEvents;
    autoStress.initialUiFrames = uiStressFrames;
    autoStress.initialSdBytes = sdStressBytes;
    Serial.println("auto_stress start=1");
    renderAutoStress();
}

void serviceAutoStress(uint32_t now) {
    if (!autoStressIsRunning()) {
        return;
    }
    const BenchmarkStats currentStats = backend.stats();
    if (currentStats.state == BackendState::ERROR) {
        failAutoStress("backend_error");
        return;
    }
    const BackendState expectedState =
        autoStress.phase == AutoStressPhase::PAUSE
            ? BackendState::PAUSED
            : BackendState::PLAYING;
    if (currentStats.state != expectedState) {
        failAutoStress("unexpected_state");
        return;
    }
    const uint32_t elapsed = now - autoStress.phaseStartedAtMs;
    switch (autoStress.phase) {
        case AutoStressPhase::BASELINE:
            if (elapsed >= kAutoBaselineMs) {
                uiStressEnabled = true;
                advanceAutoStress(AutoStressPhase::UI, now);
            }
            break;
        case AutoStressPhase::UI:
            if (elapsed >= kAutoUiMs) {
                if (!setSdStress(true)) {
                    failAutoStress("sd_stress_open");
                    return;
                }
                advanceAutoStress(AutoStressPhase::UI_SD, now);
            }
            break;
        case AutoStressPhase::UI_SD:
            if (elapsed >= kAutoUiSdMs) {
                if (!backend.pause() ||
                    backend.stats().state != BackendState::PAUSED) {
                    failAutoStress("pause");
                    return;
                }
                advanceAutoStress(AutoStressPhase::PAUSE, now);
            }
            break;
        case AutoStressPhase::PAUSE:
            if (elapsed >= kAutoPauseMs) {
                if (!backend.resume() ||
                    backend.stats().state != BackendState::PLAYING) {
                    failAutoStress("resume");
                    return;
                }
                advanceAutoStress(AutoStressPhase::RESUME, now);
            }
            break;
        case AutoStressPhase::RESUME:
            if (elapsed >= kAutoResumeMs) {
                autoStress.trackLoopsAtSeek = backend.stats().trackLoops;
                if (!backend.seekSeconds(60) ||
                    backend.stats().state != BackendState::PLAYING) {
                    failAutoStress("seek_60");
                    return;
                }
                advanceAutoStress(AutoStressPhase::SEEK, now);
            }
            break;
        case AutoStressPhase::SEEK:
            if (currentStats.trackLoops != autoStress.trackLoopsAtSeek) {
                failAutoStress("seek_decode_loop");
                return;
            }
            if (elapsed >= kAutoSeekMs) {
                if (!backend.restart() ||
                    backend.stats().state != BackendState::PLAYING) {
                    failAutoStress("restart");
                    return;
                }
                advanceAutoStress(AutoStressPhase::RESTART, now);
            }
            break;
        case AutoStressPhase::RESTART:
            if (elapsed >= kAutoRestartMs) {
                finishAutoStress();
            }
            break;
        case AutoStressPhase::IDLE:
        case AutoStressPhase::COMPLETE:
        case AutoStressPhase::FAILED:
            break;
    }
}

void serviceAutoStressKey(uint32_t now) {
    const bool triggerDown = M5Cardputer.Keyboard.isKeyPressed('t');
    if (triggerDown && !lastAutoStressTriggerDown && !autoStressIsRunning()) {
        startAutoStress(now);
    }
    lastAutoStressTriggerDown = triggerDown;
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
    serviceAutoStressKey(millis());
    serviceSerial();
    backend.service();

    const uint32_t now = millis();
    serviceAutoStress(now);
    serviceUiStress(now);
    serviceSdStress(now);
    renderState();

    if (now - lastStatsAtMs >= kStatsIntervalMs) {
        lastStatsAtMs = now;
        printStats();
    }
    delay(1);
}
