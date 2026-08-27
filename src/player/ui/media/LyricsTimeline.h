#pragma once
#include <SD.h>
#include "MediaTypes.h"
#include "ResourceIo.h"

namespace adv_walkman { namespace player {
class LyricsTimeline final {
  public:
    bool selectTrack(const char* track);
    void release();
    ~LyricsTimeline() { release(); }
    void service();
    void updatePosition(uint32_t positionMs,uint32_t durationMs);
    MediaState state() const { return state_; }
    const char* error() const { return error_; }
    const ResourceFailure& failure() const { return failure_; }
    void failurePath(char* output,size_t size) const;
    bool hasLyrics() const { return state_==MediaState::Ready && count_[0]>0; }
    bool windowReady() const { return windowReady_; }
    int current() const { return current_; }
    uint32_t revision() const { return revision_; }
    uint32_t startMs() const;
    uint32_t endMs() const;
    uint32_t cueStart(int index) const;
    uint32_t cueEnd(int index) const;
    const char* text(int relative,unsigned language) const;
    uint16_t count(unsigned language) const { return count_[language&1]; }
    uint32_t bytesRead() const { return bytesRead_; }
    uint32_t serviceMaxUs() const { return serviceMaxUs_; }
    static constexpr size_t workBytes() { return sizeof(Work); }
  private:
    struct Cue { uint32_t time, offset; };
    struct Work {
        Cue cues[2][512];
        char text[3][2][1025];
        char line[1025];
        uint8_t io[512];
    };
    Work* work_=nullptr;
    fs::File file_,directory_;
    char base_[560]{},alternative_[560]{};
    const char* error_="none";
    ResourceFailure failure_{};
    uint8_t openedLanguage_=0;
    MediaState state_=MediaState::Idle;
    uint16_t count_[2]{},sortAt_=0,pairAt_=0;
    uint8_t usedTranslation_[64]{};
    uint8_t phase_=0,language_=0,alternativeCount_=0,loadSlot_=0;
    uint16_t lineLength_=0;
    uint32_t lineOffset_=0,readOffset_=0,revision_=0,bytesRead_=0,serviceMaxUs_=0;
    int32_t offsetMs_[2]{};
    int current_=-2,loadCurrent_=-2;
    uint32_t positionMs_=0,durationMs_=0;
    bool translationHant_=false,windowReady_=false,eof_=false;
    bool hasText_[2]{};
    void fail(const char* reason);
    bool parseLine(bool index);
    void openLanguage(bool original);
    bool readIndexSlice();
    void startWindow();
    void loadWindowSlice();
};
static_assert(LyricsTimeline::workBytes()<=16*1024,"lyrics work set");
} }
