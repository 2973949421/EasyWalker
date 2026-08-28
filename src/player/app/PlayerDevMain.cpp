#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <algorithm>

#include "player/app/LibraryRuntime.h"
#include "player/app/PlayerRuntime.h"
#include "player/p3a/P3AGate.h"
#if !defined(P3A_DEVICE_GATE)
#include "player/p3abc/FreeSession.h"
#endif
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
bool ready=false;
bool returnCheckpointRequested=false;

#if defined(P3A_DEVICE_GATE)
P3AGate gate;
#else
FreeSession session;
#endif

void collectInput() {
    const uint32_t started=micros();
    uint64_t previous=input.physicalMask();
    for(unsigned n=0;n<8;++n){
        if(n)M5Cardputer.Keyboard.updateKeyList();
        const auto mask=input.physicalMask();
        const uint32_t now=millis();
        const bool allowed=ui.physicalActivity(mask,now);
        input.capture(mask,now,ui.inputEpoch(),!allowed);
        if(n&&mask==previous)break;
        previous=mask;
        if(micros()-started>=2000)break;
    }
}

void dispatchInput() {
    ui.recordInputQueue(input.overflow(),input.stale());
    UiAction action=UiAction::None;RawKeyEvent raw;
    for(unsigned n=0;n<8&&input.pop(ui.inputEpoch(),action,raw,ui.page()==UiPage::Player);++n){
        ui.recordInputLatency(millis()-raw.capturedAtMs);
#if defined(P3A_DEVICE_GATE)
        if(!gate.beforeAction(action,raw,ui.page()))ui.handleAction(action);
#else
        const auto page=ui.page();
        const bool accepted=action==UiAction::SaveDiagnostics||ui.handleAction(action);
        if(action==UiAction::SaveDiagnostics)player.requestCheckpoint();
        session.action(action,raw,page,accepted);
#endif
    }
}

void renderBootFailure(const char* reason) {
    auto& display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_ORANGE, TFT_BLACK);
    display.setTextSize(1.0f);
    display.setCursor(7, 8);
    display.println("ADV Walkman ERROR");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    UiTextLayout::draw(display, reason == nullptr ? "unknown" : reason,
                       {7, 35, 121, 70, 4, 3, true});
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
        "boot app=adv-walkman-p3d version=%s sd=%d player=%d library=%d ui=%d "
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
#else
    session.begin();
    session.observe(ui,player);
#endif
    ready=true;
}

void loop() {
    if(!ready){delay(10);return;}
    // Audio remains first. Library, keyboard and a bounded UI burst
    // follow; title animation is capped at 20 fps, without a full framebuffer.
    uint32_t workAt=micros();player.serviceAudio();uint32_t audioUs=micros()-workAt;
    M5Cardputer.update();collectInput();dispatchInput();
    workAt=micros();libraryRuntime.service();const uint32_t libraryUs=micros()-workAt;
    // A slow indivisible FS operation must not be followed by another whole
    // input/render burst before the decoder gets its next service.
    if(libraryUs>=6000){workAt=micros();player.serviceAudio();audioUs=std::max<uint32_t>(audioUs,micros()-workAt);}
    workAt=micros();collectInput();dispatchInput();

#if defined(P3A_DEVICE_GATE)
    ui.serviceBackground(true);
#else
    ui.serviceBackground(session.storageIdle());
#endif

#if !defined(P3A_DEVICE_GATE)
    session.recordWork(audioUs,libraryUs,micros()-workAt);
#endif
    if(micros()-workAt>=6000){player.serviceAudio();}

#if defined(P3A_DEVICE_GATE)
    gate.service(ui, player);
    if (gate.finished()) {
        gate.renderResult(M5Cardputer.Display);
        delay(5);
        return;
    }
    ui.setHint(gate.hint());
#endif
    // The audio service can wait for one 1536-sample output slot (~35ms).
    // Do not insert that wait between EVERY 2px/18px stripe. A bounded UI
    // burst advances small I/O jobs or paints a prepared frame, then yields
    // back to audio. The real 70ms PCM / 100ms presentation limits are logged.
    const uint32_t burstAt=micros();
    for(unsigned work=0;work<64;++work){
        // Storage owns its own turn; never follows a UI operation in the
        // same step. The next check observes the real indivisible I/O time.
        static unsigned backgroundTurn=0;
        if(++backgroundTurn%8==0 && !ui.nowPlaying().presentingLyrics() && !ui.settingsBusy())player.servicePersistence();
        else ui.service();
        if(micros()-burstAt>=6000)break;
        collectInput();dispatchInput();
#if !defined(P3A_DEVICE_GATE)
        session.observe(ui,player);
#endif
        if(micros()-burstAt>=6000)break;
    }
#if !defined(P3A_DEVICE_GATE)
    const uint32_t burstUs=micros()-burstAt;
    // Logging gets a fresh audio service boundary, not the tail of a render
    // burst. Actual logging and resource load remain inside PCM measurement.
    if(session.workDue() && !ui.nowPlaying().presentingLyrics() && !ui.settingsBusy()){
        workAt=micros();player.serviceAudio();session.recordWork(micros()-workAt,0,0);
        session.service(ui,player,burstUs);
    }else session.recordBurst(burstUs);
    if(ui.readyToReturn()){
        if(!returnCheckpointRequested){session.requestManualSave();returnCheckpointRequested=true;}
        else if(session.storageIdle()&&!session.manualSavePending())ui.finishLauncherReturn(session.lastSaveOk());
    }else returnCheckpointRequested=false;
#endif
    delay(1);
}
