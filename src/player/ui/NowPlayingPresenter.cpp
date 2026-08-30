#include "NowPlayingPresenter.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "P4Controls.h"
#include "UiTextLayout.h"

namespace adv_walkman {
namespace player {
namespace {
using G = NowPlayingGeometry;
constexpr uint16_t kBackground = 0x0861;
constexpr uint16_t kPanel = 0x10E3;
constexpr uint16_t kAccent = 0xFBE0;
constexpr uint16_t kMuted = 0x8410;
constexpr uint16_t kText = 0xFFFF;
constexpr float kTitleSize = 1.0f;
constexpr float kSmallSize = 1.0f;

void formatTime(char* output, size_t capacity, uint32_t milliseconds) {
    const uint32_t seconds = milliseconds / 1000U;
    std::snprintf(output, capacity, "%lu:%02lu",
                  static_cast<unsigned long>(seconds / 60U),
                  static_cast<unsigned long>(seconds % 60U));
}

void drawPlaybackModeIcon(M5Canvas& row, PlaybackModeIcon icon) {
    const uint16_t color = icon == PlaybackModeIcon::Invalid ? TFT_ORANGE : kMuted;
    // User-defined visual language: one compact loop frame, left arrow up,
    // right arrow down; 1/R/S distinguishes one/random/single-pass.
    row.drawFastHLine(104, 2, 8, color);
    row.drawFastVLine(103, 4, 9, color);
    row.drawLine(101, 5, 103, 2, color);
    row.drawLine(105, 5, 103, 2, color);
    row.drawFastHLine(104, 14, 8, color);
    row.drawFastVLine(112, 3, 9, color);
    row.drawLine(110, 11, 112, 14, color);
    row.drawLine(114, 11, 112, 14, color);
    const char label = playbackModeIconLabel(icon);
    if (label) {
        char text[2] = {label, '\0'};
        row.drawString(text, 106, 4);
    }
}
}  // namespace

void NowPlayingPresenter::begin() {
    row_.setBuffer(pixels_, G::width, G::rowHeight, 16);
    row_.setTextWrap(false);
}

const char* NowPlayingPresenter::bootFontSelfCheck(M5GFX& display){
    if(!fonts_)return "font_cache_unbound";
    const uint32_t started=millis();
    while(!fonts_->requestUiWindow("ADVWalkmanBenchmark",12,0,220,1)){
        fonts_->service();if(millis()-started>5000)return "font_warmup_timeout";delay(1);
    }
    prepareRow(18,kBackground,1);CachedUiFont font(fonts_,12);row_.setFont(&font);
    const auto* previousFont=display.getFont();
    display.setFont(&font);display.setTextSize(1);
    const auto layout=UiTextLayout::measure(display,"ADVWalkmanBenchmark",{19,76,97,38,2,3,true});
    display.setFont(previousFont);
    const auto invalid=UiTextLayout::measure(row_,"ADVWalkmanBenchmark",{19,76,97,38,2,3,true});
    UiTextLayout::draw(row_,"ADVWalkman",{6,0,123,18,1,0,false});
    bool ink=false;for(unsigned i=0;i<G::width*18;++i)ink|=row_.readPixel(i%G::width,i/G::width)!=kBackground;
    row_.setFont(&fonts::Font0);
    return layout.lineCount==2&&!layout.truncated&&!layout.layoutError&&layout.maxLineWidthPx<=97&&invalid.layoutError&&ink?nullptr:"actual_font_layout";
}

void NowPlayingPresenter::setActive(bool active, uint32_t nowMs) {
    if(active&&media_.suspending())return;
    if (model_.active == active) return;
    cancelFrame();repairAttempts_=0;renderHalted_=false;
    model_.setActive(active, nowMs);
    if (active) {
        clearPage_ = true;
        contentRow_ = overlayRow_ = 0;
        if (fonts_ && model_.path[0]) media_.selectTrack(model_.path);
    } else if (fonts_) {
        media_.suspend();
    }
}

void NowPlayingPresenter::measureTitle(uint32_t nowMs) {
    row_.setTextSize(kTitleSize);
    CachedUiFont font(fonts_,14);if(fonts_)row_.setFont(&font);
    model_.setTitleWidth(UiTextLayout::singleLineWidth(row_, model_.title), nowMs);
    row_.setFont(&fonts::Font0);
}

void NowPlayingPresenter::update(const PlayerSnapshot& snapshot,
                                  const char* path, LibraryRuntime& library,
                                  uint32_t nowMs) {
    if (model_.setTrack(path, nowMs)) {
        cancelFrame();repairAttempts_=0;renderHalted_=false;titleMeasured_=false;
        measureTitle(nowMs);
        if (fonts_ && model_.path[0] && model_.active) {media_.selectTrack(model_.path);contentRow_=0;}
    }
    model_.updatePlayback(snapshot.state, snapshot.positionMs, snapshot.durationMs,
                           snapshot.repeatMode, snapshot.shuffleEnabled);
    if (!model_.active) return;
    // Only the visible client requests the shared reader. A Playlist request
    // can replace an in-flight one; keyed results and the model's copy prevent
    // stale metadata from being painted on another track.
    if (model_.path[0] != '\0' &&
        (model_.metadataState == DisplayMetadataState::Fallback ||
         (model_.metadataState == DisplayMetadataState::Pending &&
          std::strcmp(library.metadataRequestPath(), model_.path) != 0))) {
        const auto result = library.requestMetadataPath(model_.path);
        model_.metadataState = result == LibraryResult::Error
            ? DisplayMetadataState::Error : DisplayMetadataState::Pending;
    }
    if (model_.metadataState == DisplayMetadataState::Pending &&
        std::strcmp(library.metadataRequestPath(), model_.path) == 0) {
        Mp3Metadata metadata;
        const auto status = library.metadataStatus();
        if (library.metadataForPath(model_.path, metadata)) {
            const bool validTags = status.error == Mp3MetadataError::None;
            model_.applyMetadata(model_.path,
                                  validTags && metadata.title.present && !metadata.titleFromFilename
                                      ? metadata.title.value : nullptr,
                                  validTags && metadata.artist.present ? metadata.artist.value : "", nowMs);
            model_.metadataWarning = static_cast<uint8_t>(status.error);
            titleMeasured_=false;
            measureTitle(nowMs);
        } else if (status.state == Mp3MetadataState::Error) {
            model_.metadataState = DisplayMetadataState::Error;
            model_.metadataWarning = static_cast<uint8_t>(status.error);
        }
    }
    const bool header= !fonts_ || media_.status().view==MediaView::Cover;
    if(model_.headerVisible!=header&&!media_.frameInProgress()){model_.headerVisible=header;overlayPending_=false;
        model_.dirty|=DirtyTitle|DirtyArtist;if(!titleMeasured_)measureTitle(nowMs);}
    model_.tick(nowMs);
    if(!header)model_.clearDirty(DirtyTitle|DirtyArtist);
    if(logNote_ && logNote_!=4 && nowMs-logNoteAt_>=1500){logNote_=0;model_.dirty|=DirtyStatus;}
    if(fonts_&&media_.status().viewFailures>shownViewFailures_){
        shownViewFailures_=media_.status().viewFailures;logNote_=3;logNoteAt_=nowMs;model_.dirty|=DirtyStatus;}
    if (fonts_) {
        media_.updatePosition(snapshot.positionMs,snapshot.durationMs,snapshot.state!=PlayerState::Playing);
        if(media_.wantsFrame(nowMs)) model_.dirty |= DirtyContent;
        if(model_.dirty & DirtyOverlay) {overlayPending_=true;media_.requestRedraw();model_.dirty |= DirtyContent;model_.clearDirty(DirtyOverlay);++stats_.overlaySlices;}
    }
}

void NowPlayingPresenter::setContent(const char* hint, const char* error) {
    const auto revision=model_.contentRevision;
    model_.setContent(hint, error);
    if(fonts_ && revision!=model_.contentRevision)media_.requestRedraw();
}

void NowPlayingPresenter::notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs) {
    model_.notifyVolumeAdjusted(volume, nowMs);
}

void NowPlayingPresenter::prepareRow(int height, uint16_t background,
                                     float textSize) {
    // The pixel allocation AND canvas dimensions stay fixed for its lifetime.
    // ImageBand retains this canvas across cooperative reads.
    if(media_.bandActive())return;
    rowHeight_=height;
    row_.clearClipRect();
    row_.setClipRect(0,0,G::width,height);
    row_.fillScreen(background);
    row_.setTextSize(textSize);
    row_.setTextColor(kText, background);
    row_.setTextWrap(false);
}

void NowPlayingPresenter::pushRow(M5GFX& display, int y) {
    int x,top,w,h;display.getClipRect(&x,&top,&w,&h);
    const int first=std::max(top,y),last=std::min(top+h,y+int(rowHeight_));
    display.setClipRect(x,first,w,std::max(0,last-first));
    row_.pushSprite(&display, 0, y);
    display.setClipRect(x,top,w,h);
}

void NowPlayingPresenter::drawContentSlice(int screenY, int height) {
    if(!media_.bandActive())prepareRow(height, kBackground, kSmallSize);
    const bool card=fonts_ && !media_.frameInProgress() && (model_.hint[0]||model_.error[0]);
    if(fonts_ && !card) media_.drawStripe(row_,screenY-frameContentY_,height);
    // All coordinates refer to the Content Stage; row clipping handles its
    // small stripe. P3C can replace this background painter without reflowing
    // either chrome or the volume overlay.
    row_.setTextColor(kMuted, kBackground);
    if(!fonts_) {row_.drawString("P3B", 6, 85 - screenY);row_.drawString("MEDIA IN P3C", 6, 105 - screenY);}
    const char* hint = card ? (model_.error[0]?model_.error:model_.hint) : (fonts_?"":"ESC\nBACK TO LIST");
    const char* newline = std::strchr(hint, '\n');
    // A glyph can span two stripes. The common layout helper accepts a
    // negative baseline and clips to this row without splitting UTF-8.
    auto line = [&](const char* text, size_t bytes, int baseline, uint16_t color) {
        row_.setTextColor(color, kBackground);
        UiTextLayout::drawClippedLabel(row_, text, G::margin, baseline - screenY,
                                        G::textWidth, bytes);
    };
    if(*hint)line(hint, newline ? static_cast<size_t>(newline - hint) : std::strlen(hint),
          135, kAccent);
    if (newline) line(newline + 1, std::strlen(newline + 1), 153, kText);
    if(card){
        // A separate instruction card, never a text overlay on the media.
        prepareRow(height,kBackground,1.5f);
        UiTextLayout::draw(row_,hint,{6,static_cast<int16_t>(58-screenY),123,140,7,5,true});
        return;
    }
    const bool visible=fonts_ && media_.frameInProgress()?frameVolumeVisible_:model_.volumeVisible;
    const uint8_t volume=fonts_ && media_.frameInProgress()?frameVolume_:model_.volume;
    if (visible) {
        // Composite foreground only over the freshly reconstructed media.
        // No panel, opaque text background, or saved stale screenshot.
        row_.drawFastVLine(8, 70 - screenY, 62, kMuted);
        const int fill = (static_cast<unsigned>(volume) * 62 + 127) / 255;
        row_.fillRect(7, 132 - screenY - fill, 3, fill, kAccent);
        char percent[8] = {};
        std::snprintf(percent, sizeof(percent), "%u%%",
                      NowPlayingModel::volumePercent(volume));
        row_.setTextSize(1.0f);
        row_.setTextColor(kText);
        CachedUiFont font(fonts_,10);row_.setFont(&font);
        row_.drawString(percent, G::overlayX, 138 - screenY);
        row_.setFont(&fonts::Font0);
    }
}

void NowPlayingPresenter::drawStateIcon(PlayerState state) {
    switch (state) {
        case PlayerState::Playing: row_.fillTriangle(6, 3, 6, 12, 13, 7, kAccent); break;
        case PlayerState::Paused:
            row_.fillRect(6, 3, 3, 10, kAccent);
            row_.fillRect(11, 3, 3, 10, kAccent); break;
        case PlayerState::Stopped: row_.fillRect(6, 4, 8, 8, kMuted); break;
        case PlayerState::Error:
            row_.drawLine(6, 3, 14, 12, TFT_ORANGE);
            row_.drawLine(14, 3, 6, 12, TFT_ORANGE); break;
        case PlayerState::Empty: row_.drawRect(6, 4, 8, 8, kMuted); break;
    }
}

bool NowPlayingPresenter::renderOne(M5GFX& display) {
    if (!model_.active) return false;
    const uint32_t started = micros();
    stats_.minimumHeap = std::min(stats_.minimumHeap, ESP.getFreeHeap());
    if(fonts_ && !renderHalted_) {
        preferContent_=!preferContent_;
        const auto step=dispatchContent(media_.presentingLyrics()||media_.bandActive(),
            !clearPage_&&preferContent_,bool(model_.dirty&DirtyContent),
            [&](){return renderContentOne(display);},[&](){return media_.bandActive();});
        if(step!=RenderStep::Idle){
            stats_.renderMaxUs=std::max<uint32_t>(stats_.renderMaxUs,micros()-started);
            return step==RenderStep::Submitted;
        }
    }
    if (clearPage_) {
        display.clearClipRect();
        display.fillScreen(kBackground);
        clearPage_ = false;
        ++stats_.pageClears;
    } else if (model_.dirty & DirtyTitle) {
        if(fonts_&&!model_.title[0]&&!fonts_->requestUiWindow("暂无歌曲",14,0,123,1))return false;
        // Measure the whole title before drawing a clipped visible window.
        if(fonts_){if(!titleMeasured_){if(!fonts_->requestUiMetrics(model_.title,14))return false;measureTitle(millis());titleMeasured_=true;}
            if(!fonts_->requestUiWindow(model_.title,14,model_.titleOffsetPx,123,1))return false;}
        prepareRow(16, kBackground, kTitleSize);
        CachedUiFont font(fonts_,14); if(fonts_)row_.setFont(&font);
        row_.setTextColor(kAccent, kBackground);
        const char* title=model_.title[0]?model_.title:"暂无歌曲";
        const int width=UiTextLayout::singleLineWidth(row_,title);
        const int x=width<=123?(135-width)/2:6;
        UiTextLayout::drawScrolledLine(row_,title,
                                       {int16_t(x), 1, int16_t(135-x-6), 15, 1, 0, false}, model_.titleOffsetPx);
        pushRow(display, 0);
        model_.clearDirty(DirtyTitle);
        ++stats_.titleDraws;
        row_.setFont(&fonts::Font0);
    } else if (model_.dirty & DirtyArtist) {
        if(fonts_ && (!fonts_->requestUiWindow("...",12,0,123,1)||!fonts_->requestUiWindow(model_.artist,12,0,123,1)))return false;
        prepareRow(12, kBackground, kSmallSize);
        CachedUiFont font(fonts_); if(fonts_)row_.setFont(&font);
        const int width=UiTextLayout::singleLineWidth(row_,model_.artist);
        const int x=width<=123?(135-width)/2:6;
        UiTextLayout::draw(row_, model_.artist, {int16_t(x), 0, int16_t(135-x-6), 12, 1, 0, true});
        pushRow(display, 16);
        model_.clearDirty(DirtyArtist);
        ++stats_.artistDraws;
        row_.setFont(&fonts::Font0);
    } else if (model_.dirty & (DirtyTime|DirtyStatus)) {
        char position[16], duration[16], text[40];
        formatTime(position,sizeof(position),model_.positionMs);
        if(model_.durationMs)formatTime(duration,sizeof(duration),model_.durationMs);else std::strcpy(duration,"--:--");
        std::snprintf(text,sizeof(text),"%s/%s",position,duration);
        const char* statusText=logNote_?(logNote_==1?"已保存":logNote_==3?"切换失败":logNote_==4?"保存中":logNote_==5?"状态已存 日志失败":"保存失败"):text;
        if(fonts_&&!fonts_->requestUiWindow(statusText,14,0,84,1))return false;
        const auto modeIcon=playbackModeIcon(model_.repeat,model_.shuffle);
        const char iconLabel=playbackModeIconLabel(modeIcon);
        char modeGlyph[2]={iconLabel,'\0'};
        if(fonts_&&!fonts_->requestUiWindow(modeGlyph,10,0,12,1))return false;
        prepareRow(18, kBackground, 1.0f);
        CachedUiFont font(fonts_,14);if(fonts_)row_.setFont(&font);
        drawStateIcon(model_.state);
        // Two compact, truthful marks: queue mode and unchanged Original path.
        CachedUiFont iconFont(fonts_,10);if(fonts_)row_.setFont(&iconFont);
        drawPlaybackModeIcon(row_,modeIcon);
        row_.drawCircle(123,8,5,kMuted);row_.drawFastHLine(120,8,7,kText);
        if(fonts_)row_.setFont(&font);
        UiTextLayout::draw(row_, statusText, {17, 2, 84, 16, 1, 0, true});
        pushRow(display, G::footerY);
        row_.setFont(&fonts::Font0);
        model_.clearDirty(DirtyTime|DirtyStatus);
        ++stats_.timeDraws;++stats_.statusDraws;
    } else if (model_.dirty & DirtyProgress) {
        prepareRow(3, kBackground, kSmallSize);
        row_.drawFastHLine(6, 1, 123, kMuted);
        const int progress = model_.progressPixels();
        if (progress > 0) row_.drawFastHLine(6, 1, progress, kAccent);
        pushRow(display, 236);
        model_.clearDirty(DirtyProgress);
        ++stats_.progressDraws;
    } else if (model_.dirty & DirtyContent) {
        if(fonts_) {if(renderHalted_||renderContentOne(display)!=RenderStep::Submitted)return false;}
        else {
        if (contentRevision_ != model_.contentRevision) {
            contentRevision_ = model_.contentRevision;
            contentRow_ = 0;
        }
        const int height = std::min(G::rowHeight, G::contentHeight - contentRow_);
        drawContentSlice(G::contentY + contentRow_, height);
        pushRow(display, G::contentY + contentRow_);
        contentRow_ += height;
        if (contentRow_ == G::contentHeight) {
            contentRow_ = 0;
            model_.clearDirty(DirtyContent);
        }
        ++stats_.contentSlices;
        }
    } else if (model_.dirty & DirtyOverlay) {
        if (overlayRevision_ != model_.overlayRevision) {
            overlayRevision_ = model_.overlayRevision;
            overlayRow_ = 0;
        }
        const int height = std::min(G::rowHeight, G::overlayHeight - overlayRow_);
        drawContentSlice(G::overlayY + overlayRow_, height);
        display.setClipRect(G::overlayX, G::overlayY, G::overlayWidth, G::overlayHeight);
        pushRow(display, G::overlayY + overlayRow_);
        display.clearClipRect();
        overlayRow_ += height;
        if (overlayRow_ == G::overlayHeight) {
            overlayRow_ = 0;
            model_.clearDirty(DirtyOverlay);
        }
        ++stats_.overlaySlices;
    } else {
        return false;
    }
    if(fonts_&&!media_.frameInProgress())fonts_->clearPins(FontCache::Ui);
    stats_.renderMaxUs = std::max<uint32_t>(stats_.renderMaxUs, micros() - started);
    return true;
}

void NowPlayingPresenter::cancelFrame(){
    if(commit_.active)++stats_.frameCancels;
    commit_.cancel();media_.abortFrame();contentRow_=0;overlayPending_=false;bandWaitAt_=0;
}
RenderStep NowPlayingPresenter::rejectFrame(const char* reason){
    if(!stats_.frameRejects)stats_.frameFailure=reason;
    ++stats_.frameRejects;cancelFrame();
    if(!repairAttempts_++){++stats_.frameRepairs;media_.requestRedraw();model_.dirty|=DirtyContent;}
    else{renderHalted_=true;model_.clearDirty(DirtyContent);logNote_=3;logNoteAt_=millis();model_.dirty|=DirtyStatus;}
    return RenderStep::Failed;
}
RenderStep NowPlayingPresenter::renderContentOne(M5GFX& display) {
    if(!media_.frameInProgress()&&commit_.active)cancelFrame();
    if(!media_.frameInProgress() && (model_.hint[0]||model_.error[0])) {
        if(contentRevision_!=model_.contentRevision){contentRevision_=model_.contentRevision;contentRow_=0;}
        const int height=std::min(G::rowHeight,G::contentHeight-contentRow_);
        drawContentSlice(G::contentY+contentRow_,height);pushRow(display,G::contentY+contentRow_);
        contentRow_+=height;++stats_.contentSlices;
        if(contentRow_==G::contentHeight){contentRow_=0;model_.clearDirty(DirtyContent);}
        return RenderStep::Submitted;
    }
    if(!media_.frameInProgress()) {
        const bool newHeader=media_.requestedView()==MediaView::Cover&&
            (!model_.headerVisible||(model_.dirty&(DirtyTitle|DirtyArtist)));
        if(newHeader&&fonts_){
            if(!titleMeasured_){if(!fonts_->requestUiMetrics(model_.title,14))return RenderStep::Waiting;measureTitle(millis());titleMeasured_=true;}
            if(!fonts_->requestUiWindow(model_.title,14,0,123,1)||!fonts_->requestUiWindow(model_.artist,12,0,123,1)||!fonts_->requestUiWindow("...",12,0,20,1))return RenderStep::Waiting;
        }
        if(model_.volumeVisible && fonts_&&!fonts_->requestUiWindow("0123456789%",10,0,100,1))return RenderStep::Waiting;
        const bool patch=overlayPending_ && !media_.status().viewPending && media_.canPatchOverlay();
        if(!media_.beginFrame(millis()))return RenderStep::Waiting;
        if(newHeader){
            prepareRow(16,kBackground,1);CachedUiFont titleFont(fonts_,14);row_.setFont(&titleFont);row_.setTextColor(kAccent,kBackground);
            const int width=UiTextLayout::singleLineWidth(row_,model_.title);
            const int x=width<=123?(135-width)/2:6;
            UiTextLayout::drawScrolledLine(row_,model_.title,{int16_t(x),1,int16_t(129-x),15,1,0,false},0);pushRow(display,0);
            prepareRow(12,kBackground,1);CachedUiFont artistFont(fonts_,12);row_.setFont(&artistFont);
            const int artistWidth=UiTextLayout::singleLineWidth(row_,model_.artist),artistX=artistWidth<=123?(135-artistWidth)/2:6;
            UiTextLayout::draw(row_,model_.artist,{int16_t(artistX),0,int16_t(129-artistX),12,1,0,true});pushRow(display,16);row_.setFont(&fonts::Font0);
            model_.clearDirty(DirtyTitle|DirtyArtist);
            ++stats_.titleDraws;++stats_.artistDraws;
        }
        frameContentY_=G::contentTop(media_.frameView()==MediaView::Lyrics);
        framePartial_=patch;overlayPending_=false;
        contentRow_=patch?G::overlayY-frameContentY_:0;
        contentEnd_=patch?contentRow_+G::overlayHeight:G::footerY-frameContentY_;
        commit_.begin(media_.generation(),media_.frameId(),contentRow_,contentEnd_,patch);++stats_.frameStarts;
        stats_.frameStartedAt=millis();stats_.frameFirstAt=stats_.frameCompletedAt=0;
        if(patch)++stats_.overlayPatches;
        frameOverlayRevision_=model_.overlayRevision;frameContentRevision_=model_.contentRevision;
        frameVolumeVisible_=model_.volumeVisible;frameVolume_=model_.volume;
    }
    const int height=std::min(media_.stripeHeight(),contentEnd_-contentRow_);
    if(!commit_.accepts(media_.generation(),media_.frameId(),contentRow_,height))return rejectFrame("frame_generation_or_range");
    if(!media_.frameResourceValid())return rejectFrame("frame_resource_failed");
    if(!media_.bandActive())prepareRow(height,kBackground,kSmallSize);
    if(row_.width()!=G::width||row_.height()!=G::rowHeight||!validStripeDestination(frameContentY_+contentRow_,height))return rejectFrame("frame_canvas_dimensions");
    if(media_.bandActive()&&!media_.bandMatches(row_,contentRow_,height))return rejectFrame("frame_band_canvas");
    if(!media_.prepareStripe(row_,contentRow_,height)){if(!bandWaitAt_)bandWaitAt_=micros();++stats_.bandWaits;return RenderStep::Waiting;}
    if(bandWaitAt_){stats_.bandWaitWallMaxUs=std::max<uint32_t>(stats_.bandWaitWallMaxUs,micros()-bandWaitAt_);bandWaitAt_=0;}
    if(media_.bandActive()&&!media_.bandMatches(row_,contentRow_,height))return rejectFrame("frame_band_owner");
    const uint32_t drawAt=micros();drawContentSlice(frameContentY_+contentRow_,height);
    stats_.drawMaxUs=std::max<uint32_t>(stats_.drawMaxUs,micros()-drawAt);
    if(framePartial_)display.setClipRect(G::overlayX,G::overlayY,G::overlayWidth,G::overlayHeight);
    const uint32_t submitAt=micros();pushRow(display,frameContentY_+contentRow_);
    stats_.submitMaxUs=std::max<uint32_t>(stats_.submitMaxUs,micros()-submitAt);
    if(!commit_.submit(media_.generation(),media_.frameId(),contentRow_,height))return rejectFrame("frame_submit_rejected");
    media_.stripeSubmitted();
    if(!stats_.frameFirstAt)stats_.frameFirstAt=millis();
    media_.finishStripe();
    if(framePartial_)display.clearClipRect();
    contentRow_+=height;++stats_.contentSlices;
    if(commit_.complete()){
        stats_.frameCompletedAt=millis();
        contentRow_=0;media_.endFrame(commit_.partial);commit_.cancel();
        // Publish chrome visibility with the completed frame, before observers
        // run; the next update must not report the previous view's header.
        model_.headerVisible=media_.status().view==MediaView::Cover;
        if(!model_.headerVisible)model_.clearDirty(DirtyTitle|DirtyArtist);
        if(frameOverlayRevision_==model_.overlayRevision && frameContentRevision_==model_.contentRevision)model_.clearDirty(DirtyContent);
    }
    return RenderStep::Submitted;
}

}  // namespace player
}  // namespace adv_walkman
