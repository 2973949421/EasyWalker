#include "UiCoordinator.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "PlayerKeys.h"
#include "P4Controls.h"
#include "P3BChecks.h"
#include "PlaybackPageRoute.h"

namespace adv_walkman {
namespace player {

bool UiCoordinator::begin(M5GFX& display, PlayerRuntime& player,
                          LibraryRuntime& libraryRuntime) {
    display_ = &display;
    player_ = &player;
    libraryRuntime_ = &libraryRuntime;
    nowPlaying_.begin();
    stats_.displaySelfFailure=nowPlaying_.bootRenderSelfCheck();++stats_.displaySelfChecks;
    // Scratch-only checks before media binding / playback; no extra framebuffer.
    for (const auto result : {checkP3BModel(), checkP3BPresenterDrawing(nowPlaying_),
                               checkP3BOverlayRestoration(nowPlaying_)}) {
        stats_.displaySelfChecks += result.checks;
        if (!stats_.displaySelfFailure) stats_.displaySelfFailure = result.failure;
    }
    if(!fonts_.begin()) return false;
    nowPlaying_.bindMedia(fonts_);
    const char* fontCheck=nowPlaying_.bootFontSelfCheck(display);stats_.displaySelfChecks+=3;
    if(!stats_.displaySelfFailure)stats_.displaySelfFailure=fontCheck;
#if !defined(P3A_DEVICE_GATE)
    // Real media parser/layout/cancellation, while Player remains restored
    // Paused/Empty. No decoder, scripted seek, sound, or state-file write.
    char restoredPath[kTrackPathCapacity]{};
    const bool hasRestored=player.currentPath(restoredPath,sizeof(restoredPath));
    const char* mediaCheck=nowPlaying_.bootMediaSelfCheck(hasRestored?restoredPath:nullptr);
    stats_.displaySelfChecks+=hasRestored?4:1;
    if(!stats_.displaySelfFailure)stats_.displaySelfFailure=mediaCheck;
#endif
    nowPlaying_.setPreferredView(player.preferredNowPlayingView());
    std::strcpy(libraryRoot_, MusicLibrary::kMusicRoot);
    std::strcpy(libraryName_, "未分类");

    const PlayerSnapshot snapshot = player.snapshot();
    if (snapshot.hasCurrent) {
        page_ = UiPage::Player;
        char track[kTrackPathCapacity] = {};
        if (player.currentPath(track, sizeof(track))) {
            parentDirectoryOfTrack(track, lastPlaylistPath_,
                                   sizeof(lastPlaylistPath_));
            firstLevelLibraryOfTrack(track, libraryRoot_,
                                     sizeof(libraryRoot_), uncategorized_);
            displayNameFromPath(libraryRoot_, libraryName_,
                                sizeof(libraryName_));
        }
    } else {
        page_ = UiPage::Library;
        openBrowser(MusicLibrary::kMusicRoot, UiPage::Library);
    }
    stats_.minimumHeap = ESP.getFreeHeap();
    settings_.begin();power_.begin(millis(),page_==UiPage::Player);
    dirty_ = true;
    return true;
}

void UiCoordinator::service() {
    if (display_ == nullptr || player_ == nullptr || libraryRuntime_ == nullptr) {
        return;
    }
    // Input only records the desired power state. Cancel is RAM-only;
    // subsequent calls close at most ONE file before returning to audio.
    if(displayLifecycle_.stage()==DisplayLifecycle::Cancel){
        nowPlaying_.setActive(false,millis());libraryVisual_.release();
        fonts_.clearPins();displayLifecycle_.cancelled();return;
    }
    if(nowPlaying_.serviceSuspension())return;
    // Browser fonts can own handles even though Player is already inactive.
    if(displayLifecycle_.stage()==DisplayLifecycle::Drain&&!fonts_.suspendOne())return;
    if(libraryVisual_.serviceSuspension())return;
    displayLifecycle_.drained();
    if(displayLifecycle_.stage()==DisplayLifecycle::Apply){
        appliedBrightness_=power_.asleep()?0:uint16_t(settings_.store.value.brightness)*255/100;
        display_->setBrightness(appliedBrightness_);displayLifecycle_.applied();
        if(!power_.asleep()){
            wake_.backlight=millis();wake_.resume=millis();wake_.position=player_->snapshot().positionMs;
            wake_.pcm=player_->snapshot().pcmBuffersSinceReset;
            wake_.frames=nowPlaying_.mediaStatus().frames;wakeFramePending_=true;
            pageClearRequested_=true;pageRequestedAt_=millis();warmReturnPending_=false;
            // Preserve the six-row model; wake is a repaint, not a rescan.
            dirtyRegions_=255;dirty_=true;libraryVisualDirty_=true;p3dPrepared_=false;
            libraryPage_.pageChanged();
            libraryVisual_.invalidate();settings_.invalidate();nowPlaying_.invalidateDisplay();
            if(page_==UiPage::Library)invalidateBrowser();
        }
        return;
    }
    if(power_.asleep())return;
    stats_.minimumHeap = std::min(stats_.minimumHeap, ESP.getFreeHeap());
    stats_.navigationState = navigation_.state;
    stats_.navigationGeneration = navigation_.generation;
    // Establish a clean page before any directory or font I/O. No old media
    // stripe may be submitted after setPage() released its generation.
    if (pageClearRequested_) {
        display_->clearClipRect();
        display_->fillScreen(0x0861);
        pageClearRequested_ = false;
        stats_.pageFirstFrameComplete = false;
        if (page_ == UiPage::Playlist) {
            display_->drawFastHLine(6, 25, 123, 0x8410);
            display_->drawRoundRect(8, 45, 119, 136, 5, 0x8410);
        }
        return;
    }
    if(serviceSaveToast(millis()))return;
    const bool opening = openRequested_;
    const uint32_t navStarted = micros();
    servicePendingNavigation();
    stats_.navigationMaxUs = std::max<uint32_t>(stats_.navigationMaxUs, micros()-navStarted);
    if (opening) return;
    if (page_ != UiPage::Player && navigation_.state != NavigationState::Error &&
        (fonts_.stats().missing || fonts_.stats().ioErrors || fonts_.stats().capacityErrors)) {
        navigationFailed(fonts_.failure().reason);
        return;
    }

    MusicLibrary& library = libraryRuntime_->library();
    if (page_ == UiPage::Library && library.state() == LibraryState::Ready &&
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0) {
        catalog_.sync(library);
    }

    const PlayerSnapshot snapshot = player_->snapshot();
    char current[kTrackPathCapacity] = {};
    player_->currentPath(current, sizeof(current));
    const uint32_t now = millis();
    // Update the inactive model BEFORE activation: sleep may span EOF/next track.
    if(!nowPlaying_.model().active)nowPlaying_.update(snapshot,current,*libraryRuntime_,now);
    nowPlaying_.setActive(page_ == UiPage::Player, now);
    nowPlaying_.update(snapshot, current, *libraryRuntime_, now);
    const bool directoryChanged=library.state()!=lastLibraryState_||library.currentGeneration()!=lastLibraryGeneration_;
    const bool trackChanged=std::strcmp(current,lastCurrentTrack_)!=0;
    lastLibraryState_=library.state();lastLibraryGeneration_=library.currentGeneration();
    setPath(lastCurrentTrack_,sizeof(lastCurrentTrack_),current);
    if(directoryChanged&&page_!=UiPage::Player){if(page_==UiPage::Library){libraryVisualDirty_=true;libraryPage_.resourceChanged();}invalidateBrowser();dirty_=true;}
    else if(trackChanged&&page_==UiPage::Playlist&&browserContextReady_){
        for(unsigned i=0;i<kP3AVisibleRows;++i){auto& row=renderContext_.rows[i];
            const char* base=std::strrchr(current,'/');const bool playing=row.valid&&base&&!std::strcmp(base+1,row.basename)&&!std::strncmp(current,navigationTarget_,std::strlen(navigationTarget_))&&current[std::strlen(navigationTarget_)]=='/';
            if(playing!=row.playing){row.playing=playing;row.displayFlags=0;dirtyRegions_|=1U<<(i+1);dirty_=true;}}
    }
    if(page_==UiPage::Player&&!nowPlaying_.mediaStatus().viewPending)
        player_->setPreferredNowPlayingView(static_cast<uint8_t>(nowPlaying_.mediaStatus().preferred));
    // A visible request is never blocked by a ready band owned by an older
    // request.  The explicit scheduler chooses one bounded step; render is the
    // only fall-through path below.
    if(page_==UiPage::Library&&libraryVisual_.transaction().stalled(now)){
        ++stats_.libraryStalls;
        const auto recovery=libraryPage_.checkProgress(libraryVisual_.transaction(),now);libraryVisual_.cancelPending();
        if(recovery==LibraryRecoveryAction::Rebuild){
            ++stats_.libraryRecoveries;libraryVisualDirty_=true;invalidateBrowser();return;
        }
        if(recovery==LibraryRecoveryAction::Error){++stats_.libraryFailures;libraryVisual_.showError();setExternalError("封面读取失败");}
    }
    UiWorkSnapshot work{};
    work.player=page_==UiPage::Player;work.library=page_==UiPage::Library;
    work.browserReady=browserContextReady_;work.pendingIntent=pendingIntent_!=PendingIntent::None;
    work.fontBusy=fonts_.busy();work.playerBandWaiting=nowPlaying_.waitingForBand();
    work.libraryBandWaiting=page_==UiPage::Library&&libraryVisual_.cover.bandActive()&&!libraryVisual_.cover.bandReady();
    work.libraryLoading=page_==UiPage::Library&&libraryVisual_.cover.state()==MediaState::Loading;
    switch(workScheduler_.choose(work)){
        case UiScheduledWork::BrowserModel:prepareBrowser();return;
        case UiScheduledWork::PendingIntent:servicePendingIntent();return;
        case UiScheduledWork::FontIo:fonts_.service();return;
        case UiScheduledWork::PlayerMedia:nowPlaying_.serviceMedia();return;
        case UiScheduledWork::LibraryMedia:libraryVisual_.service();return;
        case UiScheduledWork::Render:break;
    }
    const char* error = externalError_;
    if (error[0] == '\0' && snapshot.error != PlayerError::None) {
        error = playerErrorName(snapshot.error);
    }
    if(error[0]=='\0' && (fonts_.stats().ioErrors || fonts_.stats().missing || fonts_.stats().capacityErrors))error=fonts_.failure().reason;
    nowPlaying_.setContent(hint_, error);

    if(gateCard_[0]) {
        if(gateCardDirty_) {
            display_->setFont(&fonts::Font0);display_->fillScreen(0x0861);
            display_->setTextColor(TFT_WHITE,0x0861);display_->setTextSize(1.75f);
            UiTextLayout::draw(*display_,gateCard_,{6,12,123,216,10,5,true});
            gateCardDirty_=false;
        }
        return;
    }
    if (page_ == UiPage::Player) {
        if (nowPlaying_.renderOne(*display_)) {
            ++stats_.renderCount;
            stats_.renderMaxUs = std::max(stats_.renderMaxUs,
                                          nowPlaying_.stats().renderMaxUs);
        }
        if(modeFeedbackPending_ && (feedbackExpected_&0x80U)==0 &&
           nowPlaying_.stats().statusDraws>modeStatusDrawBaseline_ &&
           nowPlaying_.model().repeat==(feedbackExpected_==1?RepeatMode::One:
               feedbackExpected_==2?RepeatMode::All:RepeatMode::Off) &&
           nowPlaying_.model().shuffle==(feedbackExpected_==3)){
            stats_.modeFeedbackMaxMs=std::max(stats_.modeFeedbackMaxMs,
                                              uint32_t(millis()-modeRequestedAt_));
            modeFeedbackPending_=false;
        }
        if(modeFeedbackPending_ && (feedbackExpected_&0x80U)!=0 &&
           nowPlaying_.stats().statusDraws>modeStatusDrawBaseline_ &&
           nowPlaying_.model().soundPreset==
               static_cast<SoundPreset>(feedbackExpected_&0x03U)){
            stats_.recordSoundFeedback(millis()-modeRequestedAt_);
            modeFeedbackPending_=false;
        }
        if(wakeFramePending_&&nowPlaying_.mediaStatus().frames>wake_.frames&&nowPlaying_.displayComplete()){
            wake_.firstFrame=millis();wakeFramePending_=false;
            const auto scene=powerScene();if(scene==wakePage_&&wakesByPage_[scene]<255)++wakesByPage_[scene];}
        dirty_ = false;
        return;
    }
    // Rate-limit animations, not glyph preparation. A failed font request is
    // not a painted frame and must not impose another 20ms wait per glyph.
    if ((page_==UiPage::Library||page_==UiPage::Settings) || dirty_) {
        renderRetryRequested_ = false;
        render();
        if(wakeFramePending_&&stats_.pageFirstFrameComplete){wake_.firstFrame=millis();wakeFramePending_=false;if(wakesByPage_[wakePage_]<255)++wakesByPage_[wakePage_];}
        dirty_ = renderRetryRequested_;
    }
}

bool UiCoordinator::handleAction(UiAction action) {
    if (action == UiAction::None || libraryRuntime_ == nullptr ||
        player_ == nullptr) {
        return false;
    }
    ++stats_.inputEvents;
    if(action==UiAction::ToggleCurrentPlaybackPage){
        const auto before=player_->snapshot();
        const auto route=playbackPageRoute(page_==UiPage::Player,before.hasCurrent);
        if(route==PlaybackPageRoute::None)return false;
        ++stats_.tabEvents;
        if(route==PlaybackPageRoute::CurrentFolder){
            // Tab always follows the current song, not a previously browsed folder.
            char current[kTrackPathCapacity],parent[kTrackPathCapacity];
            if(!player_->currentPath(current,sizeof(current)) ||
               !parentDirectoryOfTrack(current,parent,sizeof(parent)))return false;
            setPath(lastPlaylistPath_,sizeof(lastPlaylistPath_),parent);
            if(!restorePlaylistForCurrentTrack())return false;
        }else{
            if(page_==UiPage::Playlist){
                setPath(lastPlaylistPath_,sizeof(lastPlaylistPath_),navigationTarget_);
                savedPlaylistSelected_=playlistSelected_;
            }
            cancelNavigation();setPage(UiPage::Player);
        }
        const auto after=player_->snapshot();
        stats_.tabBeforeMs=before.positionMs;stats_.tabAfterMs=after.positionMs;stats_.tabState=static_cast<uint8_t>(after.state);
        if(before.state==PlayerState::Playing)++stats_.tabPlaying;
        if(before.state==PlayerState::Paused)++stats_.tabPaused;
        if(before.state!=after.state || before.positionMs!=after.positionMs || before.queueCount!=after.queueCount)
            ++stats_.tabStateErrors;
        return true;
    }
    if (page_ != UiPage::Player && action == UiAction::Confirm &&
        navigation_.state == NavigationState::Error) {
        return openBrowser(navigationTarget_, page_);
    }

    switch (page_) {
        case UiPage::Library:
            if (action == UiAction::Left) {
                if(catalog_.count()<=1)return catalog_.count()!=0;
                catalog_.move(-1);
                libraryVisualDirty_=true;libraryMoveDirection_=-1;libraryPage_.requestSelection();++stats_.libraryRequests;
                invalidateBrowser();
                return true;
            }
            if (action == UiAction::Right) {
                if(catalog_.count()<=1)return catalog_.count()!=0;
                catalog_.move(1);
                libraryVisualDirty_=true;libraryMoveDirection_=1;libraryPage_.requestSelection();++stats_.libraryRequests;
                invalidateBrowser();
                return true;
            }
            if (action == UiAction::Confirm) {
                if (navigation_.state != NavigationState::Ready || !catalog_.count()) return false;
                pendingIntent_ = PendingIntent::SelectLibrary;
                return true;
            }
            if (action == UiAction::OpenSettings) {
                cancelNavigation();
                settings_.open();
                setPage(UiPage::Settings);
                return true;
            }
            return action == UiAction::Back;

        case UiPage::Playlist: {
            const size_t count = navigation_.state == NavigationState::Ready ? playlistCount() : 0;
            if (action == UiAction::Up && count != 0) {
                movePlaylistSelection(playlistSelected_ == 0 ? count-1 : playlistSelected_-1);
                return true;
            }
            if (action == UiAction::Down && count != 0) {
                movePlaylistSelection((playlistSelected_+1)%count);
                return true;
            }
            if (action == UiAction::Confirm) {
                if (navigation_.state != NavigationState::Ready || locateCurrent_) return false;
                pendingIntent_ = PendingIntent::ActivatePlaylistEntry;
                return true;
            }
            if (action == UiAction::Back) {
                return returnFromPlaylist();
            }
            return false;
        }

        case UiPage::Player:
            if(action==UiAction::TogglePlayback){
                return player_->snapshot().state==PlayerState::Playing?player_->pause():player_->play();
            }
            if(action==UiAction::VolumeUp || action==UiAction::VolumeDown){
                player_->setVolume(adjustedVolume(player_->volume(),action==UiAction::VolumeUp?8:-8));
                notifyVolumeAdjusted(player_->volume(),millis());
                return true;
            }
            if(action==UiAction::ToggleView) {
                const bool changed=nowPlaying_.toggleView();
                return changed;
            }
            if(action==UiAction::PreviousTrack){
                const bool changed=player_->previous();
                if(changed)++stats_.previousActions;
                else ++stats_.transportActionFailures;
                return changed;
            }
            if(action==UiAction::NextTrack){
                const bool changed=player_->next();
                if(changed)++stats_.nextActions;
                else ++stats_.transportActionFailures;
                return changed;
            }
            if(action==UiAction::CyclePlayMode){
                const auto before=player_->snapshot();
                const auto next=nextPlaybackMode(before.repeatMode,before.shuffleEnabled);
                stats_.modeBeforeRepeat=static_cast<uint8_t>(before.repeatMode);
                stats_.modeBeforeShuffle=before.shuffleEnabled;
                if(!player_->setPlaybackMode(next.repeat,next.shuffle)){
                    ++stats_.transportActionFailures;
                    return false;
                }
                const auto after=player_->snapshot();
                stats_.modeAfterRepeat=static_cast<uint8_t>(after.repeatMode);
                stats_.modeAfterShuffle=after.shuffleEnabled;
                ++stats_.playModeActions;
                feedbackExpected_=after.shuffleEnabled?3U:
                    after.repeatMode==RepeatMode::One?1U:
                    after.repeatMode==RepeatMode::All?2U:0U;
                modeRequestedAt_=millis();
                modeStatusDrawBaseline_=nowPlaying_.stats().statusDraws;
                modeFeedbackPending_=true;
                return true;
            }
            if(action==UiAction::SetSoundOriginal ||
               action==UiAction::SetSoundTape ||
               action==UiAction::SetSoundRadio ||
               action==UiAction::SetSoundVocalClear){
                const SoundPreset preset=action==UiAction::SetSoundTape?SoundPreset::Tape:
                    action==UiAction::SetSoundRadio?SoundPreset::Radio:
                    action==UiAction::SetSoundVocalClear?SoundPreset::VocalClear:
                    SoundPreset::Original;
                const bool changed=player_->soundPreset()!=preset;
                if(!player_->setSoundPreset(preset)){
                    stats_.recordSoundFailure();
                    return false;
                }
                stats_.recordSoundAction();
                if(!changed)return true;
                feedbackExpected_=0x80U|static_cast<uint8_t>(preset);
                modeRequestedAt_=millis();
                modeStatusDrawBaseline_=nowPlaying_.stats().statusDraws;
                modeFeedbackPending_=true;
                return true;
            }
            if (action == UiAction::Back) {
                return restorePlaylistForCurrentTrack();
            }
            return false;

        case UiPage::Settings:
            if (!settings_.handle(action,*player_)) {
                showLibrary();
                return true;
            }
            p3dPrepared_=false;return true;
    }
    return false;
}

void UiCoordinator::showLibrary() {
    if (libraryRuntime_ == nullptr) {
        return;
    }
    openBrowser(MusicLibrary::kMusicRoot, UiPage::Library);
}

void UiCoordinator::servicePendingIntent() {
    if (pendingIntent_ == PendingIntent::None ||
        pendingNavigation_ != PendingNavigation::None) {
        return;
    }
    const PendingIntent intent = pendingIntent_;
    pendingIntent_ = PendingIntent::None;
    const bool accepted = intent == PendingIntent::SelectLibrary
                              ? beginSelectedLibrary()
                              : activatePlaylistEntry();
    if (!accepted && externalError_[0] == '\0') {
        setExternalError("Pending action failed");
    }
}

void UiCoordinator::setHint(const char* hint) {
    const char* value = hint == nullptr ? "" : hint;
    if (std::strncmp(hint_, value, sizeof(hint_)) == 0) {
        return;
    }
    std::strncpy(hint_, value, sizeof(hint_) - 1);
    hint_[sizeof(hint_) - 1] = '\0';
    dirty_ = true;
}

void UiCoordinator::setExternalError(const char* error) {
    const char* value = error == nullptr ? "" : error;
    std::strncpy(externalError_, value, sizeof(externalError_) - 1);
    externalError_[sizeof(externalError_) - 1] = '\0';
    if (page_ != UiPage::Player) invalidateBrowser();
    dirty_ = true;
}

void UiCoordinator::setGateCard(const char* text) {
    if(!text)text="";
    if(std::strcmp(gateCard_,text)==0)return;
    std::snprintf(gateCard_,sizeof(gateCard_),"%s",text);gateCardDirty_=true;dirty_=true;
    if(!gateCard_[0])nowPlaying_.invalidateDisplay();
}

UiPage UiCoordinator::page() const {
    return page_;
}

UiStats UiCoordinator::stats() const {
    UiStats value=stats_;
    value.libraryStaleRejects=libraryVisual_.cover.staleRejects;
    return value;
}

bool UiCoordinator::currentTrackPath(char* output, size_t capacity) const {
    return player_ != nullptr && player_->currentPath(output, capacity);
}

void UiCoordinator::notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs) {
    // Receives the value AFTER the runtime applied it. Never simulates sound.
    nowPlaying_.notifyVolumeAdjusted(volume, nowMs);
}

void UiCoordinator::notifySavePending(uint32_t ticket){
    saveToast_=1;saveToastTicket_=ticket;saveToastAt_=millis();saveToastDrawn_=false;
    std::snprintf(saveToastText_,sizeof(saveToastText_),"保存中");
    if(page_==UiPage::Player)nowPlaying_.notifyLogSaving(saveToastAt_);else dirty_=true;
}

void UiCoordinator::notifySaveFinished(uint32_t ticket,SaveNotice result,const char* stage){
    if(ticket<saveToastTicket_)return;
    const bool success=result==SaveNotice::Succeeded;
    const bool partial=result==SaveNotice::StateSavedLogFailed;
    saveToast_=success?2:3;saveToastTicket_=ticket;saveToastAt_=millis();saveToastDrawn_=false;
    if(success)std::snprintf(saveToastText_,sizeof(saveToastText_),"已保存");
    else if(partial)std::snprintf(saveToastText_,sizeof(saveToastText_),"状态已保存，日志失败");
    else if(result==SaveNotice::TimedOut)std::snprintf(saveToastText_,sizeof(saveToastText_),"保存失败:超时");
    else if(stage&&*stage)std::snprintf(saveToastText_,sizeof(saveToastText_),"保存失败:%s",stage);
    else std::snprintf(saveToastText_,sizeof(saveToastText_),"保存失败");
    if(page_==UiPage::Player)nowPlaying_.notifyLogSaved(partial?5:(success?1:2),saveToastAt_);else dirty_=true;
}

bool UiCoordinator::serviceSaveToast(uint32_t now){
    if(!saveToast_||page_==UiPage::Player)return false;
    const uint32_t life=saveToast_==1?10000:1500;
    if(uint32_t(now-saveToastAt_)>=life){
        const bool wasDrawn=saveToastDrawn_;saveToast_=0;saveToastDrawn_=false;
        if(wasDrawn){
            if(page_==UiPage::Library)libraryVisual_.invalidateName();
            else if(page_==UiPage::Playlist)dirtyRegions_|=1;
            else if(page_==UiPage::Settings){settings_.invalidate();p3dPrepared_=false;}
            dirty_=true;
        }
        return wasDrawn;
    }
    // The toast uses direct display primitives and never borrows the shared
    // image stripe.  A pending cover read therefore must not delay T feedback.
    if(saveToastDrawn_)return false;
    if(!fonts_.requestUiWindow(saveToastText_,12,0,120,1,false,
                               page_==UiPage::Playlist?FontCache::Playlist:FontCache::Ui))return false;
    CachedUiFont font(&fonts_);display_->setFont(&font);display_->setTextSize(1);
    const int width=std::min(123,std::max(58,display_->textWidth(saveToastText_)+12));
    const int x=(135-width)/2,y=page_==UiPage::Library?kLibraryNameTop:3;
    display_->fillRoundRect(x,y,width,18,4,0x0000);display_->setTextColor(TFT_WHITE,0x0000);
    UiTextLayout::draw(*display_,saveToastText_,{int16_t(x+4),int16_t(y+1),int16_t(width-8),16,1,0,true});
    display_->setFont(&fonts::Font0);if(page_!=UiPage::Playlist)fonts_.clearPins(FontCache::Ui);saveToastDrawn_=true;return true;
}

void UiCoordinator::setPage(UiPage page,bool retainBrowser) {
    const UiPage previous=page_;
    if(page_==UiPage::Playlist && page==UiPage::Player)
        retainedPlaylist_=browserContextReady_;
    else if(page!=UiPage::Player&&!retainBrowser)retainedPlaylist_=false;
    if (page_ != page) {
        if(wakeFramePending_){wakeFramePending_=false;++unfinishedWakes_;}
        page_ = page;
        libraryPage_.pageChanged();
        ++stats_.pageTransitions;
        ++inputEpoch_;power_.pageChanged(millis(),page==UiPage::Player);
        if(page!=UiPage::Player)modeFeedbackPending_=false;
        if(page!=UiPage::Player)nowPlaying_.setActive(false,millis());
        if(page!=UiPage::Library){libraryVisual_.release();fonts_.clearPins(FontCache::Library);}
        else libraryVisualDirty_=true;
        // Keep the six visible Playlist rows resident only for the supported
        // Playlist <-> Player warm route. Other destinations release the lease.
        if(previous==UiPage::Playlist && page!=UiPage::Player)
            fonts_.clearPins(FontCache::Playlist);
        // A visible Player may have replaced the shared Metadata request.
        if (page == UiPage::Playlist) metadataRequested_ = false;
    }
    pageClearRequested_ = true;
    pageRequestedAt_=millis();loadingDrawn_=false;
    warmReturnPending_=false;feedbackRegions_=0;
    stats_.pageFirstFrameComplete = false;
    if(page!=UiPage::Player&&!retainBrowser)invalidateBrowser();
}

void UiCoordinator::invalidateBrowser() {
    fonts_.clearPins(page_==UiPage::Playlist?FontCache::Playlist:
                     page_==UiPage::Library?FontCache::Library:FontCache::Ui);
    if(page_==UiPage::Library&&libraryVisualDirty_)libraryVisual_.cancelPending();
    browserContextReady_ = false; prepareRow_ = 0; drawRegion_ = 0;
    metadataReadyRows_=0;
    dirtyRegions_=255;
    feedbackRegions_=0;
    dirty_ = true;
    p3dPrepared_=false;
    browserProgress_ = 0; browserProgressAt_ = millis();
}

void UiCoordinator::selectVisibleMetadata(){
    // Changing selection must not throw away titles from other visible rows.
    metadataRequested_=false;
}
void UiCoordinator::movePlaylistSelection(size_t next){
    const size_t old=playlistSelected_;playlistSelected_=next;locateCurrent_=false;
    if(browserContextReady_ && old/kP3AVisibleRows==next/kP3AVisibleRows){
        renderContext_.rows[old%kP3AVisibleRows].selected=false;
        renderContext_.rows[next%kP3AVisibleRows].selected=true;
        renderContext_.playlistSelected=next;
        feedbackRegions_=(1U<<(1+old%kP3AVisibleRows))|(1U<<(1+next%kP3AVisibleRows));
        selectionRequestedAt_=millis();dirtyRegions_|=feedbackRegions_;
        selectVisibleMetadata();dirty_=true;++stats_.highlightUpdates;
    }else invalidateBrowser();
}

void UiCoordinator::cancelNavigation() {
    navigation_.cancel(); openRequested_ = false;
    pendingNavigation_ = PendingNavigation::None;
    pendingIntent_ = PendingIntent::None;
    libraryRuntime_->cancelSelection();
    libraryRuntime_->library().cancelOpen();
}

bool UiCoordinator::openBrowser(const char* path, UiPage page, bool locateCurrent) {
    if (!path || !*path) return false;
    auto& library=libraryRuntime_->library();
    if(page==UiPage::Playlist && page_==UiPage::Player && retainedPlaylist_ &&
       library.state()==LibraryState::Ready && std::strcmp(path,navigationTarget_)==0 &&
       std::strcmp(path,library.currentPath())==0 && library.currentGeneration()==browserGeneration_){
        const bool retained=retainedPlaylist_;const uint8_t metadataRows=metadataReadyRows_;
        setPage(page,true);browserContextReady_=retained;prepareRow_=kP3AVisibleRows;metadataReadyRows_=metadataRows;
        warmReturnPending_=true;++stats_.warmReturns;
        navigation_.begin(millis());navigation_.state=NavigationState::Ready;
        // The retained window can locate the current song without SD reads.
        const char* name=std::strrchr(lastCurrentTrack_,'/');bool found=false;
        for(auto& row:renderContext_.rows){const bool playing=row.valid&&name&&!std::strcmp(row.basename,name+1);
            if(row.playing!=playing)row.displayFlags=0;row.playing=playing;}
        if(locateCurrent&&name)for(size_t i=0;i<kP3AVisibleRows;++i)
            if(renderContext_.rows[i].valid&&!std::strcmp(renderContext_.rows[i].basename,name+1)){
                movePlaylistSelection((playlistSelected_/kP3AVisibleRows)*kP3AVisibleRows+i);found=true;break;
            }
        if(locateCurrent&&!found){locateCurrent_=true;locateIndex_=0;invalidateBrowser();}
        dirtyRegions_=255;dirty_=true;return true;
    }
    if (navigation_.state == NavigationState::Loading && page_ == page &&
        std::strcmp(path, navigationTarget_) == 0) return true;
    // path may alias navigationTarget_ during an explicit retry.
    char target[kTrackPathCapacity];
    if (!setPath(target, sizeof(target), path)) return false;
    cancelNavigation();
    setPath(navigationTarget_, sizeof(navigationTarget_), target);
    externalError_[0] = navigationError_[0] = '\0';
    navigation_.begin(millis());
    locateCurrent_ = locateCurrent; locateIndex_ = 0;
    metadataRequested_ = false;
    setPage(page); // Suspend media I/O and rendering before opening a directory.
    openRequested_ = true;
    return true;
}

void UiCoordinator::navigationFailed(const char* reason) {
    if (navigation_.state != NavigationState::Error) navigation_.state = NavigationState::Error;
    ++stats_.navigationErrors;
    stats_.lastLibraryError = static_cast<uint32_t>(libraryRuntime_->library().error());
    stats_.largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    std::snprintf(navigationError_, sizeof(navigationError_), "%s", reason);
    setExternalError(reason);
    openRequested_ = false; pendingIntent_ = PendingIntent::None;
    pendingNavigation_ = PendingNavigation::None;
    libraryRuntime_->cancelSelection();
    libraryRuntime_->library().cancelOpen();
    invalidateBrowser();
}

void UiCoordinator::servicePendingNavigation() {
    MusicLibrary& library = libraryRuntime_->library();
    if (openRequested_) {
        openRequested_ = false;
        const auto result = library.openPath(navigationTarget_);
        browserGeneration_ = library.currentGeneration();
        browserRequest_ = navigation_.generation;
        if (result == LibraryResult::Error) navigationFailed(libraryErrorName(library.error()));
        return;
    }
    if (navigation_.state == NavigationState::Loading) {
        const auto s = library.stats();
        const uint32_t progress = s.scannedEntries + s.sortMoves + s.scanBytesWritten +
            static_cast<uint32_t>(library.state()) + s.pageMisses;
        const bool matching = library.currentGeneration() == browserGeneration_ &&
            std::strcmp(library.currentPath(), navigationTarget_) == 0;
        navigation_.observe(browserRequest_, millis(), progress,
            library.state() == LibraryState::Error ? NavigationObservation::Error :
            matching && library.state() == LibraryState::Ready ? NavigationObservation::Ready :
            NavigationObservation::Pending);
        if (navigation_.state == NavigationState::Error) {
            navigationFailed(navigation_.stalled ? "Directory stalled" : libraryErrorName(library.error()));
        } else if (navigation_.state == NavigationState::Ready) {
            if (page_ == UiPage::Playlist) {
                setPath(lastPlaylistPath_, sizeof(lastPlaylistPath_), navigationTarget_);
                playlistSelected_ = std::min(playlistSelected_, playlistCount() ? playlistCount()-1 : 0);
            }
            invalidateBrowser();
        }
    }
    if (pendingNavigation_ == PendingNavigation::SelectTrack) {
        if (!libraryRuntime_->selectionPending()) {
            char current[kTrackPathCapacity] = {};
            const PlayerSnapshot snapshot = player_->snapshot();
            if (player_->currentPath(current, sizeof(current)) &&
                std::strcmp(current, pendingTrackPath_) == 0 &&
                snapshot.state == PlayerState::Playing) {
                pendingNavigation_ = PendingNavigation::None;
                ++stats_.trackSelections;
                if (std::strcmp(lastCurrentTrack_, current) != 0) ++stats_.differentTrackSelections;
                stats_.lastQueueCount = snapshot.queueCount;
                setPage(UiPage::Player);
            } else {
                pendingNavigation_ = PendingNavigation::None;
                setExternalError("Track selection failed");
                navigationFailed("Track selection failed");
            }
        }
    }
}

void UiCoordinator::serviceSelectedMetadata() {
    if (page_ != UiPage::Playlist || pendingNavigation_ != PendingNavigation::None) {
        return;
    }
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready || playlistCount() == 0) {
        return;
    }
    // Selected row first, then other visible rows; one path, one reader.
    for(unsigned n=0;n<kP3AVisibleRows;++n){
        const unsigned i=(playlistSelected_%kP3AVisibleRows+n)%kP3AVisibleRows;
        auto& row=renderContext_.rows[i];
        if(!row.valid||row.type!=LibraryEntryType::Track||(metadataReadyRows_&(1U<<i)))continue;
        char path[kTrackPathCapacity];
        if(std::snprintf(path,sizeof(path),"%s/%s",navigationTarget_,row.basename)>=int(sizeof(path))){metadataReadyRows_|=1U<<i;recordMetadataFallback(255);return;}
        Mp3Metadata metadata;Mp3MetadataError warning=Mp3MetadataError::None;
        if(libraryRuntime_->cachedMetadataForPath(path,metadata,&warning)&&metadata.title.value[0]){
            if(std::strcmp(row.label,metadata.title.value)){
                setPath(row.label,sizeof(row.label),metadata.title.value);row.displayFlags=0;dirtyRegions_|=1U<<(i+1);dirty_=true;
            }
            if(metadata.titleFromFilename||warning!=Mp3MetadataError::None)recordMetadataFallback(warning==Mp3MetadataError::None?1:2+static_cast<uint8_t>(warning));
            metadataReadyRows_|=1U<<i;return;
        }
        const bool same=!std::strcmp(path,libraryRuntime_->metadataRequestPath());
        if(same&&libraryRuntime_->metadataStatus().state==Mp3MetadataState::Error){metadataReadyRows_|=1U<<i;recordMetadataFallback(2+static_cast<uint8_t>(libraryRuntime_->metadataStatus().error));return;}
        if(!same||!metadataRequested_){libraryRuntime_->requestMetadataPath(path);metadataRequested_=true;}
        return;
    }
}

