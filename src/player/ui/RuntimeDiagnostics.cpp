#include "RuntimeDiagnostics.h"
#include <Arduino.h>
#include <esp_system.h>
#include <algorithm>
namespace adv_walkman { namespace player {
namespace {
uint32_t started;RuntimeStage current=RuntimeStage::Idle;
}
RuntimeDiagnostics runtimeDiagnostics;
void RuntimeDiagnostics::begin(){
    resetReason=esp_reset_reason();
    enter(RuntimeStage::Idle);
}
void RuntimeDiagnostics::enter(RuntimeStage stage){
    current=stage;started=micros();
}
void RuntimeDiagnostics::leave(){
    const auto index=static_cast<unsigned>(current);maxima[index]=std::max(maxima[index],uint32_t(micros()-started));
    enter(RuntimeStage::Idle);
}
} }
