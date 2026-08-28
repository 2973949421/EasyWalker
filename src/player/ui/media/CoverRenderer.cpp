#include "CoverRenderer.h"
#include <cstring>
#include <algorithm>
namespace adv_walkman { namespace player {
void CoverRenderer::release(){file_.close();state_=MediaState::Idle;phase_=0;readyRow_=requestedRow_=-1;}
void CoverRenderer::selectTrack(const char* track){
    release();error_="none";failure_=ResourceFailure{};++revision_;
    if(!mediaResourcePath(track,"/ADVWalkman/covers",".cover.adv",path_,sizeof(path_))){state_=MediaState::Error;error_="cover_path";failure_.set(error_,"resolve");return;}
    state_=MediaState::Loading;phase_=1;
}
void CoverRenderer::service(){
    auto fail=[&](const char* reason){if(std::strcmp(failure_.reason,reason))failure_.set(reason,"validate",errno);state_=MediaState::Error;error_=reason;phase_=0;file_.close();++revision_;};
    if(phase_==1){++opens_;state_=openResource(file_,path_,512,failure_);phase_=state_==MediaState::Loading?2:0;
        if(!phase_){error_=failure_.reason;++revision_;}}
    else if(phase_==2){
        errno=0;const int n=file_.read(bytes_,28);bytesRead_+=n>0?n:0;
        if(n!=28){failure_.set("cover_header_read","read",errno,28,n);fail("cover_header_read");return;}
        if(std::memcmp(bytes_,"ACOV",4)||mediaU16(bytes_+4)!=1||mediaU16(bytes_+6)!=28||
           !validCoverDimensions(mediaU16(bytes_+8),mediaU16(bytes_+10),mediaU32(bytes_+20))||mediaU16(bytes_+16)!=1||mediaU16(bytes_+18)!=0||
           file_.size()!=28+mediaU32(bytes_+20)){fail("cover_header");return;}
        width_=mediaU16(bytes_+8);height_=mediaU16(bytes_+10);
        const unsigned cols=mediaU16(bytes_+12),rows=mediaU16(bytes_+14);
        if(!((cols==26&&rows==20)||(cols==30&&rows==24)||(cols==34&&rows==26)||(cols==40&&rows==32)||(cols==48&&rows==40))){fail("cover_grid");return;}
        expectedCrc_=mediaU32(bytes_+24);crc_=~0U;remaining_=width_*height_*2;phase_=3;
    }else if(phase_==3){
        const unsigned length=std::min<uint32_t>(512,remaining_);errno=0;const int n=file_.read(bytes_,length);bytesRead_+=n>0?n:0;
        if(n!=int(length)){failure_.set("cover_truncated","read",errno,length,n);fail("cover_truncated");return;}crc_=mediaCrc(crc_,bytes_,length);remaining_-=length;
        if(!remaining_){if((crc_^~0U)!=expectedCrc_){fail("cover_crc");return;}state_=MediaState::Ready;phase_=0;file_.close();++revision_;}
    }else if(state_==MediaState::Ready && requestedRow_>=0 && readyRow_!=requestedRow_){
        if(!file_){++opens_;const auto opened=openResource(file_,path_,512,failure_);if(opened!=MediaState::Loading)fail(failure_.reason);return;}
        const unsigned length=std::min(stripeHeight(),height_-requestedRow_)*width_*2;
        const uint32_t offset=28+requestedRow_*width_*2;
        errno=0;if(file_.position()!=offset&&!file_.seek(offset)){failure_.set("cover_seek","seek",errno);fail("cover_seek");return;}
        const int n=file_.read(bytes_,length);if(n!=int(length)){failure_.set("cover_read","read",errno,length,n);fail("cover_read");return;}
        bytesRead_+=length;readyRow_=requestedRow_;
    }
}
void CoverRenderer::requestRow(int y){if(y>=top() && y<top()+height_)requestedRow_=y-top();else requestedRow_=-1;}
void CoverRenderer::drawRow(lgfx::LGFXBase& canvas,int y,uint16_t background) const {
    if(state_==MediaState::Ready && y>=top() && y<top()+height_ && readyRow_==y-top()){
        for(int row=0;row<std::min({stripeHeight(),int(canvas.height()),height_-readyRow_});++row)
            for(unsigned x=0;x<width_;++x)canvas.drawPixel((135-width_)/2+x,row,mediaU16(bytes_+row*width_*2+2*x));
    }else if(state_!=MediaState::Ready){
        // Font-independent fallback graphic; normal UI never falls back to
        // Font0 merely because a song lacks a jacket.
        canvas.drawRect(43,68-y,48,48,0x8410);
        canvas.drawCircle(67,92-y,13,0x8410);
        canvas.fillCircle(67,92-y,3,0x8410);
    }
}
} }
