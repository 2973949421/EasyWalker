#include "NowPlayingMedia.h"
#include <algorithm>
namespace adv_walkman { namespace player {
void NowPlayingMedia::cancelPreparation(){preparing_=layoutReady_=glyphsReady_=false;if(fonts_)fonts_->clearPins();++cancellations_;}
void NowPlayingMedia::selectTrack(const char* path){
    cancelPreparation();frameInProgress_=false;active_=true;++generation_;timeline_.selectTrack(path);cover_.selectTrack(path);
    shownCoverRevision_=UINT32_MAX;shownCurrent_=-2;shownPage_=255;shownPages_=1;dirty_=true;seek_=true;
}
void NowPlayingMedia::release(){cancelPreparation();timeline_.release();cover_.release();active_=false;frameInProgress_=false;++generation_;}
void NowPlayingMedia::requestRedraw(){dirty_=true;if(frameInProgress_)redrawAfterFrame_=true;else cancelPreparation();}
void NowPlayingMedia::setPreferred(uint8_t value){const auto next=value==1?MediaView::Cover:MediaView::Lyrics;if(next!=preferred_){preferred_=next;seek_=true;++generation_;frameInProgress_=false;cover_.finishFrame();requestRedraw();}}
bool NowPlayingMedia::toggleView(){if(!timeline_.hasLyrics())return false;setPreferred(preferred_==MediaView::Lyrics?1:0);++viewChanges_;return true;}
void NowPlayingMedia::updatePosition(uint32_t position,uint32_t duration,bool paused){
    if(position+1000<positionMs_ || position>positionMs_+1500){seek_=true;++generation_;frameInProgress_=false;requestRedraw();}
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
        if(!dirty_&&shownCurrent_==current&&shownPage_==page){
            if(paused_)return;
            const uint32_t future=positionMs_+2000;
            if(current+1<timeline_.count(0)&&end<=future){target=current+1;position=end;}
            else if(current>=0&&shownPage_+1<shownPages_){
                const uint32_t next=start+(uint64_t(shownPage_+1)*length+shownPages_-1)/shownPages_;
                if(next>future)return;position=next;
            }else return;
        }
        fonts_->clearPins();preparing_=true;layoutReady_=glyphsReady_=false;prepareAt_=micros();
        preparedCue_=target;preparedPosition_=position;
        deadline_=!seek_&&!paused_&&shownCurrent_>=-1;
    }
    if(!layoutReady_){
        if(!renderer_.prepare(timeline_,*fonts_,preparedPosition_,preparedCue_))return;
        layoutReady_=true;const auto s=renderer_.stats();const uint32_t start=timeline_.cueStart(preparedCue_);
        deadline_=deadline_ && (preparedCue_!=shownCurrent_ || s.page!=shownPage_);
        const uint32_t length=std::max<uint32_t>(1,timeline_.cueEnd(preparedCue_)-start);
        preparedDue_=preparedCue_<0?0:start+(uint64_t(s.page)*length+s.pages-1)/s.pages;
        preparedUntil_=start+(uint64_t(s.page+1)*length+s.pages-1)/s.pages;
    }
    if(!glyphsReady_&&renderer_.prepareFrame(*fonts_)){glyphsReady_=true;prepareMaxUs_=std::max<uint32_t>(prepareMaxUs_,micros()-prepareAt_);}
}
void NowPlayingMedia::service(){
    if(!active_||!fonts_||presentingLyrics())return;const uint32_t start=micros();
    const bool lyricsPending=timeline_.state()==MediaState::Loading||(timeline_.hasLyrics()&&!timeline_.windowReady());
    if(!lyricsPending && !frameInProgress_)prepareUpcoming();
    switch(chooseMediaWork(workerTurn_++,fonts_->busy(),lyricsPending,cover_.busy())){
        case MediaWork::Font:fonts_->service();break;
        case MediaWork::Lyrics:timeline_.service();break;
        case MediaWork::Cover:cover_.service();break;
        case MediaWork::None:break;
    }
    serviceMaxUs_=std::max<uint32_t>(serviceMaxUs_,micros()-start);
}
bool NowPlayingMedia::wantsFrame(uint32_t)const{
    if(!active_||frameInProgress_)return false;
    if(effectiveView()==MediaView::Cover)return dirty_||shownCoverRevision_!=cover_.revision();
    return glyphsReady_ && preparedCue_==timeline_.current() && positionMs_>=preparedDue_ && positionMs_<preparedUntil_;
}
bool NowPlayingMedia::beginFrame(uint32_t){
    if(frameInProgress_)return true;frameView_=effectiveView();
    if(frameView_==MediaView::Lyrics){
        if(!glyphsReady_||preparedCue_!=timeline_.current()||positionMs_<preparedDue_||positionMs_>=preparedUntil_)return false;
        fonts_->setPresenting(true);ioAt_=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead();presentAt_=micros();
    }
    dirty_=false;redrawAfterFrame_=false;frameInProgress_=true;++frameId_;shownCoverRevision_=cover_.revision();return true;
}
bool NowPlayingMedia::canPatchOverlay()const{
    if(shownGeneration_!=generation_)return false;
    if(effectiveView()==MediaView::Cover)return shownCoverRevision_==cover_.revision();
    return glyphsReady_ && preparedCue_==shownCurrent_ && renderer_.stats().page==shownPage_;
}
bool NowPlayingMedia::prepareStripe(int y,int){
    if(frameView_==MediaView::Lyrics)return glyphsReady_;
    if(cover_.state()!=MediaState::Ready)return true;
    cover_.requestRow(y);return y<MediaLayout::coverTop||y>=MediaLayout::coverTop+144||cover_.rowReady(y-MediaLayout::coverTop);
}
void NowPlayingMedia::drawStripe(lgfx::LGFXBase& canvas,int y,int height){
    if(frameView_==MediaView::Lyrics)renderer_.drawStripe(canvas,*fonts_,y,height,0);else cover_.drawRow(canvas,y,0x0861);
}
void NowPlayingMedia::endFrame(){
    if(frameView_==MediaView::Lyrics){
        ++lyricsFrames_;
        presentMaxUs_=std::max<uint32_t>(presentMaxUs_,micros()-presentAt_);
        if(ioAt_!=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead())++presentIoViolations_;
        if(deadline_){++deadlineUpdates_;if(positionMs_>preparedDue_)lyricLateMaxMs_=std::max<uint32_t>(lyricLateMaxMs_,positionMs_-preparedDue_);}
        shownCurrent_=preparedCue_;shownPage_=renderer_.stats().page;shownPages_=renderer_.stats().pages;
        preparing_=layoutReady_=glyphsReady_=false;fonts_->clearPins();seek_=false;
    }
    if(frameView_==MediaView::Cover){++coverFrames_;cover_.finishFrame();}
    shownGeneration_=generation_;seek_=false;
    frameInProgress_=false;++frames_;if(redrawAfterFrame_)requestRedraw();
}
NowPlayingMediaStatus NowPlayingMedia::status()const{
    NowPlayingMediaStatus s;s.lyrics=timeline_.state();s.cover=cover_.state();s.preferred=preferred_;s.view=effectiveView();s.current=timeline_.current();
    s.frames=frames_;s.lyricsFrames=lyricsFrames_;s.coverFrames=coverFrames_;s.viewChanges=viewChanges_;s.cancellations=cancellations_;s.reads=timeline_.bytesRead()+cover_.bytesRead();s.serviceMaxUs=serviceMaxUs_;
    s.page=shownPage_==255?0:shownPage_;s.pages=shownPages_;s.layoutError=renderer_.stats().layoutError;s.invalidUtf8=renderer_.stats().invalidUtf8;
    s.generation=generation_;s.prepareMaxUs=prepareMaxUs_;s.presentMaxUs=presentMaxUs_;s.lyricLateMaxMs=lyricLateMaxMs_;s.presentIoViolations=presentIoViolations_;
    s.frameId=frameId_;s.deadlineUpdates=deadlineUpdates_;s.missedDeadlines=missedDeadlines_;
    s.error=timeline_.state()==MediaState::Error?timeline_.error():cover_.error();return s;
}
void NowPlayingMedia::resetDiagnostics(){frames_=lyricsFrames_=coverFrames_=viewChanges_=cancellations_=serviceMaxUs_=0;prepareMaxUs_=presentMaxUs_=lyricLateMaxMs_=presentIoViolations_=0;deadlineUpdates_=missedDeadlines_=0;}
} }
