#pragma once
#include <algorithm>
#include <SD.h>
#include <M5GFX.h>
#include "MediaTypes.h"
#include "ResourceIo.h"
#include "MediaLayout.h"
#include "CoverPolicy.h"
namespace adv_walkman { namespace player {
class CoverRenderer final {
  public:
    void selectTrack(const char* track);
    void release();
    void finishFrame(){if(mayCloseCoverAfterFrame(phase_))file_.close();requestedRow_=-1;}
    void suspend(){file_.close();requestedRow_=-1;if(phase_)phase_=1;}
    int top()const{return (188-height_)/2;}
    int height()const{return height_;}
    int stripeHeight()const{return top()%2?1:std::min<unsigned>(2,coverRowsPerRead(width_));}
    uint32_t opens()const{return opens_;}
    void service();
    bool busy()const{return phase_!=0 || (state_==MediaState::Ready && requestedRow_>=0 && readyRow_!=requestedRow_);}
    void requestRow(int y);
    bool rowReady(int y) const { return state_!=MediaState::Ready || readyRow_==y; }
    void drawRow(lgfx::LGFXBase& canvas,int contentY,uint16_t background) const;
    MediaState state() const {return state_;}
    const char* error() const {return error_;}
    const ResourceFailure& failure() const {return failure_;}
    const char* path() const {return path_;}
    uint32_t revision() const {return revision_;}
    uint32_t bytesRead() const {return bytesRead_;}
  private:
    fs::File file_;
    char path_[560]{};
    uint8_t bytes_[512]{};
    MediaState state_=MediaState::Idle;
    const char* error_="none";
    ResourceFailure failure_{};
    uint8_t phase_=0;
    uint32_t revision_=0,expectedCrc_=0,crc_=~0U,remaining_=0,bytesRead_=0;
    int requestedRow_=-1,readyRow_=-1;
    uint16_t width_=120,height_=144;
    uint32_t opens_=0;
};
} }