void UiCoordinator::render() {
    UiRenderContext& context = renderContext_;
    renderRetryRequested_ = true;
    if (navigation_.state == NavigationState::Error &&
        (fonts_.stats().missing || fonts_.stats().ioErrors || fonts_.stats().capacityErrors)) {
        // A broken SD font must not make recovery instructions unreadable.
        // Normal pages never use this emergency font.
        display_->setFont(&fonts::Font0);display_->setTextSize(1.2f);
        display_->fillScreen(0x0861);display_->setTextColor(TFT_ORANGE,0x0861);
        UiTextLayout::draw(*display_,navigationError_,{8,40,119,90,5,3,true});
        UiTextLayout::draw(*display_,"ENTER: RETRY\nESC: BACK",{8,155,119,70,3,3,true});
        renderRetryRequested_=false;return;
    }
    const bool partialPlaylist=page_==UiPage::Playlist&&navigation_.state==NavigationState::Ready&&!locateCurrent_&&prepareRow_>0;
    if (!browserContextReady_&&!partialPlaylist) {
        if(loadingDrawn_ || millis()-pageRequestedAt_<250)return;
        if (!fonts_.requestUiWindow("加载中",12,0,123,1,false,
                                    page_==UiPage::Playlist?FontCache::Playlist:FontCache::Ui)) return;
        CachedUiFont loading(&fonts_); display_->setFont(&loading);
        display_->setTextSize(1);display_->setTextColor(TFT_WHITE,0x0861);
        display_->fillRect(0,196,135,44,0x0861);
        UiTextLayout::draw(*display_,"加载中",{7,202,121,18,1,0,true});
        loadingDrawn_=true;
        display_->setFont(&fonts::Font0);return;
    }
    if(page_==UiPage::Library || page_==UiPage::Settings){
        if(!p3dPrepared_){p3dPrepared_=page_==UiPage::Library?libraryVisual_.prepare(fonts_,context):settings_.prepare(fonts_);return;}
        const uint32_t at=micros();
        auto* row=nowPlaying_.sharedRow();if(!row)return;
        const bool painted=page_==UiPage::Library?libraryVisual_.render(*display_,*row,fonts_,context,millis()):
            settings_.render(*display_,*row,fonts_);
        if(painted){++stats_.renderCount;stats_.renderMaxUs=std::max<uint32_t>(stats_.renderMaxUs,micros()-at);}
        if(page_==UiPage::Library){stats_.libraryText=libraryVisual_.nameLayout;stats_.libraryTextIsBenchmark=std::strcmp(context.libraryName,"ADVWalkmanBenchmark")==0&&stats_.libraryText.lineCount!=0;}
        const bool complete=page_==UiPage::Library?libraryVisual_.complete():settings_.complete();
        if(complete&&!stats_.pageFirstFrameComplete){stats_.pageFirstFrameComplete=true;++stats_.completedPages;if(page_==UiPage::Library)++stats_.libraryFrames;}
        renderRetryRequested_=!complete;return;
    }
    // Only the current region's glyphs are prepared; this never reads SD.
    if(page_==UiPage::Playlist){
        if(!dirtyRegions_){renderRetryRequested_=false;return;}
        uint8_t available=dirtyRegions_;
        if(!browserContextReady_){uint8_t ready=1;
            for(unsigned r=0;r<kP3AVisibleRows;++r)if(context.rows[r].valid)ready|=1U<<(r+1);
            available&=ready;if(!available)return;}
        drawRegion_=0;while(!(available&(1U<<drawRegion_)))++drawRegion_;
        const uint8_t selectedRegion=1U<<(playlistSelected_%kP3AVisibleRows+1);
        if((available&selectedRegion)&&!(available&1))drawRegion_=playlistSelected_%kP3AVisibleRows+1;
        if(feedbackRegions_){drawRegion_=0;while(!(feedbackRegions_&(1U<<drawRegion_)))++drawRegion_;}
    }
    const char* fixed = page_ == UiPage::Playlist ? (drawRegion_==0?"播放列表 ...":
        drawRegion_<=kP3AVisibleRows?"> /...":context.error?"Enter 重试\nEsc 返回":context.hint?context.hint:"上下选择 Enter播放\nEsc 返回 Tab当前") :
        page_ == UiPage::Library ? "LIBRARY COLLECTION LEFT / RIGHT ENTER TO OPEN S: SETTINGS ESC: STAY RETRY0123456789" :
        "SETTINGS P3A FOUNDATION Full settings UI arrives in P3D ESC BACK TO LIBRARY";
    const uint8_t pageLease=page_==UiPage::Playlist?FontCache::Playlist:FontCache::Ui;
    if (!fonts_.requestUiWindow(fixed,12,0,2000,1,false,pageLease)) return;
    if(page_==UiPage::Playlist && drawRegion_==7 && !context.playlistCount && !fonts_.requestUiWindow("暂无歌曲",12,0,123,1,false,FontCache::Playlist))return;
    if((page_!=UiPage::Playlist||drawRegion_==0)&&!fonts_.requestUiWindow(context.libraryName,12,0,page_==UiPage::Library?194:123,1,false,pageLease)) return;
    if(page_!=UiPage::Playlist||drawRegion_==kP3AVisibleRows+1)
        for(const char* text:{context.hint,context.error})if(text&&!fonts_.requestUiWindow(text,12,0,2000,1,false,pageLease))return;
    if(page_==UiPage::Settings && !fonts_.requestUiWindow(ADV_WALKMAN_VERSION,12,0,123,1))return;
    if(page_==UiPage::Playlist && drawRegion_>=1 && drawRegion_<=kP3AVisibleRows) {
        auto& row=context.rows[drawRegion_-1];
        if(row.valid){
            char finalText[128]{};
            if(!(row.displayFlags&1)){
            if(!fonts_.requestUiMetrics(row.label,12))return;
            CachedUiFont font(&fonts_);display_->setFont(&font);display_->setTextSize(1);
            const char marker[]={row.playing?'>':' ',row.type==LibraryEntryType::Directory?'/':' ',' ',0};
            const int width=display_->textWidth(marker);
            const auto layout=UiTextLayout::visitLines(*display_,row.label,
                {int16_t(7+width),50,int16_t(121-width),15,1,3,true},
                [](const char* text,void* p){std::strncpy(static_cast<char*>(p),text,127);},finalText);
            display_->setFont(&fonts::Font0);
            if(layout.invalidUtf8||layout.layoutError){navigationFailed("Playlist title layout error");return;}
            row.displayLength=std::strlen(finalText)-(layout.truncated?3:0);
            row.displayFlags=1|(layout.truncated?2:0);row.markerWidth=width;
            }else row.displayText(finalText);
            if(!fonts_.requestUiWindow(finalText,12,0,123,1,false,FontCache::Playlist))return;
        }
    }
    CachedUiFont font(&fonts_);display_->setFont(&font);
    const uint32_t started = micros();
    switch (page_) {
        case UiPage::Library:
            stats_.libraryText = LibraryPageRenderer::render(*display_, context);
            stats_.libraryTextIsBenchmark =
                std::strcmp(context.libraryName, "ADVWalkmanBenchmark") == 0;
            break;
        case UiPage::Playlist:
            PlaylistPageRenderer::renderRegion(*display_, context, drawRegion_);
            dirtyRegions_ &= ~(1U<<drawRegion_);
            if(feedbackRegions_){feedbackRegions_ &= ~(1U<<drawRegion_);
                if(!feedbackRegions_)stats_.selectionFeedbackMaxMs=std::max<uint32_t>(stats_.selectionFeedbackMaxMs,millis()-selectionRequestedAt_);}
            break;
        case UiPage::Player:
            // Player is handled by the bounded NowPlayingPresenter path.
            break;
        case UiPage::Settings:
            SettingsPageRenderer::render(*display_, context);
            break;
    }
    const uint32_t elapsed = micros() - started;
    display_->setFont(&fonts::Font0);
    if(page_!=UiPage::Playlist)fonts_.clearPins(FontCache::Ui);
    ++stats_.renderCount;
    stats_.renderMaxUs = std::max(stats_.renderMaxUs, elapsed);
    renderRetryRequested_ = page_ == UiPage::Playlist && dirtyRegions_;
    if (!renderRetryRequested_&&browserContextReady_) {
        if(warmReturnPending_){stats_.warmReturnMaxMs=std::max<uint32_t>(stats_.warmReturnMaxMs,millis()-pageRequestedAt_);warmReturnPending_=false;}
        if(!stats_.pageFirstFrameComplete)
            stats_.firstFrameMaxMs=std::max<uint32_t>(stats_.firstFrameMaxMs,millis()-pageRequestedAt_);
        stats_.pageFirstFrameComplete = true; ++stats_.completedPages;
        if(page_==UiPage::Playlist && navigation_.state==NavigationState::Ready)++stats_.playlistFrames;
        if(page_==UiPage::Library && navigation_.state==NavigationState::Ready)++stats_.libraryFrames;
    }
}

