#include "NowPlayingMedia.h"
#include <algorithm>
#include <cstring>
namespace adv_walkman { namespace player {
const char* NowPlayingMedia::bootSelfCheck(const char* track){
    const char* failure=nullptr;
    if(!track||!*track){release();return nullptr;}
    selectTrack(track);
    const uint32_t loadAt=millis();
    while(timeline_.state()==MediaState::Loading&&millis()-loadAt<5000){service();delay(1);}
    if(timeline_.state()==MediaState::Missing){release();return nullptr;}
    for(unsigned test=0;test<3;++test){
        const uint32_t position=test==0?0:timeline_.cueStart(test==1?0:std::min<int>(1,timeline_.count(0)-1));
        updatePosition(position,0,true);const uint32_t start=millis();bool ready=false;
        while(millis()-start<5000){
            service();
            if(timeline_.state()==MediaState::Error||timeline_.state()==MediaState::Missing){failure="boot_lyrics_resource";break;}
            if(timeline_.windowReady() && renderer_.prepare(timeline_,*fonts_,position)){
                const auto s=renderer_.stats();ready=!s.layoutError&&!s.invalidUtf8&&s.pages>=1;
                if(position<timeline_.cueStart(0))ready&=timeline_.current()==-1&&s.page==0&&s.intro;
                else ready&=timeline_.current()>=0;
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
void NowPlayingMedia::abortFrame(){cover_.cancelBand();if(fonts_)fonts_->setPresenting(false);frameInProgress_=false;}
void NowPlayingMedia::selectTrack(const char* path){
    if(path&&!std::strcmp(track_,path)){
        if(timeline_.state()==MediaState::Loading)timeline_.selectTrack(path);
        active_=true;dirty_=true;seek_=true;shownGeneration_=UINT32_MAX;return;
    }
    std::snprintf(track_,sizeof(track_),"%s",path?path:"");
    cancelPreparation();abortFrame();active_=true;++generation_;timeline_.selectTrack(path);cover_.selectTrack(path);
    if(fonts_)fonts_->clearPins();
    shownCoverRevision_=UINT32_MAX;shownCurrent_=-2;shownPage_=255;shownPages_=1;dirty_=true;seek_=true;
    transition_.pending=false;transition_.requested=preferred_;transition_.displayed=MediaView::Cover;
}
void NowPlayingMedia::release(){cancelPreparation();if(fonts_)fonts_->clearPins();timeline_.release();cover_.release();track_[0]=0;active_=false;frameInProgress_=false;++generation_;}
void NowPlayingMedia::suspend(){
    if(suspendStep_)return;
    transition_.cancel();cancelPreparation();if(fonts_)fonts_->clearPins();
    cover_.cancelBand();active_=false;frameInProgress_=false;shownGeneration_=UINT32_MAX;
    ++generation_;suspendStep_=1;
}
bool NowPlayingMedia::serviceSuspension(){
    switch(suspendStep_){
    case 1:if(!fonts_||fonts_->suspendOne())++suspendStep_;break;
    case 2:if(timeline_.suspendOne())++suspendStep_;break;
    case 3:cover_.suspend();suspendStep_=0;break;
    default:return false;
    }
    return true;
}
void NowPlayingMedia::requestRedraw(){dirty_=true;if(frameInProgress_)redrawAfterFrame_=true;}
void NowPlayingMedia::setPreferred(uint8_t value){
    if(transition_.pending)return;
    const auto next=value==1?MediaView::Cover:MediaView::Lyrics;
    if(next!=preferred_){preferred_=next;transition_.requested=next;requestRedraw();}
}
bool NowPlayingMedia::toggleView(){
    const auto before=transition_.generation;
    if(!transition_.toggle(timeline_.hasLyrics()))return false;
    ++viewChanges_;
    if(before==transition_.generation)return true;
    transitionAt_=transitionProgressAt_=millis();transitionProgress_=0;
    viewReadyMs_=viewFirstStripeMs_=viewCompletedMs_=0;
    transitionWarm_=transition_.requested==MediaView::Cover?cover_.state()==MediaState::Ready:
        (canPatchOverlay()||glyphsReady_);
    requestRedraw();return true;
}
void NowPlayingMedia::updatePosition(uint32_t position,uint32_t duration,bool paused){
    if(position+1000<positionMs_ || position>positionMs_+1500){seek_=true;++generation_;abortFrame();cancelPreparation();requestRedraw();}
    positionMs_=position;durationMs_=duration;paused_=paused;
    if(active_&&!presentingLyrics())timeline_.updatePosition(position,duration);
}
void NowPlayingMedia::prepareUpcoming(){
    if(!timeline_.windowReady())return;
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
    if(transition_.pending){
        const uint32_t progress=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead()+uint32_t(layoutReady_)+uint32_t(glyphsReady_);
        if(progress!=transitionProgress_){transitionProgress_=progress;transitionProgressAt_=millis();}
        const bool failed=transition_.requested==MediaView::Lyrics?timeline_.state()==MediaState::Error:cover_.state()==MediaState::Error;
        if(failed||millis()-transitionProgressAt_>=5000){
            if(!viewFailures_)viewFailure_=failed?(transition_.requested==MediaView::Lyrics?timeline_.error():cover_.error()):"view_prepare_no_progress_5s";
            transition_.cancel();++viewFailures_;dirty_=false;}
    }
    // Keep the current lyric layout resident even while its view is hidden.
    if(effectiveView()==MediaView::Cover&&glyphsReady_&&preparedCue_==timeline_.current()&&positionMs_>=preparedDue_&&positionMs_<preparedUntil_){
        displayedRenderer_=renderer_;fonts_->promotePins();shownCurrent_=preparedCue_;
        shownPage_=renderer_.stats().page;shownPages_=renderer_.stats().pages;shownGeneration_=generation_;
        preparing_=layoutReady_=glyphsReady_=false;
    }
    const bool lyricsPending=timeline_.busy();
    preparationTurn_=!preparationTurn_;
    // An owned picture stripe must finish before hidden lyric prefetch can
    // consume another turn. Its buffer cannot be borrowed by chrome either.
    if(cover_.bandActive()){cover_.service();const auto elapsed=micros()-start;
        if(elapsed>serviceMaxUs_){serviceMaxUs_=elapsed;peakWork_=static_cast<uint8_t>(MediaWork::Cover);}return;}
    if(preparationTurn_&&!frameInProgress_&&timeline_.windowReady()&&!fonts_->busy()&&
       !(transition_.pending&&transition_.requested==MediaView::Cover)){
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
    if(effectiveView()==MediaView::Cover)return (!transition_.pending||cover_.state()!=MediaState::Loading)&&(dirty_||shownCoverRevision_!=cover_.revision());
    return (dirty_&&canPatchOverlay()) || (glyphsReady_ && preparedCue_==timeline_.current() && positionMs_>=preparedDue_ && positionMs_<preparedUntil_);
}
bool NowPlayingMedia::beginFrame(uint32_t){
    if(frameInProgress_)return true;frameView_=effectiveView();
    frameCoverReady_=frameView_==MediaView::Cover&&cover_.state()==MediaState::Ready;
    if(frameView_==MediaView::Cover&&transition_.pending&&cover_.state()==MediaState::Loading)return false;
    if(frameView_==MediaView::Lyrics){
        frameFromPrepared_=glyphsReady_&&preparedCue_==timeline_.current()&&positionMs_>=preparedDue_&&positionMs_<preparedUntil_;
        if(frameFromPrepared_){displayedRenderer_=renderer_;fonts_->promotePins();}
        else if(!dirty_||!canPatchOverlay())return false;
        fonts_->setPresenting(true);ioAt_=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead();presentAt_=micros();
    }
    if(transition_.pending&&frameView_==transition_.requested)viewReadyMs_=millis();
    dirty_=false;redrawAfterFrame_=false;frameInProgress_=true;++frameId_;shownCoverRevision_=cover_.revision();return true;
}
bool NowPlayingMedia::canPatchOverlay()const{
    if(shownGeneration_!=generation_)return false;
    if(effectiveView()==MediaView::Cover)return !transition_.pending&&shownCoverRevision_==cover_.revision();
    if(shownCurrent_!=timeline_.current())return false;
    const uint32_t start=timeline_.startMs(),length=std::max<uint32_t>(1,timeline_.endMs()-start);
    const unsigned page=shownCurrent_<0?0:std::min<unsigned>(shownPages_-1,uint64_t(positionMs_>start?positionMs_-start:0)*shownPages_/length);
    return page==shownPage_;
}
bool NowPlayingMedia::prepareStripe(lgfx::LGFXBase& canvas,int y,int height){
    if(frameView_==MediaView::Lyrics)return true;
    if(!frameCoverReady_)return true;
    return cover_.prepareBand(canvas,y,height);
}
void NowPlayingMedia::drawStripe(lgfx::LGFXBase& canvas,int y,int height){
    if(frameView_==MediaView::Lyrics)displayedRenderer_.drawStripe(canvas,*fonts_,y,height,0);else cover_.drawRow(canvas,y,0x0861);
}
void NowPlayingMedia::endFrame(bool partial){
    const bool complete=!partial&&(frameView_==MediaView::Lyrics||frameCoverReady_||cover_.state()==MediaState::Missing);
    if(frameView_==MediaView::Lyrics){
        if(!partial)++lyricsFrames_;
        if(!partial)presentMaxUs_=std::max<uint32_t>(presentMaxUs_,micros()-presentAt_);
        if(ioAt_!=fonts_->stats().reads+timeline_.bytesRead()+cover_.bytesRead())++presentIoViolations_;
        if(frameFromPrepared_){
            if(deadline_){++deadlineUpdates_;if(positionMs_>preparedDue_)lyricLateMaxMs_=std::max<uint32_t>(lyricLateMaxMs_,positionMs_-preparedDue_);}
            lastDue_=preparedDue_;lastPrepared_=preparedReadyAt_;lastSubmitted_=positionMs_;
            shownCurrent_=preparedCue_;shownPage_=displayedRenderer_.stats().page;shownPages_=displayedRenderer_.stats().pages;
            preparing_=layoutReady_=glyphsReady_=false;
        }
        fonts_->setPresenting(false);fonts_->clearPins(FontCache::Ui);seek_=false;
    }
    if(frameView_==MediaView::Cover){if(!partial){if(frameCoverReady_)++coverFrames_;else ++fallbackFrames_;}cover_.finishFrame();}
    if(complete&&transition_.pending&&frameView_==transition_.requested){
        auto& maximum=transitionWarm_?viewWarmMaxMs_:viewColdMaxMs_;
        maximum=std::max<uint32_t>(maximum,millis()-transitionAt_);preferred_=frameView_;
        viewCompletedMs_=millis();
        if(transitionWarm_)++viewWarmCompleted_;else ++viewColdCompleted_;
    }
    if(complete)transition_.commit(frameView_);
    shownGeneration_=generation_;seek_=false;
    frameInProgress_=false;if(partial)++patchFrames_;else if(complete)++frames_;if(redrawAfterFrame_)requestRedraw();
}
NowPlayingMediaStatus NowPlayingMedia::status()const{
    NowPlayingMediaStatus s;s.lyrics=timeline_.state();s.cover=cover_.state();s.preferred=preferred_;s.view=transition_.displayed;s.current=timeline_.current();
    s.viewPending=transition_.pending;s.viewCoalesced=transition_.coalesced;s.viewWarmMaxMs=viewWarmMaxMs_;s.viewColdMaxMs=viewColdMaxMs_;s.viewFailures=viewFailures_;
    s.viewWarmCompleted=viewWarmCompleted_;s.viewColdCompleted=viewColdCompleted_;s.viewFailure=viewFailure_;
    s.viewRequestedMs=transitionAt_;s.viewReadyMs=viewReadyMs_;s.viewFirstStripeMs=viewFirstStripeMs_;s.viewCompletedMs=viewCompletedMs_;
    s.frames=frames_;s.lyricsFrames=lyricsFrames_;s.coverFrames=coverFrames_;s.viewChanges=viewChanges_;s.cancellations=cancellations_;s.reads=timeline_.bytesRead()+cover_.bytesRead();s.serviceMaxUs=serviceMaxUs_;
    s.patchFrames=patchFrames_;s.fallbackFrames=fallbackFrames_;
    s.page=shownPage_==255?0:shownPage_;s.pages=shownPages_;s.layoutError=renderer_.stats().layoutError;s.invalidUtf8=renderer_.stats().invalidUtf8;
    s.generation=generation_;s.peakWork=peakWork_;s.prepareMaxUs=prepareMaxUs_;s.presentMaxUs=presentMaxUs_;s.lyricLateMaxMs=lyricLateMaxMs_;s.presentIoViolations=presentIoViolations_;
    s.frameId=frameId_;s.deadlineUpdates=deadlineUpdates_;s.missedDeadlines=missedDeadlines_;
    s.lyricDueMs=lastDue_;s.lyricPreparedMs=lastPrepared_;s.lyricSubmittedMs=lastSubmitted_;s.coverOpens=cover_.opens();
    s.error=timeline_.state()==MediaState::Error?timeline_.error():cover_.error();return s;
}
void NowPlayingMedia::resetDiagnostics(){frames_=lyricsFrames_=coverFrames_=patchFrames_=fallbackFrames_=viewChanges_=cancellations_=serviceMaxUs_=0;prepareMaxUs_=presentMaxUs_=lyricLateMaxMs_=presentIoViolations_=0;deadlineUpdates_=missedDeadlines_=0;viewWarmMaxMs_=viewColdMaxMs_=viewFailures_=viewWarmCompleted_=viewColdCompleted_=transition_.coalesced=0;viewFailure_="none";}
} }
