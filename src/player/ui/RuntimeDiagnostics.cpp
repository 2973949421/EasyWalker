#include "RuntimeDiagnostics.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <algorithm>
namespace adv_walkman { namespace player {
namespace {
struct Breadcrumb{uint32_t magic,phase,ms,check;};
RTC_NOINIT_ATTR Breadcrumb breadcrumb;
static_assert(sizeof(Breadcrumb)<=64,"bounded RTC breadcrumb");
uint32_t started;RuntimeStage current=RuntimeStage::Idle;
constexpr uint32_t magic=0xAD083D1A;
}
RuntimeDiagnostics runtimeDiagnostics;
void RuntimeDiagnostics::begin(){
    resetReason=esp_reset_reason();
    if(resetReason!=ESP_RST_POWERON&&resetReason!=ESP_RST_BROWNOUT&&
       breadcrumb.magic==magic&&breadcrumb.check==(magic^breadcrumb.phase^breadcrumb.ms)&&
       breadcrumb.phase<static_cast<unsigned>(RuntimeStage::Count)){
        previousPhase=breadcrumb.phase;previousMs=breadcrumb.ms;previousValid=true;
    }
    enter(RuntimeStage::Idle);
}
void RuntimeDiagnostics::enter(RuntimeStage stage){
    current=stage;started=micros();const uint32_t ms=millis();
    breadcrumb.magic=magic;breadcrumb.phase=static_cast<unsigned>(stage);breadcrumb.ms=ms;
    breadcrumb.check=magic^breadcrumb.phase^ms;
}
void RuntimeDiagnostics::leave(){
    const auto index=static_cast<unsigned>(current);maxima[index]=std::max(maxima[index],uint32_t(micros()-started));
    enter(RuntimeStage::Idle);
}
} }
