#pragma once

#include <AudioFileSource.h>
#include <SD.h>

namespace adv_walkman {
namespace player {

class TrackedSdFileSource final : public AudioFileSource {
  public:
    TrackedSdFileSource() = default;
    ~TrackedSdFileSource() override;

    bool open(const char* filename) override;
    uint32_t read(void* data, uint32_t len) override;
    uint32_t readNonBlock(void* data, uint32_t len) override;
    bool seek(int32_t pos, int dir) override;
    bool close() override;
    bool isOpen() override;
    uint32_t getSize() override;
    uint32_t getPos() override;

    void setReadLimit(uint32_t exclusiveEnd);
    bool eofObserved() const;
    bool readError() const;
    uint64_t bytesRead() const;

  private:
    File file_;
    uint32_t physicalSize_ = 0;
    uint32_t readLimit_ = 0;
    uint64_t bytesRead_ = 0;
    bool eofObserved_ = false;
    bool readError_ = false;
};

}  // namespace player
}  // namespace adv_walkman
