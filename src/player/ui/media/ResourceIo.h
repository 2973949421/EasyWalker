#pragma once
#include <SD.h>
#include <cerrno>
#include <cstdio>
#include <sys/stat.h>
#include "MediaTypes.h"

namespace adv_walkman { namespace player {
struct ResourceFailure {
    const char* reason="none";
    const char* operation="none";
    int systemError=0;
    int32_t expected=-1,actual=-1;
    void set(const char* why,const char* op,int code=0,int32_t want=-1,int32_t got=-1) {
        if(reason[0]!='n' || reason[1]!='o' || reason[2]!='n' || reason[3]!='e')return;
        reason=why;operation=op;systemError=code;expected=want;actual=got;
    }
};
// A failed fopen is NOT proof of a missing file. Preserve its errno before
// probing existence, and classify Missing only on an explicit ENOENT.
inline MediaState openResource(fs::File& file,const char* path,size_t buffer,
                                ResourceFailure& failure) {
    file.close();failure=ResourceFailure{};errno=0;file=SD.open(path,FILE_READ);const int openError=errno;
    if(!file) {
        char native[600];std::snprintf(native,sizeof(native),"/sd%s",path);
        struct stat info{};errno=0;const int result=::stat(native,&info);const int statError=errno;
        const bool absent=result!=0 && statError==ENOENT;
        failure.set(absent?"not_found":"open_failed","open",openError?openError:statError);
        return absent?MediaState::Missing:MediaState::Error;
    }
    errno=0;if(!file.setBufferSize(buffer)) {
        failure.set("buffer_failed","buffer",errno);file.close();return MediaState::Error;
    }
    return MediaState::Loading;
}
inline const char* mediaStateName(MediaState state) {
    switch(state){case MediaState::Idle:return "idle";case MediaState::Loading:return "loading";
    case MediaState::Ready:return "ready";case MediaState::Missing:return "missing";default:return "error";}
}
} }
