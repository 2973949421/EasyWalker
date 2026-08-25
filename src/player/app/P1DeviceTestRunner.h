#pragma once

#include "player/app/PlayerRuntime.h"

namespace adv_walkman {
namespace player {

class P1DeviceTestRunner final {
  public:
    void begin(PlayerRuntime& runtime);
    bool start();
    void service();

    bool active() const;
    bool ownsDisplay() const;
    bool resumedAfterRestart() const;
    const char* phaseName() const;

  private:
    enum class Phase : uint8_t {
        Idle,
        GateALegal,
        GateACorrupt,
        GateAMissing,
        GateATransportWarm,
        GateAPauseHold,
        GateAResume,
        GateAVbrSeekRunning,
        GateAFinalMix,
        GateAReplayRunning,
        GateASeekVerify,
        GateBRepeatOne,
        GateBRepeatAll,
        GateBRepeatOff,
        GateBPersist,
        GateBResumeSilence,
        Passed,
        Failed,
    };

    void startGateA();
    void startGateB();
    void serviceGateA(const PlayerSnapshot& snapshot, uint32_t now);
    void serviceGateB(const PlayerSnapshot& snapshot, uint32_t now);
    bool startLegalFixture(size_t index);
    bool runQueueModelChecks();
    bool runControllerModeChecks();
    bool verifyAllFixtureHashes();
    bool verifyFileSha256(const char* path, const char* expected,
                          size_t fixtureNumber);
    void enter(Phase phase);
    void fail(const char* reason);
    void passGateA();
    void passGateB();
    void render(bool force = false);
    bool writeGateLog(const char* path, const char* status,
                      const char* detail);
    bool hasRestartMarker() const;
    void updateDiagnostics(const PlayerSnapshot& snapshot);

    PlayerRuntime* runtime_ = nullptr;
    Phase phase_ = Phase::Idle;
    uint32_t phaseStartedAtMs_ = 0;
    uint32_t runStartedAtMs_ = 0;
    uint32_t initialHeap_ = 0;
    uint32_t minimumHeap_ = 0;
    uint32_t maximumBackpressure_ = 0;
    uint32_t maximumServiceUs_ = 0;
    uint32_t maximumSeekErrorMs_ = 0;
    uint32_t expectedTrackEnded_ = 0;
    uint32_t heldPositionMs_ = 0;
    size_t fixtureIndex_ = 0;
    bool observedExpectedRate_ = false;
    bool restartResume_ = false;
    bool p101Passed_ = false;
    bool failureSnapshotValid_ = false;
    bool fixtureHashesVerified_ = false;
    size_t fixtureHashFailureIndex_ = 0;
    char fixtureHashObserved_[65] = "none";
    char failure_[96] = "none";
    PlayerSnapshot failureSnapshot_{};
};

}  // namespace player
}  // namespace adv_walkman
