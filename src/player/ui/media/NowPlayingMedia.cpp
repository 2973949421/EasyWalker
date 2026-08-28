#include "NowPlayingMedia.h"
#include <algorithm>
#include <cstring>
namespace adv_walkman { namespace player {
const char* NowPlayingMedia::bootSelfCheck(){
    const char* failure=nullptr;
    selectTrack("/Music/ADVWalkmanBenchmark/benchmark.mp3");
    for(uint32_t position:{0U,40600U,130000U}){
        updatePosition(position,299260,true);const uint32_t start=millis();bool ready=false;
        while(millis()-start<5000){
            service();
            if(timeline_.state()==MediaState::Error||timeline_.state()==MediaState::Missing){failure="boot_lyrics_resource";break;}
            if(timeline_.windowReady() && renderer_.prepare(timeline_,*fonts_,position)){
                const auto s=renderer_.stats();ready=!s.layoutError&&!s.invalidUtf8&&s.pages>=1;
                if(position==0)ready&=timeline_.current()==-1&&s.glyphs>0&&s.page==0&&s.intro;
                else ready&=timeline_.current()>=0&&s.glyphs>0;
                break;
            }
            delay(1);
        }
        if(failure||!ready){if(!failure)failure="boot_media_position_layout";break;}
    }
    const auto generation=generation_;release();
    if(!failure&&(active_||frameInProgress_||generation_<=generation))failure="boot_media_cancel";
    resetDiagnostics();return failure;
}
void NowPlayingMedia::cancelPreparation(){preparing_=layoutReady_=glyphsReady_=false;if(fonts_)fonts_->clearPins(FontCache::Next);++cancellations_;}
void NowPlayingMedia::selectTrack(const char* path){
    if(path&&!std::strcmp(track_,path)){
        if(timeline_.state()==MediaState::Loading)timeline_.selectTrack(path);
        active_=true;dirty_=true;seek_=true;shownGeneration_=UINT32_MAX;return;
    }
    std::snprintf(track_,sizeof(track_),"%s",path?path:"");
    cancelPreparation();frameInProgress_=false;active_=true;++generation_;timeline_.selectTrack(path);cover_.selectTrack(path);
    if(fonts_)fonts_->clearPins();
    shownCoverRevision_=UINT32_MAX;shownCurrent_=-2;shownPage_=255;shownPages_=1;dirty_=true;seek_=true;
}
void NowPlayingMedia::release(){cancelPreparation();if(fonts_)fonts_->clearPins();timeline_.release();cover_.release();track_[0]=0;active_=false;frameInProgress_=false;++generation_;}
void NowPlayingMedia::suspend(){cancelPreparation();if(fonts_){fonts_->clearPins();fonts_->suspend();}timeline_.suspend();cover_.suspend();active_=false;frameInProgress_=false;shownGeneration_=UINT32_MAX;}
void NowPlayingMedia::requestRedraw(){dirty_=true;if(frameInProgress_)redrawAfterFrame_=true;}
void NowPlayingMedia::setPreferred(uint8_t value){const auto next=value==1?MediaView::Cover:MediaView::Lyrics;if(next!=preferred_){preferred_=next;seek_=true;++generation_;frameInProgress_=false;cancelPreparation();if(fonts_)fonts_->setPresenting(false);cover_.finishFrame();requestRedraw();}}
bool NowPlayingMedia::toggleView(){if(!timeline_.hasLyrics())return false;setPreferred(preferred_==MediaView::Lyrics?1:0);++viewChanges_;return true;}
void NowPlayingMedia::updatePosition(uint32_t position,uint32_t duration,bool paused){
    if(position+1000<positionMs_ || position>positionMs_+1500){seek_=true;++generation_;frameInProgress_=false;cancelPreparation();if(fonts_)fonts_->setPresenting(false);requestRedraw();}
    positionMs_=position;durationMs_=duration;paused_=paused;
    if(active_&&!presentingLyrics())timeline_.updatePosition(position,duration);
}
void NowPlayingMedia::prepareUpcoming(){
    if(!timeline_.windowReady()||effectiveView()!=MediaView::Lyrics)return;
    const int current=timeline_.current();
    if(preparing_ && (preparedCue_<current || preparedCue_>current+1 || (layoutReady_&&positionMs_>=preparedUntil_))){
        if(deadline_)++missedDeadlines_;
        cancelPreparation();
    }
    if(!preparing_){
        int target=current;uint32_t position=positionMs_;
        const uint32_t start=timeline_.startMs(),end=timeline_.endMs(),length=std::max<uint32_t>(1,end-start);
        const unsigned page=current<0?0:std::min<unsigned>(shownPages_-1,uint64_t(positionMs_>start?positionMs_-start:0)*shownPages_/length);
        if(shownGeneration_==generation_&&shownCurrent_==current&&shownPage_==page){
            if(paused_)return;
            if(current>=0&&shownPage_+1<shownPages_){
                const uint32_t next=start+(uint64_t(shownPage_+1)*length+shownPages_-1)/shownPages_;
                position=next;
            }else if(current+1<timeline_.count(0)){target=current+1;position=end;}
            else return;
        }
        fonts_->clearPins(FontCache::Next);preparing_=true;layoutReady_=glyphsReady_=false;prepareAt_=micros();
        preparedCue_=target;preparedPosition_=position;
        deadline_=!seek_&&!paused_&&shownCurrent_>=-1;
    }
    if(!layoutReady_){
        if(!renderer_.prepare(timeline_,*fonts_,preparedPosition_,preparedCue_))return;
        layoutReady_=true;const auto s=renderer_.stats();const uint32_t start=timeline_.cueStart(preparedCue_);
        deadline_=deadline_ && (preparedCue_!=shownCurrent_ || s.page!=shownPage_);
        const uint32_t length=std::max<uint32_t>(1,timeline_.cueEnd(preparedCue_)-start);
        preparedDue_=preparedCue_<0?0:start+(uint64_t(s.page)*length+s.pages-1)/s.pages;
        preparedUntil_=preparedCue_<0?timeline_.cueStart(0):start+(uint64_t(s.page+1)*length+s.pages-1)/s.pages;
    }
    if(!glyphsReady_&&renderer_.prepareFrame(*fonts_)){glyphsReady_=true;preparedReadyAt_=positionMs_;prepareMaxUs_=std::max<uint32_t>(prepareMaxUs_,micros()-prepareAt_);}
}
void NowPlayingMedia::service(){
    if(!active_||!fonts_||presentingLyrics())return;const uint32_t start=micros();
    const bool lyricsPending=timeline_.busy();
    preparationTurn_=!preparationTurn_;
    if(preparationTurn_&&!frameInProgress_&&timeline_.windowReady()&&!fonts_->busy()){
        prepareUpcoming();const auto elapsed=micros()-start;
        if(elapsed>serviceMaxUs_){serviceMaxUs_=elapsed;peakWork_=4;}return;
    }
    const bool dueLyrics=effectiveView()==MediaView::Lyrics && (!timeline_.windowReady() ||
        (preparing_&&!glyphsReady_&&preparedCue_==timeline_.current()&&positionMs_>=preparedPosition_));
    const auto work=chooseMediaWork(workerTurn_++,fonts_->busy(),lyricsPending,cover_.busy(),dueLyrics);
    switch(work){
        case MediaWork::Font:fonts_->service();break;
        case MediaWork::Lyrics:timeline_.service();break;
        case MediaWork::Cover:cover_.service();break;
        case MediaWork::None:break;
    }
    const auto elapsed=micros()-start;if(elapsed>serviceMaxUs_){serviceMaxUs_=elapsed;peakWork_=static_cast<uint8_t>(work);}
}
bool NowPlayingMedia::wantsFrame(uint32_t)const{
    if(!active_||frameInProgress_)return false;
    if(effectiveView()==MediaView::Cover)return dirty_||shownCoverRevision_!=cover_.revision();
    return (dirty_&&canPatchOverlay()) || (glyphsReady_ && preparedCue_==timeline_.current() && positionMs_>=preparedDue_ && positionMs_<preparedUntil_);
}
bool NowPlayingMedia::beginFrame(uint32_t){
    if(frameInProgress_)return true;frameView_=effectiveView();
    if(frameView_==MediaView::Lyrics){
        frameFromPrepared_=glyphsReady_&&preparedCue_==timeline_.current()&&positionMs_>=preparedDue_&&positionMs_<preparedUntil_;
        if(frameFromPrepared_){displayedRenderer_=renderer_;fonts_->promotePins();}
        else if(!dirty_||!canPatchOverlay())return false;
        fonts_->setPresenting(true);ioAt_=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead();presentAt_=micros();
    }
    dirty_=false;redrawAfterFrame_=false;frameInProgress_=true;++frameId_;shownCoverRevision_=cover_.revision();return true;
}
bool NowPlayingMedia::canPatchOverlay()const{
    if(shownGeneration_!=generation_)return false;
    if(effectiveView()==MediaView::Cover)return shownCoverRevision_==cover_.revision();
    if(shownCurrent_!=timeline_.current())return false;
    const uint32_t start=timeline_.startMs(),length=std::max<uint32_t>(1,timeline_.endMs()-start);
    const unsigned page=shownCurrent_<0?0:std::min<unsigned>(shownPages_-1,uint64_t(positionMs_>start?positionMs_-start:0)*shownPages_/length);
    return page==shownPage_;
}
bool NowPlayingMedia::prepareStripe(int y,int){
    if(frameView_==MediaView::Lyrics)return true;
    if(cover_.state()!=MediaState::Ready)return true;
    cover_.requestRow(y);return y<cover_.top()||y>=cover_.top()+cover_.height()||cover_.rowReady(y-cover_.top());
}
void NowPlayingMedia::drawStripe(lgfx::LGFXBase& canvas,int y,int height){
    if(frameView_==MediaView::Lyrics)displayedRenderer_.drawStripe(canvas,*fonts_,y,height,0);else cover_.drawRow(canvas,y,0x0861);
}
void NowPlayingMedia::endFrame(){
    if(frameView_==MediaView::Lyrics){
        ++lyricsFrames_;
        presentMaxUs_=std::max<uint32_t>(presentMaxUs_,micros()-presentAt_);
        if(ioAt_!=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead())++presentIoViolations_;
        if(frameFromPrepared_){
            if(deadline_){++deadlineUpdates_;if(positionMs_>preparedDue_)lyricLateMaxMs_=std::max<uint32_t>(lyricLateMaxMs_,positionMs_-preparedDue_);}
            lastDue_=preparedDue_;lastPrepared_=preparedReadyAt_;lastSubmitted_=positionMs_;
            shownCurrent_=preparedCue_;shownPage_=displayedRenderer_.stats().page;shownPages_=displayedRenderer_.stats().pages;
            preparing_=layoutReady_=glyphsReady_=false;
        }
        fonts_->setPresenting(false);fonts_->clearPins(FontCache::Ui);seek_=false;
    }
    if(frameView_==MediaView::Cover){++coverFrames_;cover_.finishFrame();}
    shownGeneration_=generation_;seek_=false;
    frameInProgress_=false;++frames_;if(redrawAfterFrame_)requestRedraw();
}
NowPlayingMediaStatus NowPlayingMedia::status()const{
    NowPlayingMediaStatus s;s.lyrics=timeline_.state();s.cover=cover_.state();s.preferred=preferred_;s.view=effectiveView();s.current=timeline_.current();
    s.frames=frames_;s.lyricsFrames=lyricsFrames_;s.coverFrames=coverFrames_;s.viewChanges=viewChanges_;s.cancellations=cancellations_;s.reads=timeline_.bytesRead()+cover_.bytesRead();s.serviceMaxUs=serviceMaxUs_;
    s.page=shownPage_==255?0:shownPage_;s.pages=shownPages_;s.layoutError=renderer_.stats().layoutError;s.invalidUtf8=renderer_.stats().invalidUtf8;
    s.generation=generation_;s.peakWork=peakWork_;s.prepareMaxUs=prepareMaxUs_;s.presentMaxUs=presentMaxUs_;s.lyricLateMaxMs=lyricLateMaxMs_;s.presentIoViolations=presentIoViolations_;
    s.frameId=frameId_;s.deadlineUpdates=deadlineUpdates_;s.missedDeadlines=missedDeadlines_;
    s.lyricDueMs=lastDue_;s.lyricPreparedMs=lastPrepared_;s.lyricSubmittedMs=lastSubmitted_;s.coverOpens=cover_.opens();
    s.error=timeline_.state()==MediaState::Error?timeline_.error():cover_.error();return s;
}
void NowPlayingMedia::resetDiagnostics(){frames_=lyricsFrames_=coverFrames_=viewChanges_=cancellations_=serviceMaxUs_=0;prepareMaxUs_=presentMaxUs_=lyricLateMaxMs_=presentIoViolations_=0;deadlineUpdates_=missedDeadlines_=0;}
} }
