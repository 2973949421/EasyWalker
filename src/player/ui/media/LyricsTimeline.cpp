#include "LyricsTimeline.h"
#include "FrameCachePolicy.h"
#include <algorithm>
#include <new>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace adv_walkman { namespace player {
namespace {
constexpr uint32_t kOffsetMask=(1U<<18)-1;
bool timestamp(const char* p,const char*& end,uint32_t& time) {
    if(*p++!='[')return false;
    unsigned minutes=0,seconds=0,digits=0;
    while(*p>='0'&&*p<='9'&&digits<3){ minutes=minutes*10+(*p++-'0');++digits; }
    if(!digits || *p++!=':')return false;
    if(p[0]<'0'||p[0]>'9'||p[1]<'0'||p[1]>'9')return false;
    seconds=(p[0]-'0')*10+p[1]-'0';p+=2;if(seconds>=60)return false;
    unsigned fraction=0,multiplier=100;
    if(*p=='.'||*p==':') {
        ++p;unsigned n=0;while(*p>='0'&&*p<='9'&&n<3){fraction+=(*p++-'0')*multiplier;multiplier/=10;++n;}
        if(!n)return false;
    }
    if(*p++!=']')return false;
    time=(minutes*60+seconds)*1000+fraction;end=p;return true;
}
}
void LyricsTimeline::release() {
    suspend();delete work_;work_=nullptr;state_=MediaState::Idle;phase_=0;
    count_[0]=count_[1]=0;current_=-2;windowReady_=false;alternative_[0]=0;
    slotCue_[0]=slotCue_[1]=-2;slotReady_[0]=slotReady_[1]=0;currentSlot_=0;
}
bool LyricsTimeline::selectTrack(const char* track) {
    release();error_="none";failure_=ResourceFailure{};failureDirectory_=false;alternativeCount_=0;offsetMs_[0]=offsetMs_[1]=0;translationHant_=false;
    hasText_[0]=hasText_[1]=false;
    std::memset(usedTranslation_,0,sizeof(usedTranslation_));
    if(!mediaResourcePath(track,"/Lyrics","",base_,sizeof(base_))) { fail("lyric_path");return false; }
    work_=new(std::nothrow) Work{};if(!work_){fail("lyrics_memory");return false;}
    state_=MediaState::Loading;phase_=1;language_=0;++revision_;return true;
}
void LyricsTimeline::fail(const char* reason) { if(std::strcmp(failure_.reason,reason))failure_.set(reason,"parse",errno);error_=reason;state_=MediaState::Error;phase_=0;suspend();windowReady_=false;++revision_; }
void LyricsTimeline::openLanguage(bool original) {
    char path[576];
    if(original && alternative_[0])std::snprintf(path,sizeof(path),"%s",alternative_);
    else std::snprintf(path,sizeof(path),"%s%s",base_,original?".lrc":(translationHant_?".zh-Hant.lrc":".zh-Hans.lrc"));
    openedLanguage_=original?0:1;ResourceFailure attempt;
    const auto opened=openResource(file_,path,512,attempt);lineLength_=0;readOffset_=lineOffset_=0;eof_=false;
    if(opened==MediaState::Error){failure_=attempt;fail(attempt.reason);return;}
    if(file_ && file_.size()>128*1024)fail("lrc_file_limit");
}
bool LyricsTimeline::parseLine(bool index) {
    work_->line[lineLength_]=0;const char* p=work_->line;
    if(lineOffset_==0 && lineLength_>=3 && uint8_t(p[0])==0xEF && uint8_t(p[1])==0xBB && uint8_t(p[2])==0xBF)p+=3;
    bool invalid=false;const char* check=p;while(*check)mediaCodepoint(check,invalid);
    if(invalid){fail("lrc_utf8");return false;}
    if(index && std::strncmp(p,"[offset:",8)==0) {
        char* end=nullptr;const long value=std::strtol(p+8,&end,10);
        if(!end || *end!=']' || value<-3600000 || value>3600000){fail("lrc_offset");return false;}
        offsetMs_[language_]=value;return true;
    }
    const char* end=nullptr;uint32_t time;bool found=false;
    while(timestamp(p,end,time)) {
        found=true;
        if(index) {
            if(count_[language_]>=512){fail("lrc_cue_limit");return false;}
            work_->cues[language_][count_[language_]++]={time,lineOffset_};
        }
        p=end;
    }
    if(!index) {
        while(*p==' '||*p=='\t')++p;
        std::snprintf(work_->text[(currentSlot_+loadSlot_/2)%2][loadSlot_%2],1025,"%s",found?p:"");
    } else if(!found && p[0]=='[' && p[1]>='0' && p[1]<='9') { fail("lrc_timestamp");return false; }
    else if(index && found){while(*p==' '||*p=='\t')++p;if(*p)hasText_[language_]=true;}
    return true;
}
bool LyricsTimeline::readIndexSlice() {
    const size_t wanted=std::min<size_t>(sizeof(work_->io),file_.size()-file_.position());
    errno=0;const int n=file_.read(work_->io,wanted);
    if(n!=int(wanted)){failure_.set("lrc_read","read",errno,wanted,n);fail("lrc_read");return false;}bytesRead_+=n;
    for(int i=0;i<n;++i) {
        const char c=work_->io[i];++readOffset_;
        if(c==0){fail("lrc_nul");return false;}
        if(c=='\n') { if(!parseLine(true))return false;lineLength_=0;lineOffset_=readOffset_; }
        else if(c!='\r') { if(lineLength_>=1024){fail("lrc_line_limit");return false;}work_->line[lineLength_++]=c; }
    }
    if(n==0 || file_.position()>=file_.size()) {
        if(lineLength_ && !parseLine(true))return false;
        file_.close();lineLength_=0;sortAt_=0;phase_=7;return true;
    }
    return false;
}
void LyricsTimeline::service() {
    if(!work_ || state_==MediaState::Error || state_==MediaState::Missing)return;
    const uint32_t started=micros();
    if(phase_==1) { openLanguage(true);if(state_==MediaState::Error)return;phase_=file_?6:2; }
    else if(phase_==2) {
        char parent[560];std::snprintf(parent,sizeof(parent),"%s",base_);char* slash=std::strrchr(parent,'/');if(slash)*slash=0;
        errno=0;directory_=SD.open(parent,FILE_READ);phase_=3;if(!directory_){const int code=errno;failureDirectory_=true;failure_.set(code==ENOENT?"not_found":"directory_open","directory",code);state_=code==ENOENT?MediaState::Missing:MediaState::Error;error_=failure_.reason;phase_=0;++revision_;}
    } else if(phase_==3) {
        errno=0;fs::File entry=directory_.openNextFile();const int scanError=errno;
        if(!entry && scanError && scanError!=ENOENT){failureDirectory_=true;failure_.set("directory_scan","enumerate",scanError);fail("directory_scan");return;}
        if(!entry){directory_.close();if(alternativeCount_>1)fail("ambiguous_original_lrc");else if(alternativeCount_==1)phase_=4;else {state_=MediaState::Missing;phase_=0;++revision_;}}
        else if(!entry.isDirectory()) {
            const char* filename=std::strrchr(entry.name(),'/');filename=filename?filename+1:entry.name();
            const char* basename=std::strrchr(base_,'/');basename=basename?basename+1:base_;
            const size_t len=std::strlen(basename),total=std::strlen(filename);
            if(total>len+5 && std::strncmp(filename,basename,len)==0 && filename[len]=='.' &&
               std::strcmp(filename+total-4,".lrc")==0 && std::strncmp(filename+len,".zh-",4)!=0) {
                ++alternativeCount_;std::snprintf(alternative_,sizeof(alternative_),"%.*s/%s",int(basename-base_-1),base_,filename);
            }
        }
    } else if(phase_==4) {openLanguage(true);if(state_==MediaState::Error)return;phase_=file_?6:0;if(!file_)state_=MediaState::Missing;}
    else if(phase_==5) {
        openLanguage(false);if(state_==MediaState::Error)return;
        if(file_)phase_=6;else if(!translationHant_)translationHant_=true;else {phase_=8;pairAt_=0;}
    } else if(phase_==6) readIndexSlice();
    else if(phase_==7) {
        // Stable insertion, one entry per service; offset metadata is applied
        // before insertion so negative offset clamping preserves stable order.
        if(sortAt_<count_[language_]) {
            Cue cue=work_->cues[language_][sortAt_];cue.time=uint32_t(std::max<int64_t>(0,int64_t(cue.time)+offsetMs_[language_]));
            unsigned at=sortAt_;while(at && work_->cues[language_][at-1].time>cue.time){work_->cues[language_][at]=work_->cues[language_][at-1];--at;}
            work_->cues[language_][at]=cue;++sortAt_;
        } else if(language_==0) {language_=1;phase_=5;}
        else {phase_=8;pairAt_=0;}
    } else if(phase_==8) {
        if(pairAt_<count_[0]) {
            auto& cue=work_->cues[0][pairAt_];int best=-1;uint32_t distance=301;
            for(unsigned i=0;i<count_[1];++i) if(!(usedTranslation_[i/8]&(1U<<(i%8)))) {
                const uint32_t d=cue.time>work_->cues[1][i].time?cue.time-work_->cues[1][i].time:work_->cues[1][i].time-cue.time;
                if(d<distance){distance=d;best=i;}
            }
            if(best>=0){cue.offset|=uint32_t(best+1)<<18;usedTranslation_[best/8]|=1U<<(best%8);}
            ++pairAt_;
        } else {state_=count_[0]&&hasText_[0]?MediaState::Ready:MediaState::Missing;phase_=0;current_=-2;++revision_;updatePosition(positionMs_,durationMs_);}
    } else if(phase_==9)loadWindowSlice();
    serviceMaxUs_=std::max<uint32_t>(serviceMaxUs_,micros()-started);
}
void LyricsTimeline::updatePosition(uint32_t positionMs,uint32_t durationMs) {
    positionMs_=positionMs;durationMs_=durationMs;if(!hasLyrics())return;
    unsigned lo=0,hi=count_[0];while(lo<hi){const unsigned mid=(lo+hi)/2;if(work_->cues[0][mid].time<=positionMs)lo=mid+1;else hi=mid;}
    const int target=int(lo)-1;
    if(target!=current_){current_=target;windowReady_=false;startWindow();}
}
void LyricsTimeline::startWindow() {
    loadCurrent_=current_;loadSlot_=0;lineLength_=0;lineStarted_=false;eof_=false;
    const int wanted=std::max(0,current_);
    currentSlot_=currentTextSlot(slotCue_[0],slotCue_[1],wanted);
    for(unsigned i=0;i<2;++i){const unsigned slot=(currentSlot_+i)%2;
        if(slotCue_[slot]!=wanted+int(i)){
            slotCue_[slot]=wanted+i;slotReady_[slot]=0;
            std::memset(work_->text[slot],0,sizeof(work_->text[slot]));
        }
    }
    phase_=9;
}
void LyricsTimeline::loadWindowSlice() {
    if(loadSlot_>=4) {phase_=0;windowReady_=true;++revision_;return;}
    const unsigned slot=(currentSlot_+loadSlot_/2)%2;
    const int cueIndex=slotCue_[slot];
    const unsigned lang=loadSlot_%2;
    openedLanguage_=lang;
    if(slotReady_[slot]&(1U<<lang)){advanceSlot();return;}
    if(cueIndex<0 || cueIndex>=count_[0]){slotReady_[slot]|=1U<<lang;advanceSlot();return;}
    const auto& original=work_->cues[0][cueIndex];const int pair=int(original.offset>>18)-1;
    if(lang && pair<0){slotReady_[slot]|=1U<<lang;advanceSlot();return;}
    auto& input=windows_[lang];
    if(!input) {
        char path[576];
        if(!lang && alternative_[0])std::snprintf(path,sizeof(path),"%s",alternative_);
        else std::snprintf(path,sizeof(path),"%s%s",base_,!lang?".lrc":(translationHant_?".zh-Hant.lrc":".zh-Hans.lrc"));
        openedLanguage_=lang;ResourceFailure attempt;const auto opened=openResource(input,path,512,attempt);
        if(opened!=MediaState::Loading){failure_=attempt;fail("lrc_window_open");return;}
        return;
    }
    if(!lineStarted_){lineOffset_=(lang?work_->cues[1][pair].offset:original.offset)&kOffsetMask;lineLength_=0;
        if(input.position()!=lineOffset_&&!input.seek(lineOffset_)){failure_.set("lrc_window_seek","seek",errno);fail("lrc_window_seek");return;}
        lineStarted_=true;return;}
    const size_t wanted=std::min<size_t>(512,input.size()-input.position());errno=0;
    const int n=input.read(work_->io,wanted);if(n!=int(wanted)){failure_.set("lrc_window_read","read",errno,wanted,n);fail("lrc_window_read");return;}bytesRead_+=n;bool done=n==0;
    for(int i=0;i<n;++i){const char c=work_->io[i];if(c==0){fail("lrc_nul");return;}if(c=='\n'){done=true;break;}if(c!='\r'){if(lineLength_>=1024){fail("lrc_line_limit");return;}work_->line[lineLength_++]=c;}}
    if(done || input.position()>=input.size()){if(!parseLine(false))return;slotReady_[slot]|=1U<<lang;advanceSlot();++revision_;}
}
uint32_t LyricsTimeline::startMs() const {return current_>=0 && work_ ? work_->cues[0][current_].time : 0;}
uint32_t LyricsTimeline::cueStart(int index) const {return index>=0 && index<count_[0] && work_?work_->cues[0][index].time:0;}
uint32_t LyricsTimeline::cueEnd(int index) const {return index+1<count_[0]?cueStart(index+1):(durationMs_>cueStart(index)?durationMs_:cueStart(index)+10000);}
void LyricsTimeline::failurePath(char* output,size_t size) const {
    if(failureDirectory_){std::snprintf(output,size,"%s",base_);char* slash=std::strrchr(output,'/');if(slash)*slash=0;return;}
    if(!openedLanguage_ && alternative_[0])std::snprintf(output,size,"%s",alternative_);
    else std::snprintf(output,size,"%s%s",base_,!openedLanguage_?".lrc":(translationHant_?".zh-Hant.lrc":".zh-Hans.lrc"));
}
uint32_t LyricsTimeline::endMs() const {
    if(!work_ || !count_[0])return durationMs_;
    if(current_+1<count_[0])return work_->cues[0][current_+1].time;
    return durationMs_>startMs()?durationMs_:startMs()+10000;
}
const char* LyricsTimeline::text(int relative,unsigned language) const {
    const int cue=current_+relative;
    if(work_&&language<2)for(unsigned i=0;i<2;++i)
        if(slotCue_[i]==cue&&(slotReady_[i]&(1U<<language)))return work_->text[i][language];
    return "";
}
bool LyricsTimeline::cueReady(int cue)const{
    if(!work_)return false;
    for(unsigned i=0;i<2;++i)if(slotCue_[i]==cue)return slotReady_[i]==3;
    return false;
}
} }