void UiCoordinator::prepareBrowser() {
    const uint32_t started = micros();
    if (page_ == UiPage::Player) return;
    if (navigation_.state == NavigationState::Loading) return;
    if (!browserContextReady_) {
        buildRenderContext(renderContext_);
        browserContextReady_ = !renderRetryRequested_;
        const uint32_t progress=static_cast<uint32_t>(prepareRow_+locateIndex_);
        if(progress!=browserProgress_){browserProgress_=progress;browserProgressAt_=millis();}
        if(!browserContextReady_ && millis()-browserProgressAt_>=5000 && navigation_.state==NavigationState::Ready)
            navigationFailed("Page load stalled");
        if(browserContextReady_) {dirty_=true;drawRegion_=0;}
    } else if (page_ == UiPage::Playlist && navigation_.state==NavigationState::Ready) {
        serviceSelectedMetadata();
    }
    stats_.prepareMaxUs = std::max<uint32_t>(stats_.prepareMaxUs, micros()-started);
}

void UiCoordinator::buildRenderContext(UiRenderContext& context) {
    renderRetryRequested_ = false;
    context.page = page_;
    context.hint = hint_[0] == '\0' ? nullptr : hint_;
    context.error = externalError_[0] == '\0' ? nullptr : externalError_;
    if(page_!=UiPage::Library||libraryVisualDirty_)setPath(context.libraryName, sizeof(context.libraryName), libraryName_);
    context.catalogIndex = catalog_.selectedIndex();
    context.catalogCount = catalog_.count();

    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() == LibraryState::Error && context.error == nullptr) {
        context.error = libraryErrorName(library.error());
    }
    if (page_ == UiPage::Library && navigation_.state == NavigationState::Ready && catalog_.count() && library.state() == LibraryState::Ready &&
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0) {
        LibraryDescriptor descriptor;
        if(libraryVisualDirty_){
        if (catalog_.selected(library, descriptor) == LibraryResult::Ok) {
            // descriptor is local: never leave a dangling pointer in the
            // context handed to the renderer after this function returns.
            setPath(context.libraryName, sizeof(context.libraryName),
                    descriptor.name);
            libraryVisual_.select(descriptor.path,libraryMoveDirection_,libraryPage_.token());libraryVisualDirty_=false;libraryMoveDirection_=0;
            libraryVisual_.setShortName(1,descriptor.name);
            if(catalog_.count()==1){libraryVisual_.setShortName(0,descriptor.name);libraryVisual_.setShortName(2,descriptor.name);}
        } else {
            renderRetryRequested_ = true;
        }
        }else if(libraryVisual_.shortNamesReady()!=7){
            const unsigned slot=(libraryVisual_.shortNamesReady()&1)?2:0;
            const size_t index=wheelIndex(catalog_.selectedIndex(),catalog_.count(),slot);
            const auto result=catalog_.at(index,library,descriptor);
            if(result==LibraryResult::Ok){libraryVisual_.setShortName(slot,descriptor.name);
                if(catalog_.count()==2)libraryVisual_.setShortName(2,descriptor.name);}
            else if(result==LibraryResult::Error){navigationFailed("Library neighbor read failed");return;}
        }
        if(libraryVisual_.shortNamesReady()!=7)renderRetryRequested_=true;
    }
    if(page_==UiPage::Library&&!catalog_.count()&&libraryVisualDirty_){libraryVisual_.select("/Music",0,libraryPage_.token());libraryVisualDirty_=false;context.libraryName[0]=0;}

    const PlayerSnapshot snapshot = player_->snapshot();
    context.playerState = playerStateName(snapshot.state);
    context.positionMs = snapshot.positionMs;
    context.currentTrack = lastCurrentTrack_;
    context.directoryPath = library.currentPath();
    context.playlistSelected = playlistSelected_;
    context.playlistCount = playlistCount();
    if (page_ == UiPage::Playlist && navigation_.state == NavigationState::Ready) {
        buildPlaylistRows(context);
    } else {
        for(auto& row:context.rows)row.valid=false;
    }
}

