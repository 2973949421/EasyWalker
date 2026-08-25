#include <Arduino.h>
#include <M5Cardputer.h>

#include <cstdlib>
#include <cstring>

#include "player/app/P1DeviceTestRunner.h"
#include "player/app/PlayerRuntime.h"
#include "player/support/AdvStorage.h"
#include "player/support/P1Fixtures.h"

namespace {

using adv_walkman::player::P1DeviceTestRunner;
using adv_walkman::player::PersistenceResult;
using adv_walkman::player::PlayerRuntime;
using adv_walkman::player::PlayerSnapshot;
using adv_walkman::player::PlayerState;
using adv_walkman::player::RepeatMode;

constexpr size_t kCommandCapacity = 80;
constexpr uint32_t kStatusIntervalMs = 5000;
constexpr uint32_t kDisplayIntervalMs = 500;

PlayerRuntime runtime;
P1DeviceTestRunner testRunner;
char command[kCommandCapacity]{};
size_t commandLength = 0;
uint32_t lastStatusAtMs = 0;
uint32_t lastDisplayAtMs = 0;
bool lastTestKeyDown = false;
bool sdMounted = false;

void printStatus() {
    const PlayerSnapshot snapshot = runtime.snapshot();
    char path[adv_walkman::player::kTrackPathCapacity]{};
    runtime.controller().currentPath(path, sizeof(path));
    Serial.printf(
        "player state=%s error=%s audio_error=%s track=%u/%u "
        "position_ms=%lu duration_ms=%lu source_offset=%lu sample_rate=%lu "
        "bitrate_kbps=%u vbr=%s repeat=%s shuffle=%s heap=%lu min_heap=%lu "
        "backpressure=%lu service_max_us=%lu track_ended=%lu audio_errors=%lu "
        "state_store=%s state_result=%s state_writes=%lu path=%s\n",
        adv_walkman::player::playerStateName(snapshot.state),
        adv_walkman::player::playerErrorName(snapshot.error),
        adv_walkman::player::audioErrorName(snapshot.audioError),
        static_cast<unsigned>(snapshot.currentIndex + 1),
        static_cast<unsigned>(snapshot.queueCount),
        static_cast<unsigned long>(snapshot.positionMs),
        static_cast<unsigned long>(snapshot.durationMs),
        static_cast<unsigned long>(snapshot.sourceByteOffset),
        static_cast<unsigned long>(snapshot.sampleRateHz),
        static_cast<unsigned>(snapshot.bitrateKbps),
        snapshot.variableBitrate ? "yes" : "no",
        adv_walkman::player::repeatModeName(snapshot.repeatMode),
        snapshot.shuffleEnabled ? "on" : "off",
        static_cast<unsigned long>(ESP.getFreeHeap()),
        static_cast<unsigned long>(ESP.getMinFreeHeap()),
        static_cast<unsigned long>(snapshot.backpressureEvents),
        static_cast<unsigned long>(snapshot.serviceMaxUs),
        static_cast<unsigned long>(snapshot.trackEndedEvents),
        static_cast<unsigned long>(snapshot.audioErrorEvents),
        runtime.stateStoreAvailable() ? "ready" : "unavailable",
        adv_walkman::player::persistenceResultName(
            runtime.lastPersistenceResult()),
        static_cast<unsigned long>(runtime.stateWriteCount()),
        path[0] == '\0' ? "none" : path);
}

void renderPlayer() {
    if (testRunner.ownsDisplay()) {
        return;
    }
    const uint32_t now = millis();
    if (now - lastDisplayAtMs < kDisplayIntervalMs) {
        return;
    }
    lastDisplayAtMs = now;

    const PlayerSnapshot snapshot = runtime.snapshot();
    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(7, 7);
    display.setTextColor(GREEN, BLACK);
    display.setTextSize(1.4f);
    display.println("ADV Walkman P1 Dev");
    display.setTextSize(1.0f);
    display.setTextColor(WHITE, BLACK);
    display.printf("State: %s\n",
                   adv_walkman::player::playerStateName(snapshot.state));
    display.printf("Track: %u/%u\n",
                   static_cast<unsigned>(snapshot.currentIndex + 1),
                   static_cast<unsigned>(snapshot.queueCount));
    display.printf("Pos: %lu / %lu ms\n",
                   static_cast<unsigned long>(snapshot.positionMs),
                   static_cast<unsigned long>(snapshot.durationMs));
    display.printf("SR: %lu  %s\n",
                   static_cast<unsigned long>(snapshot.sampleRateHz),
                   snapshot.variableBitrate ? "VBR" : "CBR");
    display.printf("Repeat: %s  Shuffle: %s\n",
                   adv_walkman::player::repeatModeName(snapshot.repeatMode),
                   snapshot.shuffleEnabled ? "On" : "Off");
    if (snapshot.state == PlayerState::Error) {
        display.setTextColor(ORANGE, BLACK);
        display.printf("Error: %s/%s\n",
                       adv_walkman::player::playerErrorName(snapshot.error),
                       adv_walkman::player::audioErrorName(snapshot.audioError));
    }
    display.setTextColor(YELLOW, BLACK);
    display.println("T: run current P1 gate");
}

void executeCommand(char* input) {
    while (*input == ' ') {
        ++input;
    }
    if (std::strcmp(input, "play") == 0) {
        Serial.printf("command play ok=%d\n", runtime.play());
    } else if (std::strcmp(input, "pause") == 0) {
        Serial.printf("command pause ok=%d\n", runtime.pause());
    } else if (std::strcmp(input, "resume") == 0) {
        Serial.printf("command resume ok=%d\n", runtime.resume());
    } else if (std::strcmp(input, "stop") == 0) {
        runtime.stop();
        Serial.println("command stop ok=1");
    } else if (std::strcmp(input, "next") == 0) {
        Serial.printf("command next ok=%d\n", runtime.next());
    } else if (std::strcmp(input, "prev") == 0) {
        Serial.printf("command prev ok=%d\n", runtime.previous());
    } else if (std::strncmp(input, "seek ", 5) == 0) {
        char* end = nullptr;
        const unsigned long seconds = std::strtoul(input + 5, &end, 10);
        const bool valid = end != input + 5 && *end == '\0' &&
                           seconds <= UINT32_MAX / 1000UL;
        Serial.printf("command seek ok=%d\n",
                      valid && runtime.seekToMs(
                                   static_cast<uint32_t>(seconds * 1000UL)));
    } else if (std::strcmp(input, "repeat off") == 0) {
        runtime.setRepeatMode(RepeatMode::Off);
        Serial.println("command repeat ok=1 mode=OFF");
    } else if (std::strcmp(input, "repeat all") == 0) {
        runtime.setRepeatMode(RepeatMode::All);
        Serial.println("command repeat ok=1 mode=ALL");
    } else if (std::strcmp(input, "repeat one") == 0) {
        runtime.setRepeatMode(RepeatMode::One);
        Serial.println("command repeat ok=1 mode=ONE");
    } else if (std::strcmp(input, "shuffle on") == 0) {
        runtime.setShuffleEnabled(true);
        Serial.println("command shuffle ok=1 value=on");
    } else if (std::strcmp(input, "shuffle off") == 0) {
        runtime.setShuffleEnabled(false);
        Serial.println("command shuffle ok=1 value=off");
    } else if (std::strcmp(input, "status") == 0) {
        printStatus();
    } else if (std::strcmp(input, "test") == 0) {
        Serial.printf("command test ok=%d\n", testRunner.start());
    } else {
        Serial.println(
            "commands: play pause resume stop next prev seek <seconds> "
            "repeat off|all|one shuffle on|off status test");
    }
}

void serviceSerial() {
    while (Serial.available() > 0) {
        const char value = static_cast<char>(Serial.read());
        if (value == '\r' || value == '\n') {
            if (commandLength > 0) {
                command[commandLength] = '\0';
                executeCommand(command);
                commandLength = 0;
            }
        } else if (commandLength + 1 < sizeof(command)) {
            command[commandLength++] = value;
        }
    }
}

void serviceTestKey() {
    const bool down = M5Cardputer.Keyboard.isKeyPressed('t');
    if (down && !lastTestKeyDown) {
        testRunner.start();
    }
    lastTestKeyDown = down;
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

    sdMounted = adv_walkman::player::mountAdvSd();
    Serial.printf(
        "boot app=adv-walkman-player version=%s gate=%d sd=%s\n",
        ADV_WALKMAN_VERSION, P1_DEVICE_GATE, sdMounted ? "ready" : "failed");
    Serial.println(
        "dependencies platform=espressif32-6.7.0 arduino=2.0.16 "
        "m5cardputer=1.1.1 m5unified=0.2.20 m5gfx=0.2.27 "
        "audio=ESP8266Audio-1.9.7");

    if (!sdMounted || !runtime.begin(true)) {
        Serial.println("boot_error=player_or_sd_init_failed");
    }
    testRunner.begin(runtime);

    if (!testRunner.resumedAfterRestart() &&
        runtime.snapshot().state == PlayerState::Empty &&
        runtime.lastPersistenceResult() == PersistenceResult::NotFound) {
        runtime.setPersistenceSuspended(true);
        runtime.replaceQueue(adv_walkman::player::p1AllValidFixtures(), 0,
                             false);
        runtime.setPersistenceSuspended(false);
    }
    printStatus();
    renderPlayer();
    lastStatusAtMs = millis();
}

void loop() {
    M5Cardputer.update();
    serviceTestKey();
    serviceSerial();
    runtime.service();
    testRunner.service();
    renderPlayer();

    const uint32_t now = millis();
    if (now - lastStatusAtMs >= kStatusIntervalMs) {
        lastStatusAtMs = now;
        printStatus();
    }
    delay(1);
}
