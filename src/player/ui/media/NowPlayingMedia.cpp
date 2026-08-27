#include "NowPlayingMedia.h"
#include <algorithm>
namespace adv_walkman { namespace player {
void NowPlayingMedia::selectTrack(const char* path){
    if(active_)++cancellations_;active_=true;timeline_.selectTrack(path);cover_.selectTrack(path);
    shownLyricRevision_=shownCoverRevision_=UINT32_MAX;shownCurrent_=-2;shownPage_=255;dirty_=true;frameInProgress_=false;animationStartedAtMs_=0;
}
void NowPlayingMedia::release(){timeline_.release();cover_.release();active_=false;frameInProgress_=false;++cancellations_;}
void NowPlayingMedia::service(){
    if(!active_ || !fonts_)return;const uint32_t start=micros();
    // Exactly one resource worker per pass; font lookups requested by the
    // visible stripe have priority, without making display functions do I/O.
    if(fonts_->busy())fonts_->service();
    else if(frameInProgress_ && frameView_==MediaView::Cover)cover_.service();
    else if(timeline_.state()==MediaState::Loading || (timeline_.hasLyrics()&&!timeline_.windowReady()))timeline_.service();
    else cover_.service();
    serviceMaxUs_=std::max<uint32_t>(serviceMaxUs_,micros()-start);
}
void NowPlayingMedia::updatePosition(uint32_t positionMs,uint32_t durationMs,bool paused){
    if(positionMs+1000<positionMs_ || positionMs>positionMs_+1500)seek_=true;
    positionMs_=positionMs;durationMs_=durationMs;paused_=paused;
    if(active_)timeline_.updatePosition(positionMs,durationMs);
}
void NowPlayingMedia::setPreferred(uint8_t value){
    const auto next=value==1?MediaView::Cover:MediaView::Lyrics;if(next!=preferred_){preferred_=next;dirty_=true;}
}
bool NowPlayingMedia::toggleView(){
    if(!timeline_.hasLyrics())return false;
    preferred_=preferred_==MediaView::Lyrics?MediaView::Cover:MediaView::Lyrics;++viewChanges_;dirty_=true;return true;
}
bool NowPlayingMedia::wantsFrame(uint32_t nowMs) const {
    if(!active_ || frameInProgress_)return false;
    if(dirty_ || shownLyricRevision_!=timeline_.revision() || shownCoverRevision_!=cover_.revision())return true;
    if(effectiveView()!=MediaView::Lyrics || !timeline_.windowReady())return false;
    if(animationStartedAtMs_ && nowMs-frameAtMs_>=84)return true;
    // Pagination uses the Player position, not wall time. Pause therefore
    // cannot advance pages. The cheap check never touches the files.
    if(renderer_.stats().pages>1){
        const uint32_t length=std::max<uint32_t>(1,timeline_.endMs()-timeline_.startMs());
        const uint32_t elapsed=positionMs_>timeline_.startMs()?positionMs_-timeline_.startMs():0;
        const unsigned page=std::min<unsigned>(renderer_.stats().pages-1,uint64_t(elapsed)*renderer_.stats().pages/length);
        if(page!=shownPage_)return true;
    }
    return false;
}
bool NowPlayingMedia::beginFrame(uint32_t nowMs){
    if(frameInProgress_)return true;
    frameView_=effectiveView();
    if(frameView_==MediaView::Lyrics){
        if(!timeline_.windowReady() || !renderer_.prepare(timeline_,*fonts_,positionMs_))return false;
        const int current=timeline_.current();
        if(current!=shownCurrent_){
            animationStartedAtMs_=!seek_ && !paused_ && current==shownCurrent_+1 && shownCurrent_>=0?nowMs:0;
            shownCurrent_=current;
        }
        frameShift_=0;
        if(animationStartedAtMs_){const uint32_t elapsed=nowMs-animationStartedAtMs_;if(elapsed>=200)animationStartedAtMs_=0;else frameShift_=int(18*(200-elapsed)/200);}
        shownPage_=renderer_.stats().page;
    }
    seek_=false;dirty_=false;shownLyricRevision_=timeline_.revision();shownCoverRevision_=cover_.revision();
    frameAtMs_=nowMs;frameInProgress_=true;return true;
}
bool NowPlayingMedia::prepareStripe(int y,int height){
    if(frameView_==MediaView::Lyrics)return renderer_.prepareStripe(*fonts_,y,height,frameShift_);
    cover_.requestRow(y);return y<12 || y>=156 || cover_.rowReady(y-12);
}
void NowPlayingMedia::drawStripe(lgfx::LGFXBase& canvas,int y,int height){
    if(frameView_==MediaView::Lyrics)renderer_.drawStripe(canvas,*fonts_,y,height,frameShift_);
    else cover_.drawRow(canvas,y,0x0861);
}
void NowPlayingMedia::endFrame(){frameInProgress_=false;++frames_;}
NowPlayingMediaStatus NowPlayingMedia::status() const {
    NowPlayingMediaStatus s;s.lyrics=timeline_.state();s.cover=cover_.state();s.preferred=preferred_;s.view=effectiveView();s.current=timeline_.current();
    s.frames=frames_;s.viewChanges=viewChanges_;s.cancellations=cancellations_;s.reads=timeline_.bytesRead()+cover_.bytesRead();s.serviceMaxUs=serviceMaxUs_;
    s.page=renderer_.stats().page;s.pages=renderer_.stats().pages;s.layoutError=renderer_.stats().layoutError;s.invalidUtf8=renderer_.stats().invalidUtf8;
    s.error=timeline_.state()==MediaState::Error?timeline_.error():cover_.error();return s;
}
} }