void UiCoordinator::buildPlaylistRows(UiRenderContext& context) {
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready) {
        return;
    }
    const size_t count = playlistCount();
    if (locateCurrent_) {
        if (locateIndex_ < count) {
            LibraryEntry entry;
            if (library.entryAt(physicalEntryIndex(locateIndex_),entry)!=LibraryResult::Ok) {renderRetryRequested_=true;return;}
            const char* basename=std::strrchr(lastCurrentTrack_,'/');
            if (entry.type==LibraryEntryType::Track && basename && std::strcmp(entry.name,basename+1)==0) {
                playlistSelected_=locateIndex_;locateCurrent_=false;context.playlistSelected=playlistSelected_;
            } else ++locateIndex_;
            renderRetryRequested_=true;return;
        }
        locateCurrent_=false;
    }
    const size_t windowStart = (playlistSelected_ / kP3AVisibleRows) *
                               kP3AVisibleRows;
    if (prepareRow_ == 0) {++stats_.windowBuilds;for(auto& row:context.rows) row.valid=false;}
    if (prepareRow_ < kP3AVisibleRows) {
        const size_t rowIndex = (playlistSelected_%kP3AVisibleRows+prepareRow_)%kP3AVisibleRows;
        const size_t logical = windowStart + rowIndex;
        if (logical >= count) {
            ++prepareRow_;renderRetryRequested_=prepareRow_<kP3AVisibleRows;return;
        }
        const size_t physical = physicalEntryIndex(logical);
        LibraryEntry entry;
        const LibraryResult result = library.entryAt(physical, entry);
        if (result == LibraryResult::Pending) {
            renderRetryRequested_ = true;
            return;
        }
        if (result != LibraryResult::Ok) {
            navigationFailed(libraryErrorName(library.error()));return;
        }
        PlaylistRenderRow& row = context.rows[rowIndex];
        row.playing = false;
        row.valid = true;
        row.displayFlags=0;
        row.selected = logical == playlistSelected_;
        row.type = entry.type;
        trackLabel(entry.name, row.label, sizeof(row.label));
        setPath(row.basename,sizeof(row.basename),entry.name);

        char path[kTrackPathCapacity] = {};
        if (entry.type == LibraryEntryType::Track &&
            std::snprintf(path,sizeof(path),"%s/%s",library.currentPath(),entry.name)<int(sizeof(path))) {
            row.playing = lastCurrentTrack_[0] != '\0' &&
                          std::strcmp(path, lastCurrentTrack_) == 0;
            Mp3Metadata metadata;Mp3MetadataError warning=Mp3MetadataError::None;
            if(libraryRuntime_->cachedMetadataForPath(path,metadata,&warning)&&metadata.title.value[0]){
                setPath(row.label,sizeof(row.label),metadata.title.value);metadataReadyRows_|=1U<<rowIndex;
            }
        }
        ++prepareRow_;
        renderRetryRequested_=prepareRow_<kP3AVisibleRows;
    }
}

