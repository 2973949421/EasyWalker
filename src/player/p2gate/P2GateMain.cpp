#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>

#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"
#include "player/p2gate/P2DeviceTestRunner.h"
#include "player/support/AdvStorage.h"

namespace {

using namespace adv_walkman::player;

constexpr const char* kAutomaticRecentSlotA =
    "/ADVWalkman/test/p2-state/recent-auto-a.bin";
constexpr const char* kAutomaticRecentSlotB =
    "/ADVWalkman/test/p2-state/recent-auto-b.bin";

PlayerRuntime player;
LibraryRuntime libraryRuntime;
P2DeviceTestRunner testRunner;
bool previousTestKeyDown = false;
bool bootReady = false;
uint32_t previousPlayerServiceStartedAtUs = 0;

void renderIdle(const char* error = nullptr) {
    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(6, 6);
    display.setTextColor(error == nullptr ? GREEN : RED, BLACK);
    display.setTextSize(1.35f);
    display.println("ADV Walkman P2 Gate");
    display.setTextSize(1.0f);
    display.setTextColor(WHITE, BLACK);
    if (error != nullptr) {
        display.printf("Boot error: %s\n", error);
        display.println("Check SD fixture");
        return;
    }
    display.println("Fixture + audio ready");
    display.println("Press T once to start");
    display.println("Then do not press keys");
    display.println("One gate, four SD logs");
}

void serviceTestKey() {
    const bool down = M5Cardputer.Keyboard.isKeyPressed('t');
    if (bootReady && down && !previousTestKeyDown &&
        !testRunner.ownsDisplay()) {
        const bool started = testRunner.start();
        Serial.printf("p2_gate_start ok=%d\n", started ? 1 : 0);
    }
    previousTestKeyDown = down;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t waitStarted = millis();
    while (!Serial && millis() - waitStarted < 1500) {
        delay(10);
    }

    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(1);

    const bool sdReady = mountAdvSd();
    if (sdReady) {
        SD.mkdir("/ADVWalkman");
        SD.mkdir("/ADVWalkman/test");
        SD.mkdir("/ADVWalkman/test/p2-state");
        SD.remove(kAutomaticRecentSlotA);
        SD.remove(kAutomaticRecentSlotB);
    }
    const bool playerReady = sdReady && player.begin(false);
    if (playerReady) {
        // The Gate must never publish its temporary Queue into the formal P1
        // queue/session slots. This suspension lasts for this test boot only.
        player.setPersistenceSuspended(true);
    }
    const bool libraryReady =
        playerReady && libraryRuntime.begin(SD, player, kAutomaticRecentSlotA,
                                            kAutomaticRecentSlotB);
    bootReady = sdReady && playerReady && libraryReady;
    if (bootReady) {
        testRunner.begin(player, libraryRuntime);
        renderIdle();
    } else {
        renderIdle(!sdReady ? "SD_MOUNT" :
                   !playerReady ? "PLAYER" : "LIBRARY");
    }

    Serial.printf(
        "boot app=adv-walkman-p2-gate version=%s sd=%d player=%d library=%d\n",
        ADV_WALKMAN_VERSION, sdReady ? 1 : 0, playerReady ? 1 : 0,
        libraryReady ? 1 : 0);
    Serial.println(
        "gate key=T fixture=/Music/ADVWalkmanP2Test "
        "state=/ADVWalkman/test/p2-state logs=/ADVWalkman/logs/p2-0x-last.txt");
}

void loop() {
    const uint32_t loopStartedAtUs = micros();
    const uint32_t inputStartedAtUs = micros();
    M5Cardputer.update();
    serviceTestKey();
    const uint32_t inputUpdateUs = micros() - inputStartedAtUs;
    if (bootReady) {
        // The P2 contract requires Player service before every Library step.
        const uint32_t playerStartedAtUs = micros();
        const uint32_t playerServiceStartGapUs =
            previousPlayerServiceStartedAtUs == 0
                ? 0
                : playerStartedAtUs - previousPlayerServiceStartedAtUs;
        previousPlayerServiceStartedAtUs = playerStartedAtUs;
        const bool speakerRequestSlotOccupiedAtServiceStart =
            M5.Speaker.isPlaying(0) != 0;
        player.service();
        const uint32_t playerRuntimeServiceUs = micros() - playerStartedAtUs;
        const uint32_t libraryStartedAtUs = micros();
        libraryRuntime.service();
        const uint32_t libraryRuntimeServiceUs =
            micros() - libraryStartedAtUs;
        const uint32_t preGateLoopBodyUs = micros() - loopStartedAtUs;
        testRunner.recordLoopTimings(
            inputUpdateUs, playerServiceStartGapUs,
            speakerRequestSlotOccupiedAtServiceStart, playerRuntimeServiceUs,
            libraryRuntimeServiceUs, preGateLoopBodyUs);
        const uint32_t gateStartedAtUs = micros();
        testRunner.service();
        const uint32_t gateServiceUs = micros() - gateStartedAtUs;
        const uint32_t loopBodyUs = micros() - loopStartedAtUs;
        testRunner.recordGateServiceTiming(gateServiceUs, loopBodyUs);
    }
    delay(1);
}
