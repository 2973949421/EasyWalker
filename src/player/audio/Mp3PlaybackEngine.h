#pragma once

#include <AudioGeneratorMP3.h>
#include <M5Unified.h>

#include "player/audio/AudioTypes.h"
#include "player/audio/M5SpeakerPcmOutput.h"
#include "player/audio/Mp3Probe.h"
#include "player/audio/TrackedSdFileSource.h"

namespace adv_walkman {
namespace player {

class PlaybackMp3Decoder final : public AudioGeneratorMP3 {
  public:
    bool begin(AudioFileSource* source, AudioOutput* output) override;
    bool loop() override;
    bool stop() override;
    int streamErrorCode() const;

  private:
    void latchStreamError();

    int terminalStreamError_ = -1;
    bool terminalStreamErrorValid_ = false;
};

class Mp3PlaybackEngine final {
  public:
    Mp3PlaybackEngine();
    ~Mp3PlaybackEngine();

    bool begin();
    bool open(const char* path, uint32_t positionMs = 0,
              bool startPaused = false, uint32_t sourceOffsetHint = 0);
    void service();
    bool pause();
    bool resume();
    void stop();
    bool seekToMs(uint32_t targetMs);
    void setVolume(uint8_t volume);
    bool pollEvent(AudioEvent& event);
    AudioStatus status() const;

  private:
    static constexpr size_t kPathCapacity = 512;
    static constexpr uint8_t kInitialVolume = 128;

    bool startAt(const Mp3SeekPoint& point, bool startPaused);
    void servicePlaying();
    void serviceDraining();
    void closePlaybackImmediate();
    void updateSourceOffset();
    uint32_t playbackPositionMs() const;
    bool fail(AudioError error);
    void queueEvent(AudioEventType type, AudioError error);

    M5SpeakerPcmOutput output_;
    TrackedSdFileSource source_;
    PlaybackMp3Decoder decoder_;
    Mp3Info mp3Info_{};
    AudioState state_ = AudioState::Empty;
    AudioError error_ = AudioError::None;
    char path_[kPathCapacity]{};
    uint32_t positionBaseMs_ = 0;
    uint32_t sourceByteOffset_ = 0;
    uint32_t serviceMaxUs_ = 0;
    uint8_t volume_ = kInitialVolume;
    bool initialized_ = false;
    bool drainFlushed_ = false;
    bool eventPending_ = false;
    AudioEvent pendingEvent_{};
};

}  // namespace player
}  // namespace adv_walkman
