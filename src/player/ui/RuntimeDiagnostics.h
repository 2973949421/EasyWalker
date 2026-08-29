#pragma once
#include <cstdint>
namespace adv_walkman { namespace player {
enum class RuntimeStage:uint8_t{Idle,Audio,Keyboard,Directory,Settings,Persistence,Ui,Log,Count};
struct RuntimeDiagnostics{
    uint32_t resetReason=0;
    uint32_t maxima[static_cast<unsigned>(RuntimeStage::Count)]{};
    void begin();void enter(RuntimeStage stage);void leave();
};
extern RuntimeDiagnostics runtimeDiagnostics;
} }
