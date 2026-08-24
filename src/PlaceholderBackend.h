#pragma once

#include "BenchmarkBackend.h"

namespace adv_walkman {

class PlaceholderBackend final : public BenchmarkBackend {
  public:
    PlaceholderBackend(BackendId id, const char* backendName);

    bool begin(const char* path) override;
    void service() override;
    bool pause() override;
    bool resume() override;
    void stop() override;
    BenchmarkStats stats() const override;
    const char* name() const override;

  private:
    BackendId id_;
    const char* name_;
    BackendState state_ = BackendState::BOOT;
};

}  // namespace adv_walkman