bool UiCoordinator::beginSelectedLibrary() {
    MusicLibrary& library = libraryRuntime_->library();
    LibraryDescriptor descriptor;
    const LibraryResult selected = catalog_.selected(library, descriptor);
    if (selected == LibraryResult::Pending) {
        pendingIntent_ = PendingIntent::SelectLibrary;
        dirty_ = true;
        return true;
    }
    if (selected != LibraryResult::Ok) {
        navigationFailed("Library unavailable");
        return false;
    }
    setPath(libraryName_, sizeof(libraryName_), descriptor.name);
    setPath(libraryRoot_, sizeof(libraryRoot_), descriptor.path);
    uncategorized_ = descriptor.uncategorized;
    playlistSelected_ = 0;
    metadataReadyRows_=0;
    metadataRequested_ = false;

    return openBrowser(descriptor.path, UiPage::Playlist);
}

bool UiCoordinator::activatePlaylistEntry() {
    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready || playlistCount() == 0) {
        return false;
    }
    const size_t physical = physicalEntryIndex(playlistSelected_);
    LibraryEntry entry;
    const LibraryResult entryResult = library.entryAt(physical, entry);
    if (entryResult == LibraryResult::Pending) {
        pendingIntent_ = PendingIntent::ActivatePlaylistEntry;
        dirty_ = true;
        return true;
    }
    if (entryResult != LibraryResult::Ok) {
        navigationFailed("Entry unavailable");
        return false;
    }
    if (entry.type == LibraryEntryType::Directory) {
        char target[kTrackPathCapacity];
        if(std::snprintf(target,sizeof(target),"%s/%s",library.currentPath(),entry.name)>=int(sizeof(target)))return false;
        playlistSelected_ = 0;
        return openBrowser(target,UiPage::Playlist);
    }

    if (std::snprintf(pendingTrackPath_,sizeof(pendingTrackPath_),"%s/%s",library.currentPath(),entry.name)>=int(sizeof(pendingTrackPath_))) {
        setExternalError("Track path unavailable");
        return false;
    }
    setPath(lastPlaylistPath_, sizeof(lastPlaylistPath_), library.currentPath());
    savedPlaylistSelected_ = playlistSelected_;
    const LibraryResult result = libraryRuntime_->selectTrack(physical, true);
    if (result == LibraryResult::Error) {
        navigationFailed("Track selection failed");
        return false;
    }
    if (result == LibraryResult::Ok) {
        ++stats_.trackSelections;
        if(std::strcmp(lastCurrentTrack_,pendingTrackPath_)!=0)++stats_.differentTrackSelections;
        stats_.lastQueueCount = player_->snapshot().queueCount;
        setPage(UiPage::Player);
    } else {
        pendingNavigation_ = PendingNavigation::SelectTrack;
    }
    return true;
}

