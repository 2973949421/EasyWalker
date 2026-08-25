#include "player/app/P1DeviceTestRunner.h"

#include <M5Cardputer.h>
#include <SD.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mbedtls/sha256.h>

#include "player/core/PlaybackQueue.h"
#include "player/support/FixedTrackSource.h"
#include "player/support/P1Fixtures.h"

#ifndef P1_DEVICE_GATE
#define P1_DEVICE_GATE 1
#endif

#if P1_DEVICE_GATE != 1 && P1_DEVICE_GATE != 2
#error "P1_DEVICE_GATE must be 1 or 2"
#endif

namespace adv_walkman {
namespace player {
namespace {

constexpr char kP101Log[] = "/ADVWalkman/logs/p1-01-last.txt";
constexpr char kP102Log[] = "/ADVWalkman/logs/p1-02-last.txt";
constexpr char kP103Log[] = "/ADVWalkman/logs/p1-03-last.txt";
constexpr char kP104Log[] = "/ADVWalkman/logs/p1-04-last.txt";
constexpr uint32_t kFixtureTimeoutMs = 20000;
constexpr uint32_t kOperationTimeoutMs = 5000;

uint32_t difference(uint32_t left, uint32_t right) {
    return left > right ? left - right : right - left;
}

}  // namespace

void P1DeviceTestRunner::begin(PlayerRuntime& runtime) {
    runtime_ = &runtime;
    SD.mkdir("/ADVWalkman");
    SD.mkdir("/ADVWalkman/logs");
#if P1_DEVICE_GATE == 2
    if (hasRestartMarker()) {
        restartResume_ = true;
        // The restart marker is written only after Gate B verified the fixed
        // Fixture set and committed the paired state.
        fixtureHashesVerified_ = true;
        std::strcpy(fixtureHashObserved_, "pre_restart_all_match");
        initialHeap_ = ESP.getFreeHeap();
        minimumHeap_ = initialHeap_;
        enter(Phase::GateBResumeSilence);
        runStartedAtMs_ = millis();
    }
#endif
}

bool P1DeviceTestRunner::start() {
    if (runtime_ == nullptr || phase_ != Phase::Idle) {
        return false;
    }
#if P1_DEVICE_GATE == 2
    startGateB();
#else
    startGateA();
#endif
    return phase_ != Phase::Failed;
}

void P1DeviceTestRunner::service() {
    if (runtime_ == nullptr || phase_ == Phase::Idle ||
        phase_ == Phase::Passed || phase_ == Phase::Failed) {
        render();
        return;
    }

    const uint32_t now = millis();
    const PlayerSnapshot snapshot = runtime_->snapshot();
    updateDiagnostics(snapshot);
#if P1_DEVICE_GATE == 2
    serviceGateB(snapshot, now);
#else
    serviceGateA(snapshot, now);
#endif
    render();
}

bool P1DeviceTestRunner::active() const {
    return phase_ != Phase::Idle && phase_ != Phase::Passed &&
           phase_ != Phase::Failed;
}

bool P1DeviceTestRunner::ownsDisplay() const {
    return phase_ != Phase::Idle;
}

bool P1DeviceTestRunner::resumedAfterRestart() const {
    return restartResume_;
}

const char* P1DeviceTestRunner::phaseName() const {
    switch (phase_) {
        case Phase::Idle: return "IDLE";
        case Phase::GateALegal: return "P1-01 FORMATS";
        case Phase::GateACorrupt: return "P1-01 CORRUPT";
        case Phase::GateAMissing: return "P1-01 MISSING";
        case Phase::GateATransportWarm: return "P1-02 WARM";
        case Phase::GateAPauseHold: return "P1-02 PAUSE/SEEK";
        case Phase::GateAResume: return "P1-02 NEXT/PREV";
        case Phase::GateAVbrSeekRunning: return "P1-02 VBR SEEK";
        case Phase::GateAFinalMix: return "P1-02 MIXED";
        case Phase::GateAReplayRunning: return "P1-02 REPLAY";
        case Phase::GateASeekVerify: return "P1-02 SEEK VERIFY";
        case Phase::GateBRepeatOne: return "P1-03 REPEAT ONE";
        case Phase::GateBRepeatAll: return "P1-03 REPEAT ALL";
        case Phase::GateBRepeatOff: return "P1-03 SEQUENTIAL";
        case Phase::GateBPersist: return "P1-04 SAVE";
        case Phase::GateBResumeSilence: return "P1-04 RESTORE";
        case Phase::Passed: return "PASS";
        case Phase::Failed: return "FAIL";
    }
    return "UNKNOWN";
}

void P1DeviceTestRunner::startGateA() {
    runtime_->setPersistenceSuspended(true);
    runtime_->setShuffleEnabled(false);
    runtime_->setRepeatMode(RepeatMode::Off);
    runtime_->controller().resetDiagnostics();
    initialHeap_ = ESP.getFreeHeap();
    minimumHeap_ = initialHeap_;
    runStartedAtMs_ = millis();
    fixtureIndex_ = 0;
    std::strcpy(failure_, "none");
    if (!verifyAllFixtureHashes()) {
        fail("fixture_sha256_mismatch");
        return;
    }
    if (!writeGateLog(kP101Log, "RUNNING", "phase=formats") ||
        !writeGateLog(kP102Log, "RUNNING", "phase=waiting_for_p1_01")) {
        fail("running_log_write_failed");
        return;
    }
    if (!startLegalFixture(0)) {
        fail("cbr44_open_failed");
    }
}

void P1DeviceTestRunner::startGateB() {
    runtime_->setPersistenceSuspended(true);
    runtime_->setShuffleEnabled(false);
    runtime_->setRepeatMode(RepeatMode::Off);
    runtime_->controller().resetDiagnostics();
    initialHeap_ = ESP.getFreeHeap();
    minimumHeap_ = initialHeap_;
    runStartedAtMs_ = millis();
    std::strcpy(failure_, "none");
    if (!verifyAllFixtureHashes()) {
        fail("fixture_sha256_mismatch");
        return;
    }
    if (!writeGateLog(kP103Log, "RUNNING", "phase=queue_models") ||
        !writeGateLog(kP104Log, "RUNNING", "phase=waiting_for_p1_03")) {
        fail("running_log_write_failed");
        return;
    }
    if (!runQueueModelChecks()) {
        fail("queue_model_check_failed");
        return;
    }
    if (!runControllerModeChecks()) {
        fail("controller_mode_check_failed");
        return;
    }

    runtime_->setRepeatMode(RepeatMode::One);
    expectedTrackEnded_ = runtime_->snapshot().trackEndedEvents + 1;
    if (!runtime_->replaceQueue(p1SingleValidFixture(0), 0, true) ||
        !runtime_->seekToMs(11000)) {
        fail("repeat_one_start_failed");
        return;
    }
    enter(Phase::GateBRepeatOne);
}

void P1DeviceTestRunner::serviceGateA(const PlayerSnapshot& snapshot,
                                      uint32_t now) {
    switch (phase_) {
        case Phase::GateALegal:
            if (snapshot.state == PlayerState::Error) {
                fail("legal_fixture_error");
                return;
            }
            if (snapshot.sampleRateHz != 0) {
                if (snapshot.sampleRateHz != p1FixtureSampleRate(fixtureIndex_)) {
                    fail("sample_rate_mismatch");
                    return;
                }
                observedExpectedRate_ = true;
            }
            if (snapshot.state == PlayerState::Stopped) {
                if (!observedExpectedRate_ ||
                    snapshot.trackEndedEvents != expectedTrackEnded_) {
                    fail("eof_event_mismatch");
                    return;
                }
                ++fixtureIndex_;
                if (fixtureIndex_ < kP1ValidFixtureCount) {
                    if (!startLegalFixture(fixtureIndex_)) {
                        fail("next_fixture_open_failed");
                    }
                } else {
                    expectedTrackEnded_ = snapshot.trackEndedEvents;
                    runtime_->replaceQueue(p1CorruptFixture(), 0, true);
                    enter(Phase::GateACorrupt);
                }
                return;
            }
            if (now - phaseStartedAtMs_ > kFixtureTimeoutMs) {
                fail("fixture_timeout");
            }
            return;

        case Phase::GateACorrupt:
            if (snapshot.state == PlayerState::Stopped ||
                snapshot.trackEndedEvents != expectedTrackEnded_) {
                fail("corrupt_reported_track_end");
                return;
            }
            if (snapshot.state == PlayerState::Error) {
                runtime_->replaceQueue(p1MissingFixture(), 0, true);
                enter(Phase::GateAMissing);
                return;
            }
            if (now - phaseStartedAtMs_ > kOperationTimeoutMs) {
                fail("corrupt_timeout");
            }
            return;

        case Phase::GateAMissing:
            if (snapshot.trackEndedEvents != expectedTrackEnded_) {
                fail("missing_reported_track_end");
                return;
            }
            if (snapshot.state == PlayerState::Error) {
                p101Passed_ = true;
                if (!writeGateLog(
                        kP101Log, "PASS",
                        "formats=3 eof_once=1 corrupt_error=1 missing_error=1 original_320kbps=manual_pending")) {
                    fail("p101_log_write_failed");
                    return;
                }
                if (!runtime_->replaceQueue(p1AllValidFixtures(), 0, true)) {
                    fail("transport_queue_failed");
                    return;
                }
                enter(Phase::GateATransportWarm);
                return;
            }
            if (now - phaseStartedAtMs_ > 1000) {
                fail("missing_not_error");
            }
            return;

        case Phase::GateATransportWarm:
            if (snapshot.state == PlayerState::Error) {
                fail("transport_start_error");
                return;
            }
            if (snapshot.state == PlayerState::Playing &&
                now - phaseStartedAtMs_ >= 1000) {
                if (!runtime_->pause()) {
                    fail("pause_failed");
                    return;
                }
                heldPositionMs_ = runtime_->snapshot().positionMs;
                enter(Phase::GateAPauseHold);
            } else if (now - phaseStartedAtMs_ > kOperationTimeoutMs) {
                fail("transport_start_timeout");
            }
            return;

        case Phase::GateAPauseHold:
            if (snapshot.state != PlayerState::Paused) {
                fail("pause_state_lost");
                return;
            }
            if (now - phaseStartedAtMs_ >= 500) {
                if (difference(snapshot.positionMs, heldPositionMs_) > 100) {
                    fail("pause_position_advanced");
                    return;
                }
                if (!runtime_->seekToMs(6000)) {
                    fail("cbr_seek_failed");
                    return;
                }
                const PlayerSnapshot sought = runtime_->snapshot();
                maximumSeekErrorMs_ = std::max(
                    maximumSeekErrorMs_, difference(sought.positionMs, 6000));
                if (sought.state != PlayerState::Paused ||
                    maximumSeekErrorMs_ > 1000 || !runtime_->resume()) {
                    fail("cbr_seek_accuracy_failed");
                    return;
                }
                enter(Phase::GateAResume);
            }
            return;

        case Phase::GateAResume:
            if (snapshot.state == PlayerState::Error) {
                fail("resume_error");
                return;
            }
            if (now - phaseStartedAtMs_ >= 500) {
                if (!runtime_->pause() || !runtime_->next()) {
                    fail("paused_next_failed");
                    return;
                }
                PlayerSnapshot moved = runtime_->snapshot();
                if (moved.state != PlayerState::Paused || moved.currentIndex != 1 ||
                    !runtime_->seekToMs(6000)) {
                    fail("paused_next_state_failed");
                    return;
                }
                moved = runtime_->snapshot();
                maximumSeekErrorMs_ = std::max(
                    maximumSeekErrorMs_, difference(moved.positionMs, 6000));
                if (maximumSeekErrorMs_ > 1000 || !runtime_->resume()) {
                    fail("vbr_seek_or_resume_failed");
                    return;
                }
                enter(Phase::GateAVbrSeekRunning);
            }
            return;

        case Phase::GateAVbrSeekRunning:
            if (snapshot.state == PlayerState::Error) {
                fail("vbr_seek_decode_error");
                return;
            }
            if (now - phaseStartedAtMs_ >= 700) {
                if (snapshot.state != PlayerState::Playing ||
                    snapshot.sampleRateHz != 44100 ||
                    snapshot.positionMs < 6000 || snapshot.positionMs > 8500 ||
                    !runtime_->pause() || !runtime_->previous()) {
                    fail("vbr_seek_not_serviced");
                    return;
                }
                PlayerSnapshot moved = runtime_->snapshot();
                if (moved.currentIndex != 1 || moved.positionMs > 1000 ||
                    moved.state != PlayerState::Paused || !runtime_->previous()) {
                    fail("previous_5s_rule_failed");
                    return;
                }
                moved = runtime_->snapshot();
                if (moved.currentIndex != 0 || moved.state != PlayerState::Paused ||
                    !runtime_->next() || !runtime_->resume()) {
                    fail("previous_history_failed");
                    return;
                }
                enter(Phase::GateAFinalMix);
            }
            return;

        case Phase::GateAFinalMix:
            if (snapshot.state == PlayerState::Error) {
                fail("mixed_operation_error");
                return;
            }
            if (now - phaseStartedAtMs_ >= 500) {
                runtime_->stop();
                PlayerSnapshot stopped = runtime_->snapshot();
                if (stopped.state != PlayerState::Stopped ||
                    stopped.positionMs != 0 || stopped.currentIndex != 1 ||
                    !runtime_->play()) {
                    fail("stop_replay_failed");
                    return;
                }
                enter(Phase::GateAReplayRunning);
            }
            return;

        case Phase::GateAReplayRunning:
            if (snapshot.state == PlayerState::Error) {
                fail("replay_error");
                return;
            }
            if (now - phaseStartedAtMs_ >= 500) {
                if (snapshot.state != PlayerState::Playing ||
                    snapshot.positionMs == 0 || !runtime_->pause() ||
                    !runtime_->resume() || !runtime_->seekToMs(3000)) {
                    fail("replay_not_serviced");
                    return;
                }
                enter(Phase::GateASeekVerify);
            }
            return;

        case Phase::GateASeekVerify:
            if (snapshot.state == PlayerState::Error) {
                fail("mixed_seek_error");
                return;
            }
            if (now - phaseStartedAtMs_ >= 300) {
                if (snapshot.state != PlayerState::Playing ||
                    snapshot.positionMs < 3000 || snapshot.positionMs > 4500) {
                    fail("mixed_seek_not_serviced");
                    return;
                }
                runtime_->stop();
                passGateA();
            }
            return;

        default:
            return;
    }
}

void P1DeviceTestRunner::serviceGateB(const PlayerSnapshot& snapshot,
                                      uint32_t now) {
    switch (phase_) {
        case Phase::GateBRepeatOne:
            if (snapshot.state == PlayerState::Error) {
                fail("repeat_one_error");
                return;
            }
            if (snapshot.trackEndedEvents >= expectedTrackEnded_) {
                if (snapshot.state != PlayerState::Playing ||
                    snapshot.currentIndex != 0) {
                    fail("repeat_one_did_not_repeat");
                    return;
                }
                runtime_->setRepeatMode(RepeatMode::All);
                expectedTrackEnded_ = snapshot.trackEndedEvents + 1;
                if (!runtime_->replaceQueue(p1AllValidFixtures(), 2, true) ||
                    !runtime_->seekToMs(11000)) {
                    fail("repeat_all_start_failed");
                    return;
                }
                runtime_->setShuffleEnabled(true);
                enter(Phase::GateBRepeatAll);
            } else if (now - phaseStartedAtMs_ > kOperationTimeoutMs) {
                fail("repeat_one_timeout");
            }
            return;

        case Phase::GateBRepeatAll:
            if (snapshot.state == PlayerState::Error) {
                fail("repeat_all_error");
                return;
            }
            if (snapshot.trackEndedEvents >= expectedTrackEnded_) {
                if (snapshot.state != PlayerState::Playing ||
                    snapshot.currentIndex == 2) {
                    fail("repeat_all_did_not_wrap");
                    return;
                }
                runtime_->setShuffleEnabled(false);
                runtime_->setRepeatMode(RepeatMode::Off);
                expectedTrackEnded_ = snapshot.trackEndedEvents + 1;
                if (!runtime_->replaceQueue(p1AllValidFixtures(), 2, true) ||
                    !runtime_->seekToMs(11000)) {
                    fail("repeat_off_start_failed");
                    return;
                }
                enter(Phase::GateBRepeatOff);
            } else if (now - phaseStartedAtMs_ > kOperationTimeoutMs) {
                fail("repeat_all_timeout");
            }
            return;

        case Phase::GateBRepeatOff:
            if (snapshot.state == PlayerState::Error) {
                fail("repeat_off_error");
                return;
            }
            if (snapshot.trackEndedEvents >= expectedTrackEnded_) {
                if (snapshot.state != PlayerState::Stopped) {
                    fail("repeat_off_did_not_stop");
                    return;
                }
                if (!writeGateLog(
                        kP103Log, "PASS",
                        "sequential=1 shuffle_round=1 repeat_one=1 repeat_all=1 history=1 repeat_one_manual_navigation=1 mode_switch_keeps_track=1")) {
                    fail("p103_log_write_failed");
                    return;
                }
                runtime_->setPersistenceSuspended(false);
                if (!runtime_->replaceQueue(p1AllValidFixtures(), 0, true) ||
                    !runtime_->pause() || !runtime_->next() ||
                    !runtime_->seekToMs(4000)) {
                    fail("persist_state_prepare_failed");
                    return;
                }
                runtime_->setRepeatMode(RepeatMode::All);
                runtime_->setShuffleEnabled(true);
                runtime_->requestCheckpoint(true);
                enter(Phase::GateBPersist);
            } else if (now - phaseStartedAtMs_ > kOperationTimeoutMs) {
                fail("repeat_off_timeout");
            }
            return;

        case Phase::GateBPersist:
            if (runtime_->persistenceIdle()) {
                if (runtime_->lastPersistenceResult() != PersistenceResult::Ok) {
                    fail("state_write_failed");
                    return;
                }
                if (!writeGateLog(
                        kP104Log, "RUNNING",
                        "phase=RESTART_PENDING expected_track=2 expected_position_ms=4000 state_roundtrip=started")) {
                    fail("restart_marker_write_failed");
                    return;
                }
                delay(100);
                ESP.restart();
                return;
            }
            if (now - phaseStartedAtMs_ > 15000) {
                fail("state_write_timeout");
            }
            return;

        case Phase::GateBResumeSilence:
            if (now - phaseStartedAtMs_ >= 3000) {
                const bool stateOk = snapshot.state == PlayerState::Paused &&
                                     snapshot.queueCount == 3 &&
                                     snapshot.currentIndex == 1 &&
                                     snapshot.repeatMode == RepeatMode::All &&
                                     snapshot.shuffleEnabled;
                const bool positionOk =
                    difference(snapshot.positionMs, 4000) <= 1000;
                char currentPath[kTrackPathCapacity]{};
                bool pathsOk = runtime_->controller().currentPath(
                    currentPath, sizeof(currentPath));
                pathsOk = pathsOk &&
                          std::strcmp(currentPath, p1FixturePath(1)) == 0;
                char queuedPath[kTrackPathCapacity]{};
                for (size_t index = 0;
                     pathsOk && index < kP1ValidFixtureCount; ++index) {
                    pathsOk = runtime_->controller().queue().pathAtSourceIndex(
                                  index, queuedPath, sizeof(queuedPath)) &&
                              std::strcmp(queuedPath, p1FixturePath(index)) == 0;
                }
                const PlaybackQueueSnapshotView queue =
                    runtime_->controller().queueSnapshotView();
                const bool queueStateOk =
                    queue.shuffleEnabled && queue.orderCount == 3 &&
                    queue.orderCursor == 1 && queue.historyCount == 1 &&
                    queue.order != nullptr && queue.history != nullptr &&
                    queue.order[0] == 0 && queue.order[1] == 1 &&
                    queue.order[2] == 2 && queue.history[0] == 0;
                if (!stateOk || !positionOk || !pathsOk || !queueStateOk ||
                    M5.Speaker.isPlaying()) {
                    fail("restore_or_silence_failed");
                    return;
                }
                passGateB();
            }
            return;

        default:
            return;
    }
}

bool P1DeviceTestRunner::startLegalFixture(size_t index) {
    fixtureIndex_ = index;
    observedExpectedRate_ = false;
    expectedTrackEnded_ = runtime_->snapshot().trackEndedEvents + 1;
    if (!runtime_->replaceQueue(p1SingleValidFixture(index), 0, true)) {
        return false;
    }
    enter(Phase::GateALegal);
    return true;
}

bool P1DeviceTestRunner::runQueueModelChecks() {
    PlaybackQueue queue(0x12345678u);
    if (!queue.reset(p1AllValidFixtures(), 0) || queue.currentSourceIndex() != 0 ||
        !queue.advance(false) || queue.currentSourceIndex() != 1 ||
        !queue.advance(false) || queue.currentSourceIndex() != 2 ||
        queue.advance(false) || !queue.advance(true) ||
        queue.currentSourceIndex() != 0) {
        return false;
    }

    queue.setRandomSeed(0xA5A5A5A5u);
    queue.setShuffleEnabled(true);
    bool seen[kP1ValidFixtureCount] = {};
    seen[queue.currentSourceIndex()] = true;
    for (size_t index = 1; index < kP1ValidFixtureCount; ++index) {
        if (!queue.advance(false) || queue.currentSourceIndex() >= kP1ValidFixtureCount ||
            seen[queue.currentSourceIndex()]) {
            return false;
        }
        seen[queue.currentSourceIndex()] = true;
    }
    if (queue.advance(false) || !queue.previous()) {
        return false;
    }

    const char* const noPaths[] = {nullptr};
    FixedTrackSource empty(noPaths, 0);
    if (!queue.reset(empty, 0) || !queue.empty() ||
        !queue.reset(p1SingleValidFixture(0), 0) ||
        queue.advance(false) || queue.previous()) {
        return false;
    }
    return true;
}

bool P1DeviceTestRunner::runControllerModeChecks() {
    runtime_->setShuffleEnabled(false);
    runtime_->setRepeatMode(RepeatMode::Off);
    if (!runtime_->replaceQueue(p1AllValidFixtures(), 1, true) ||
        !runtime_->pause()) {
        return false;
    }
    PlayerSnapshot snapshot = runtime_->snapshot();
    const uint32_t position = snapshot.positionMs;
    runtime_->setRepeatMode(RepeatMode::One);
    if (!runtime_->next()) {
        return false;
    }
    snapshot = runtime_->snapshot();
    if (snapshot.state != PlayerState::Paused || snapshot.currentIndex != 2 ||
        !runtime_->previous()) {
        return false;
    }
    snapshot = runtime_->snapshot();
    if (snapshot.state != PlayerState::Paused || snapshot.currentIndex != 1) {
        return false;
    }
    runtime_->setShuffleEnabled(true);
    runtime_->setRepeatMode(RepeatMode::All);
    snapshot = runtime_->snapshot();
    if (snapshot.state != PlayerState::Paused || snapshot.currentIndex != 1 ||
        difference(snapshot.positionMs, position) > 100) {
        return false;
    }
    runtime_->setShuffleEnabled(false);
    runtime_->stop();
    return true;
}

bool P1DeviceTestRunner::verifyAllFixtureHashes() {
    fixtureHashesVerified_ = false;
    fixtureHashFailureIndex_ = 0;
    std::strcpy(fixtureHashObserved_, "none");
    for (size_t index = 0; index < kP1ValidFixtureCount; ++index) {
        if (!verifyFileSha256(p1FixturePath(index), p1FixtureSha256(index),
                              index + 1)) {
            return false;
        }
    }
    if (!verifyFileSha256(p1CorruptFixturePath(), p1CorruptFixtureSha256(),
                          kP1ValidFixtureCount + 1)) {
        return false;
    }
    fixtureHashesVerified_ = true;
    std::strcpy(fixtureHashObserved_, "all_match");
    return true;
}

bool P1DeviceTestRunner::verifyFileSha256(const char* path,
                                          const char* expected,
                                          size_t fixtureNumber) {
    File file = SD.open(path, FILE_READ);
    if (!file) {
        fixtureHashFailureIndex_ = fixtureNumber;
        std::strcpy(fixtureHashObserved_, "missing");
        return false;
    }

    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    bool ok = mbedtls_sha256_starts_ret(&context, 0) == 0;
    uint8_t buffer[1024];
    while (ok && file.available()) {
        const size_t received = file.read(buffer, sizeof(buffer));
        if (received == 0) {
            ok = false;
            break;
        }
        ok = mbedtls_sha256_update_ret(&context, buffer, received) == 0;
        yield();
    }
    file.close();

    uint8_t digest[32]{};
    if (ok) {
        ok = mbedtls_sha256_finish_ret(&context, digest) == 0;
    }
    mbedtls_sha256_free(&context);
    if (!ok) {
        fixtureHashFailureIndex_ = fixtureNumber;
        std::strcpy(fixtureHashObserved_, "read_error");
        return false;
    }

    static const char kHex[] = "0123456789abcdef";
    for (size_t index = 0; index < sizeof(digest); ++index) {
        fixtureHashObserved_[index * 2] = kHex[digest[index] >> 4];
        fixtureHashObserved_[index * 2 + 1] = kHex[digest[index] & 0x0F];
    }
    fixtureHashObserved_[64] = '\0';
    if (std::strcmp(fixtureHashObserved_, expected) != 0) {
        fixtureHashFailureIndex_ = fixtureNumber;
        return false;
    }
    return true;
}

void P1DeviceTestRunner::enter(Phase phase) {
    phase_ = phase;
    phaseStartedAtMs_ = millis();
    render(true);
}

void P1DeviceTestRunner::fail(const char* reason) {
    std::strncpy(failure_, reason == nullptr ? "unknown" : reason,
                 sizeof(failure_) - 1);
    failure_[sizeof(failure_) - 1] = '\0';
    runtime_->stop();
#if P1_DEVICE_GATE == 2
    writeGateLog(phase_ == Phase::GateBResumeSilence ? kP104Log : kP103Log,
                 "FAIL", failure_);
#else
    writeGateLog(p101Passed_ ? kP102Log : kP101Log, "FAIL", failure_);
#endif
    enter(Phase::Failed);
}

void P1DeviceTestRunner::passGateA() {
    if (!writeGateLog(
            kP102Log, "PASS",
            "pause_resume=1 seek_cbr_vbr=1 next_prev=1 stop_replay=1 serviced_between_operations=1")) {
        fail("p102_log_write_failed");
        return;
    }
    enter(Phase::Passed);
}

void P1DeviceTestRunner::passGateB() {
    if (!writeGateLog(kP104Log, "PASS",
                      "state_roundtrip=1 restored_queue=1 restored_paused=1 silent_3s=1")) {
        fail("p104_log_write_failed");
        return;
    }
    enter(Phase::Passed);
}

void P1DeviceTestRunner::render(bool force) {
    const uint32_t now = millis();
    if (!force && now - lastRenderAtMs_ < 250) {
        return;
    }
    lastRenderAtMs_ = now;
    auto& display = M5Cardputer.Display;
    display.fillScreen(BLACK);
    display.setCursor(6, 6);
    display.setTextColor(phase_ == Phase::Failed ? RED : YELLOW, BLACK);
    display.setTextSize(1.4f);
    display.printf("P1 GATE %d %s\n", P1_DEVICE_GATE,
                   phase_ == Phase::Passed ? "PASS" :
                   phase_ == Phase::Failed ? "FAIL" : "RUNNING");
    display.setTextSize(1.0f);
    display.setTextColor(WHITE, BLACK);
    display.printf("%s\n", phaseName());
    if (runtime_ != nullptr) {
        const PlayerSnapshot snapshot = runtime_->snapshot();
        display.printf("State %s Track %u/%u\n", playerStateName(snapshot.state),
                       static_cast<unsigned>(snapshot.currentIndex + 1),
                       static_cast<unsigned>(snapshot.queueCount));
        display.printf("Pos %lu SR %lu\n",
                       static_cast<unsigned long>(snapshot.positionMs),
                       static_cast<unsigned long>(snapshot.sampleRateHz));
        display.printf("EOF %lu Err %lu\n",
                       static_cast<unsigned long>(snapshot.trackEndedEvents),
                       static_cast<unsigned long>(snapshot.audioErrorEvents));
    }
    display.printf("Heap %lu Min %lu\n",
                   static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(minimumHeap_));
    if (phase_ == Phase::Failed) {
        display.setTextColor(ORANGE, BLACK);
        display.printf("%s\n", failure_);
    } else if (phase_ == Phase::Passed) {
        display.setTextColor(GREEN, BLACK);
        display.println("Logs saved to SD");
        display.println("LISTEN: MANUAL");
    } else {
        display.println("Do not press keys");
    }
}

bool P1DeviceTestRunner::writeGateLog(const char* path, const char* status,
                                      const char* detail) {
    SD.remove(path);
    File log = SD.open(path, FILE_WRITE);
    if (!log) {
        return false;
    }
    const PlayerSnapshot snapshot = runtime_ == nullptr
                                        ? PlayerSnapshot{}
                                        : runtime_->snapshot();
    log.printf("status=%s\n", status);
    log.printf("version=%s\n", ADV_WALKMAN_VERSION);
    log.printf("gate=%d\n", P1_DEVICE_GATE);
    log.printf("phase=%s\n", phaseName());
    log.printf("detail=%s\n", detail == nullptr ? "none" : detail);
    log.printf("elapsed_ms=%lu\n",
               static_cast<unsigned long>(millis() - runStartedAtMs_));
    log.printf("state=%s\n", playerStateName(snapshot.state));
    log.printf("position_ms=%lu\n",
               static_cast<unsigned long>(snapshot.positionMs));
    log.printf("sample_rate=%lu\n",
               static_cast<unsigned long>(snapshot.sampleRateHz));
    log.printf("track_ended_events=%lu\n",
               static_cast<unsigned long>(snapshot.trackEndedEvents));
    log.printf("audio_error_events=%lu\n",
               static_cast<unsigned long>(snapshot.audioErrorEvents));
    log.printf("heap_start=%lu\n", static_cast<unsigned long>(initialHeap_));
    log.printf("heap_now=%lu\n", static_cast<unsigned long>(ESP.getFreeHeap()));
    log.printf("heap_min_sampled=%lu\n",
               static_cast<unsigned long>(minimumHeap_));
    log.printf("backpressure_max=%lu\n",
               static_cast<unsigned long>(maximumBackpressure_));
    log.printf("service_max_us=%lu\n",
               static_cast<unsigned long>(maximumServiceUs_));
    log.printf("seek_max_error_ms=%lu\n",
               static_cast<unsigned long>(maximumSeekErrorMs_));
    log.printf("state_writes_this_boot=%lu\n",
               runtime_ == nullptr ? 0UL :
                   static_cast<unsigned long>(runtime_->stateWriteCount()));
    log.printf("fixture_hashes_verified=%u\n",
               fixtureHashesVerified_ ? 1U : 0U);
    log.printf("fixture_hash_failure_index=%u\n",
               static_cast<unsigned>(fixtureHashFailureIndex_));
    log.printf("fixture_hash_observed=%s\n", fixtureHashObserved_);
    for (size_t index = 0; index < kP1ValidFixtureCount; ++index) {
        log.printf("fixture_%u_path=%s\n", static_cast<unsigned>(index + 1),
                   p1FixturePath(index));
        log.printf("fixture_%u_sha256=%s\n",
                   static_cast<unsigned>(index + 1),
                   p1FixtureSha256(index));
    }
    log.printf("corrupt_fixture_sha256=%s\n", p1CorruptFixtureSha256());
    log.flush();
    log.close();

    File verify = SD.open(path, FILE_READ);
    if (!verify) {
        return false;
    }
    char firstLine[32]{};
    const size_t received = verify.readBytesUntil('\n', firstLine,
                                                   sizeof(firstLine) - 1);
    verify.close();
    firstLine[received] = '\0';
    char expected[32]{};
    std::snprintf(expected, sizeof(expected), "status=%s", status);
    return std::strcmp(firstLine, expected) == 0;
}

bool P1DeviceTestRunner::hasRestartMarker() const {
    File log = SD.open(kP104Log, FILE_READ);
    if (!log) {
        return false;
    }
    char content[256]{};
    const size_t count = log.read(
        reinterpret_cast<uint8_t*>(content), sizeof(content) - 1);
    log.close();
    content[count] = '\0';
    return std::strstr(content, "status=RUNNING") != nullptr &&
           std::strstr(content, "RESTART_PENDING") != nullptr;
}

void P1DeviceTestRunner::updateDiagnostics(const PlayerSnapshot& snapshot) {
    minimumHeap_ = minimumHeap_ == 0
                       ? ESP.getFreeHeap()
                       : std::min(minimumHeap_, ESP.getFreeHeap());
    maximumBackpressure_ =
        std::max(maximumBackpressure_, snapshot.backpressureEvents);
    maximumServiceUs_ = std::max(maximumServiceUs_, snapshot.serviceMaxUs);
}

}  // namespace player
}  // namespace adv_walkman
