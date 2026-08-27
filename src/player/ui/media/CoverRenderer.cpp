#include "CoverRenderer.h"
#include <cstring>
#include <algorithm>
namespace adv_walkman { namespace player {
void CoverRenderer::release(){file_.close();state_=MediaState::Idle;phase_=0;readyRow_=requestedRow_=-1;}
void CoverRenderer::selectTrack(const char* track){
    release();error_="none";++revision_;
    if(!mediaResourcePath(track,"/ADVWalkman/covers",".cover.adv",path_,sizeof(path_))){state_=MediaState::Error;error_="cover_path";return;}
    state_=MediaState::Loading;phase_=1;
}
void CoverRenderer::service(){
    auto fail=[&](const char* reason){state_=MediaState::Error;error_=reason;phase_=0;file_.close();++revision_;};
    if(phase_==1){file_=SD.open(path_,FILE_READ);phase_=2;if(!file_){state_=MediaState::Missing;phase_=0;++revision_;}
        else if(!file_.setBufferSize(512))fail("cover_buffer");}
    else if(phase_==2){
        const int n=file_.read(bytes_,28);bytesRead_+=n>0?n:0;
        if(n!=28 || std::memcmp(bytes_,"ACOV",4)||mediaU16(bytes_+4)!=1||mediaU16(bytes_+6)!=28||
           mediaU16(bytes_+8)!=120||mediaU16(bytes_+10)!=144||mediaU16(bytes_+16)!=1||mediaU16(bytes_+18)!=0||
           mediaU32(bytes_+20)!=34560||file_.size()!=34588){fail("cover_header");return;}
        const unsigned cols=mediaU16(bytes_+12),rows=mediaU16(bytes_+14);
        if(!((cols==26&&rows==20)||(cols==30&&rows==24)||(cols==34&&rows==26))){fail("cover_grid");return;}
        expectedCrc_=mediaU32(bytes_+24);crc_=~0U;remaining_=34560;phase_=3;
    }else if(phase_==3){
        const unsigned length=std::min<uint32_t>(512,remaining_);const int n=file_.read(bytes_,length);bytesRead_+=n>0?n:0;
        if(n!=int(length)){fail("cover_truncated");return;}crc_=mediaCrc(crc_,bytes_,length);remaining_-=length;
        if(!remaining_){if((crc_^~0U)!=expectedCrc_){fail("cover_crc");return;}state_=MediaState::Ready;phase_=0;++revision_;}
    }else if(state_==MediaState::Ready && requestedRow_>=0 && readyRow_!=requestedRow_){
        const unsigned length=std::min(2,144-requestedRow_)*240;
        if(!file_.seek(28+requestedRow_*240) || file_.read(bytes_,length)!=int(length)){fail("cover_read");return;}
        bytesRead_+=length;readyRow_=requestedRow_;
    }
}
void CoverRenderer::requestRow(int y){if(y>=12 && y<156)requestedRow_=y-12;else requestedRow_=-1;}
void CoverRenderer::drawRow(lgfx::LGFXBase& canvas,int y,uint16_t background) const {
    if(state_==MediaState::Ready && y>=12 && y<156 && readyRow_==y-12){
        for(int row=0;row<std::min(2,int(canvas.height()));++row)
            for(unsigned x=0;x<120;++x)canvas.drawPixel(7+x,row,mediaU16(bytes_+row*240+2*x));
    }else if(state_!=MediaState::Ready){
        canvas.setFont(&fonts::Font0);canvas.setTextSize(1.5f);canvas.setTextColor(0x8410,background);
        canvas.drawString(state_==MediaState::Loading?"LOADING COVER":"NO COVER",12,72-y);
    }
}
} }