bool UiCoordinator::returnFromPlaylist() {
    if (std::strcmp(navigationTarget_, libraryRoot_) == 0 ||
        std::strcmp(navigationTarget_, MusicLibrary::kMusicRoot) == 0) {
        showLibrary();return true;
    }
    char parent[kTrackPathCapacity];
    if(!parentDirectoryOfTrack(navigationTarget_,parent,sizeof(parent))) {showLibrary();return true;}
    playlistSelected_ = 0;
    return openBrowser(parent,UiPage::Playlist);
}

bool UiCoordinator::restorePlaylistForCurrentTrack() {
    char current[kTrackPathCapacity] = {};
    if (!player_->currentPath(current, sizeof(current))) {
        setExternalError("No current track");
        return false;
    }
    if (lastPlaylistPath_[0] == '\0' &&
        !parentDirectoryOfTrack(current, lastPlaylistPath_,
                                sizeof(lastPlaylistPath_))) {
        setExternalError("Invalid track path");
        return false;
    }
    if (!firstLevelLibraryOfTrack(current, libraryRoot_,
                                  sizeof(libraryRoot_), uncategorized_)) {
        setExternalError("Track outside /Music");
        return false;
    }
    displayNameFromPath(libraryRoot_, libraryName_, sizeof(libraryName_));
    playlistSelected_ = savedPlaylistSelected_;
    return openBrowser(lastPlaylistPath_,UiPage::Playlist,true);
}

