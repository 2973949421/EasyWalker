#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>

#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"
#include "player/p3a/P3AGate.h"
#include "player/support/AdvStorage.h"
#include "player/ui/InputRouter.h"
#include "player/ui/UiCoordinator.h"

namespace {

using namespace adv_walkman::player;

// Cardputer ADV portrait baseline: headphone jack at the top. Rotation 0 has
// the correct geometry but is physically upside down in that posture.
constexpr uint8_t kPortraitRotation = 2;

PlayerRuntime player;
LibraryRuntime libraryRuntime;
UiCoordinator ui;
InputRouter input;

#if defined(P3A_DEVICE_GATE)
P3AGate gate;
#endif

void renderBootFailure(const char* reason) {
    auto& display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.setTextSize(1.0f);
    display.setCursor(7, 8);
    display.println("ADV Walkman P3A ERROR");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(7, 35);
    display.printf("%.30s", reason == nullptr ? "unknown" : reason);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    const uint32_t waitStarted = millis();
    while (!Serial && millis() - waitStarted < 1000) {
        delay(10);
    }

    auto config = M5.config();
    M5Cardputer.begin(config, true);
    M5Cardputer.Display.setRotation(kPortraitRotation);

    if (M5Cardputer.Display.width() != 135 ||
        M5Cardputer.Display.height() != 240) {
        renderBootFailure("Portrait geometry is not 135x240");
        return;
    }

    const bool sdReady = mountAdvSd();
    const bool playerReady = sdReady && player.begin(true);
    const bool libraryReady = playerReady && libraryRuntime.begin(SD, player);
    const bool uiReady = libraryReady &&
                         ui.begin(M5Cardputer.Display, player, libraryRuntime);
    Serial.printf(
        "boot app=adv-walkman-p3a version=%s sd=%d player=%d library=%d ui=%d "
        "display=%dx%d rotation=%u\n",
        ADV_WALKMAN_VERSION, sdReady, playerReady, libraryReady, uiReady,
        M5Cardputer.Display.width(), M5Cardputer.Display.height(),
        static_cast<unsigned>(M5Cardputer.Display.getRotation()));
    if (!uiReady) {
        renderBootFailure(!sdReady ? "microSD mount failed"
                                   : (!playerReady ? "Player init failed"
                                                   : "Library/UI init failed"));
        return;
    }

#if defined(P3A_DEVICE_GATE)
    // Production restores to Player/Paused. The dedicated Gate navigates to
    // Library so its prompt sequence is deterministic without erasing state.
    ui.showLibrary();
    gate.begin(M5Cardputer.Display.width(), M5Cardputer.Display.height(),
               M5Cardputer.Display.getRotation());
    ui.setHint(gate.hint());
#endif
}

void loop() {
    // Audio remains first. Library, keyboard and one bounded dirty render
    // follow; no full-screen framebuffer or continuous animation is used.
    player.service();
    libraryRuntime.service();
    M5Cardputer.update();

    UiAction action = UiAction::None;
    RawKeyEvent raw;
    if (input.poll(action, raw)) {
#if defined(P3A_DEVICE_GATE)
        const bool consumed = gate.beforeAction(action, raw, ui.page());
        if (!consumed) {
            ui.handleAction(action);
        }
#else
        ui.handleAction(action);
#endif
        Serial.printf("input page=%s action=%s x=%d y=%d fn=%d count=%u\n",
                      uiPageName(ui.page()), uiActionName(action), raw.x, raw.y,
                      raw.fn ? 1 : 0, static_cast<unsigned>(raw.keyCount));
    }

#if defined(P3A_DEVICE_GATE)
    gate.service(ui, player);
    if (gate.finished()) {
        gate.renderResult(M5Cardputer.Display);
        delay(5);
        return;
    }
    ui.setHint(gate.hint());
#endif
    ui.service();
    delay(1);
}
