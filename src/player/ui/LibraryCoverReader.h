#pragma once
#include <M5GFX.h>
#include "media/ResourceIo.h"
namespace adv_walkman { namespace player {
class LibraryCoverReader {
 public:
    void select(const char* directory);
    void release(){file_.close();phase_=0;state_=MediaState::Idle;row_=requested_=-1;path_[0]=0;}
    void service();
    void requestRow(int y){requested_=y;}
    bool rowReady(int y)const{return state_==MediaState::Ready&&row_==y;}
    void drawRow(lgfx::LGFXBase& canvas,int y)const;
    MediaState state()const{return state_;}
    const char* path()const{return path_;}
    const ResourceFailure& failure()const{return failure_;}
    uint32_t reads=0,errors=0,cancels=0,serviceMaxUs=0;
 private:
    void fail(const char* reason);
    fs::File file_;char path_[600]{};uint8_t bytes_[512]{};
    ResourceFailure failure_{};MediaState state_=MediaState::Idle;
    uint8_t phase_=0;uint32_t crc_=0,expected_=0,remaining_=0;
    int row_=-1,requested_=-1;
};
} }