size_t UiCoordinator::playlistCount() const {
    if (libraryRuntime_ == nullptr) {
        return 0;
    }
    const MusicLibrary& library = libraryRuntime_->library();
    if (library.state() != LibraryState::Ready) {
        return 0;
    }
    return uncategorized_ &&
                   std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0
               ? library.trackCount()
               : library.entryCount();
}

size_t UiCoordinator::physicalEntryIndex(size_t logicalIndex) const {
    const MusicLibrary& library = libraryRuntime_->library();
    return uncategorized_ &&
                   std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0
               ? library.directoryCount() + logicalIndex
               : logicalIndex;
}

bool UiCoordinator::setPath(char* destination, size_t capacity,
                            const char* source) {
    if (destination == nullptr || capacity == 0 || source == nullptr) {
        return false;
    }
    const size_t length = std::strlen(source);
    if (length + 1 > capacity) {
        destination[0] = '\0';
        return false;
    }
    std::memcpy(destination, source, length + 1);
    return true;
}

bool UiCoordinator::parentDirectoryOfTrack(const char* track, char* output,
                                           size_t capacity) {
    if (track == nullptr || output == nullptr || capacity == 0) {
        return false;
    }
    const size_t rootLength = std::strlen(MusicLibrary::kMusicRoot);
    const size_t length = std::strlen(track);
    if (length <= rootLength + 1 || length + 1 > capacity ||
        std::strncmp(track, MusicLibrary::kMusicRoot, rootLength) != 0 ||
        track[rootLength] != '/' || std::strstr(track, "/../") != nullptr) {
        return false;
    }
    std::memcpy(output, track, length + 1);
    char* slash = std::strrchr(output, '/');
    if (slash == nullptr || slash < output + rootLength) {
        output[0] = '\0';
        return false;
    }
    *slash = '\0';
    return std::strlen(output) >= rootLength;
}

