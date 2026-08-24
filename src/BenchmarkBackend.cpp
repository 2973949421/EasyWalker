#include "BenchmarkBackend.h"

namespace adv_walkman {

const char* backendStateName(BackendState state) {
    switch (state) {
        case BackendState::BOOT:
            return "BOOT";
        case BackendState::READY:
            return "READY";
        case BackendState::PLAYING:
            return "PLAYING";
        case BackendState::PAUSED:
            return "PAUSED";
        case BackendState::STOPPED:
            return "STOPPED";
        case BackendState::ERROR:
            return "ERROR";
        case BackendState::NOT_IMPLEMENTED:
            return "NOT_IMPLEMENTED";
    }
    return "UNKNOWN";
}

}  // namespace adv_walkman
