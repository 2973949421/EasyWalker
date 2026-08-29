#include <M5GFX.h>
#include "LibraryCoverReader.h"
#include <algorithm>
#include <cstring>
namespace adv_walkman { namespace player {
uint64_t LibraryCoverReader::pathHash(const char* value){
    uint64_t hash=1469598103934665603ULL;
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(value);p&&*p;++p){hash^=*p;hash*=1099511628211ULL;}
    return hash;
}
bool LibraryCoverReader::restoreValidation(uint64_t key){
    for(const auto& item:validations_)if(item.valid&&item.key==key){
        width_=item.width;height_=item.height;expected_=item.expected;fileBytes_=item.fileBytes;
        ++validationHits;return true;
    }
    return false;
}
void LibraryCoverReader::rememberValidation(uint64_t key){
    auto& item=validations_[nextValidation_++%3];
    item.key=key;item.expected=expected_;item.fileBytes=fileBytes_;
    item.width=width_;item.height=height_;item.valid=true;
}
void LibraryCoverReader::select(const char* directory,uint32_t generation){
    if(!directory||std::strncmp(directory,"/Music",6)||std::strstr(directory,"..")||
       (directory[6]&& (directory[6]!='/'||!directory[7]||std::strchr(directory+7,'/')))){fail("library_cover_path");return;}
    char target[sizeof(path_)];
    const int n=directory[6]?std::snprintf(target,sizeof(target),"/ADVWalkman/library-covers/folders/%s/cover.adv",directory+7):
        std::snprintf(target,sizeof(target),"/ADVWalkman/library-covers/root.cover.adv");
    if(n<0||n>=int(sizeof(target))){fail("library_cover_path");return;}
    if(!std::strcmp(target,path_)&&state_==MediaState::Ready){generation_=generation;return;}
    if(state_==MediaState::Loading||band_.active())++cancels;
    band_.cancel();file_.close();bandGeneration_=0;failure_=ResourceFailure{};
    std::strcpy(path_,target);generation_=generation;
    if(restoreValidation(pathHash(path_))){state_=MediaState::Ready;phase_=0;return;}
    state_=MediaState::Loading;phase_=1;
}
void LibraryCoverReader::fail(const char* reason){
    band_.cancel();bandGeneration_=0;
    failure_.set(reason,"library_cover",errno);file_.close();phase_=0;
    state_=MediaState::Error;++errors;
}
void LibraryCoverReader::service(){
    const uint32_t at=micros();
    if(phase_==1){state_=openResource(file_,path_,512,failure_);phase_=state_==MediaState::Loading?2:0;if(state_==MediaState::Error)++errors;}
    else if(phase_==2){const int n=file_.read(bytes_,24);++reads;
        width_=mediaU16(bytes_+8);height_=mediaU16(bytes_+10);
        if(n!=24||std::memcmp(bytes_,"LCOV",4)||mediaU16(bytes_+4)!=1||mediaU16(bytes_+6)!=24||!width_||width_>135||!height_||height_>174||mediaU16(bytes_+12)!=1||mediaU16(bytes_+14)!=0||mediaU32(bytes_+16)!=uint32_t(width_)*height_*2||file_.size()!=24+mediaU32(bytes_+16))fail("library_cover_header");
        else{expected_=mediaU32(bytes_+20);fileBytes_=file_.size();crc_=~0U;remaining_=width_*height_*2;phase_=3;}}
    else if(phase_==3){const unsigned length=std::min<uint32_t>(512,remaining_);const int n=file_.read(bytes_,length);++reads;
        if(n!=int(length))fail("library_cover_truncated");else{crc_=mediaCrc(crc_,bytes_,length);remaining_-=length;
            if(!remaining_){phase_=4;}}}
    else if(phase_==4){file_.close();phase_=0;if((crc_^~0U)!=expected_)fail("library_cover_crc");else{rememberValidation(pathHash(path_));state_=MediaState::Ready;}}
    else if(state_==MediaState::Ready&&band_.active()&&!band_.ready()){
        if(bandGeneration_!=generation_){band_.cancel();bandGeneration_=0;++staleRejects;serviceMaxUs=std::max<uint32_t>(serviceMaxUs,micros()-at);return;}
        if(!file_){if(openResource(file_,path_,512,failure_)!=MediaState::Loading)fail("library_cover_reopen");}
        else{const unsigned length=band_.request();const auto offset=band_.offset(24);
            if(file_.position()!=offset){if(!file_.seek(offset))fail("library_cover_seek");}
            else if(file_.read(bytes_,length)!=int(length))fail("library_cover_read");
            else{if(!band_.append(bytes_,length))fail("library_band_canvas");++reads;}}}
    serviceMaxUs=std::max<uint32_t>(serviceMaxUs,micros()-at);
}
} }