bool UiCoordinator::firstLevelLibraryOfTrack(const char* track, char* output,
                                             size_t capacity,
                                             bool& uncategorized) {
    char parent[kTrackPathCapacity] = {};
    if (!parentDirectoryOfTrack(track, parent, sizeof(parent))) {
        return false;
    }
    const size_t rootLength = std::strlen(MusicLibrary::kMusicRoot);
    if (std::strcmp(parent, MusicLibrary::kMusicRoot) == 0) {
        uncategorized = true;
        return std::snprintf(output, capacity, "%s", MusicLibrary::kMusicRoot) > 0;
    }
    const char* component = parent + rootLength + 1;
    const char* slash = std::strchr(component, '/');
    const size_t componentLength = slash == nullptr
                                       ? std::strlen(component)
                                       : static_cast<size_t>(slash - component);
    if (componentLength == 0 || rootLength + 1 + componentLength + 1 > capacity) {
        return false;
    }
    std::memcpy(output, MusicLibrary::kMusicRoot, rootLength);
    output[rootLength] = '/';
    std::memcpy(output + rootLength + 1, component, componentLength);
    output[rootLength + 1 + componentLength] = '\0';
    uncategorized = false;
    return true;
}

void UiCoordinator::displayNameFromPath(const char* path, char* output,
                                        size_t capacity) {
    if (output == nullptr || capacity == 0) {
        return;
    }
    if (path == nullptr || std::strcmp(path, MusicLibrary::kMusicRoot) == 0) {
        std::snprintf(output, capacity, "%s", "未分类");
        return;
    }
    const char* slash = std::strrchr(path, '/');
    std::snprintf(output, capacity, "%s", slash == nullptr ? path : slash + 1);
}

void UiCoordinator::serviceBackground(bool logIdle){
    // Saving remains active with the backlight off. Never stack it onto a
    // prepared lyric frame or an unfinished log write.
    const auto errors=settings_.returnErrors;
    if(!nowPlaying_.presentingLyrics())settings_.service(*player_,logIdle);
    if(errors!=settings_.returnErrors)p3dPrepared_=false;
    const uint8_t brightness=power_.asleep()?0:uint16_t(settings_.store.value.brightness)*255/100;
    if(!displayLifecycle_.pending()&&brightness!=appliedBrightness_){display_->setBrightness(brightness);appliedBrightness_=brightness;}
}
bool UiCoordinator::physicalActivity(uint64_t mask,uint32_t now){
    const bool was=power_.asleep(),swallowed=power_.swallowing();const bool allowed=power_.sample(mask,now,page_==UiPage::Player,settings_.store.value);
    if(power_.asleep()!=was){
        ++inputEpoch_;displayLifecycle_.request(power_.asleep());
        if(power_.asleep()&&sleepsByPage_[powerScene()]<255)++sleepsByPage_[powerScene()];
        if(!power_.asleep()){
            if(wake_.captured&&!wake_.firstFrame)++unfinishedWakes_;
            wake_=WakeStats{};wake_.captured=now;wakePage_=powerScene();
        }
    }
    if(swallowed&&!power_.swallowing())wake_.unlocked=now;
    return allowed;
}

void UiCoordinator::trackLabel(const char* name, char* output,
                               size_t capacity) {
    if (output == nullptr || capacity == 0) {
        return;
    }
    if (name == nullptr) {
        output[0] = '\0';
        return;
    }
    const size_t length = std::strlen(name);
    if (length >= capacity) {
        output[0] = '\0';
        return;
    }
    std::memcpy(output, name, length + 1);
    char* dot = std::strrchr(output, '.');
    if (dot != nullptr &&
        (std::strcmp(dot, ".mp3") == 0 || std::strcmp(dot, ".MP3") == 0)) {
        *dot = '\0';
    }
}

}  // namespace player
}  // namespace adv_walkman
