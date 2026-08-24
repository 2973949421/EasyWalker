#include "PlaceholderBackend.h"

namespace adv_walkman {

PlaceholderBackend::PlaceholderBackend(BackendId id, const char* backendName)
    : id_(id), name_(backendName) {}

bool PlaceholderBackend::begin(const char* path) {
    (void)path;
    state_ = BackendState::NOT_IMPLEMENTED;
    return false;
}

void PlaceholderBackend::service() {}

bool PlaceholderBackend::pause() {
    return false;
}

bool PlaceholderBackend::resume() {
    return false;
}

void PlaceholderBackend::stop() {
    state_ = BackendState::NOT_IMPLEMENTED;
}

BenchmarkStats PlaceholderBackend::stats() const {
    return {
        id_,
        state_,
        0,
        0,
        0,
        0,
        -1,
        "unavailable",
        "backend_not_implemented",
    };
}

const char* PlaceholderBackend::name() const {
    return name_;
}

}  // namespace adv_walkman
