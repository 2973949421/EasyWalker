#include "UiCoordinator.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include "PlayerKeys.h"
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
    nowPlaying_.setActive(page_ == UiPage::Player, now);
    nowPlaying_.update(snapshot, current, *libraryRuntime_, now);
    if(page_==UiPage::Player&&!nowPlaying_.mediaStatus().viewPending)
        player_->setPreferredNowPlayingView(static_cast<uint8_t>(nowPlaying_.mediaStatus().preferred));
    // A call performs resource work OR drawing, never both in a single
    // uninterruptible UI slice. Ready lyric stripes do not access storage.
    resourceTurn_=!resourceTurn_;
    if(resourceTurn_ && !nowPlaying_.presentingLyrics()){
        if(page_==UiPage::Player)nowPlaying_.serviceMedia();
        else if(fonts_.busy()) fonts_.service();
        else if(pendingIntent_ != PendingIntent::None) servicePendingIntent();
        else if(page_==UiPage::Library && browserContextReady_ && p3dPrepared_) libraryVisual_.service();
        else prepareBrowser();
        return;
    }
    const char* error = externalError_;
    if (error[0] == '\0' && snapshot.error != PlayerError::None) {
        error = playerErrorName(snapshot.error);
    }
    if(error[0]=='\0' && (fonts_.stats().ioErrors || fonts_.stats().missing || fonts_.stats().capacityErrors))error=fonts_.failure().reason;
    nowPlaying_.setContent(hint_, error);
    if (library.state() != lastLibraryState_ ||
        library.currentGeneration() != lastLibraryGeneration_ ||
        snapshot.state != lastPlayerState_ ||
        std::strcmp(current, lastCurrentTrack_) != 0) {
        lastLibraryState_ = library.state();
        lastLibraryGeneration_ = library.currentGeneration();
        lastPlayerState_ = snapshot.state;
        setPath(lastCurrentTrack_, sizeof(lastCurrentTrack_), current);
        if (page_ != UiPage::Player) invalidateBrowser();
        dirty_ = true;
    }

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
            lastRenderAtMs_ = now;
            ++stats_.renderCount;
            stats_.renderMaxUs = std::max(stats_.renderMaxUs,
                                          nowPlaying_.stats().renderMaxUs);
        }
        dirty_ = false;
        return;
    }
    // Rate-limit animations, not glyph preparation. A failed font request is
    // not a painted frame and must not impose another 20ms wait per glyph.
    if ((page_==UiPage::Library||page_==UiPage::Settings) || dirty_) {
        renderRetryRequested_ = false;
        render();
        lastRenderAtMs_ = now;
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
        const auto route=playbackPageRoute(page_==UiPage::Player,page_==UiPage::Settings,before.hasCurrent);
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
                catalog_.move(-1);
                libraryVisualDirty_=true;libraryMoveDirection_=-1;
                invalidateBrowser();
                return true;
            }
            if (action == UiAction::Right) {
                catalog_.move(1);
                libraryVisualDirty_=true;libraryMoveDirection_=1;
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
    return stats_;
}

bool UiCoordinator::currentTrackPath(char* output, size_t capacity) const {
    return player_ != nullptr && player_->currentPath(output, capacity);
}

void UiCoordinator::notifyVolumeAdjusted(uint8_t volume, uint32_t nowMs) {
    // Receives the value AFTER the runtime applied it. Never simulates sound.
    nowPlaying_.notifyVolumeAdjusted(volume, nowMs);
}

void UiCoordinator::setPage(UiPage page) {
    if(page_==UiPage::Playlist && page==UiPage::Player)
        retainedPlaylist_=browserContextReady_;
    else if(page!=UiPage::Player)retainedPlaylist_=false;
    if (page_ != page) {
        page_ = page;
        ++stats_.pageTransitions;
        nowPlaying_.setActive(page == UiPage::Player, millis());
        if(page!=UiPage::Library)libraryVisual_.release();
        else libraryVisualDirty_=true;
        // A visible Player may have replaced the shared Metadata request.
        if (page == UiPage::Playlist) metadataRequested_ = false;
    }
    pageClearRequested_ = true;
    pageRequestedAt_=millis();loadingDrawn_=false;
    warmReturnPending_=false;feedbackRegions_=0;
    stats_.pageFirstFrameComplete = false;
    if(page!=UiPage::Player)invalidateBrowser();
}

void UiCoordinator::invalidateBrowser() {
    fonts_.clearPins(FontCache::Ui);
    browserContextReady_ = false; prepareRow_ = 0; drawRegion_ = 0;
    dirtyRegions_=255;
    feedbackRegions_=0;
    dirty_ = true;
    p3dPrepared_=false;
    browserProgress_ = 0; browserProgressAt_ = millis();
}

void UiCoordinator::selectVisibleMetadata(){
    selectedMetadataPath_[0]=selectedMetadataTitle_[0]=0;metadataRequested_=false;
    const auto& row=renderContext_.rows[playlistSelected_%kP3AVisibleRows];
    if(row.valid && row.type==LibraryEntryType::Track)
        std::snprintf(selectedMetadataPath_,sizeof(selectedMetadataPath_),"%s/%s",navigationTarget_,row.basename);
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
        const bool retained=retainedPlaylist_;
        setPage(page);browserContextReady_=retained;prepareRow_=kP3AVisibleRows;
        warmReturnPending_=true;++stats_.warmReturns;
        navigation_.begin(millis());navigation_.state=NavigationState::Ready;
        // The retained window can locate the current song without SD reads.
        const char* name=std::strrchr(lastCurrentTrack_,'/');bool found=false;
        for(auto& row:renderContext_.rows)row.playing=row.valid&&name&&!std::strcmp(row.basename,name+1);
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
    metadataRequested_ = false; selectedMetadataPath_[0] = selectedMetadataTitle_[0] = '\0';
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
    if (!selectedMetadataPath_[0]) return;
    if (!metadataRequested_) {
        libraryRuntime_->requestMetadataPath(selectedMetadataPath_);
        metadataRequested_ = true;
        return;
    }

    const Mp3MetadataStatus status = libraryRuntime_->metadataStatus();
    if (status.state == Mp3MetadataState::Ready) {
        Mp3Metadata metadata;
        if (libraryRuntime_->metadataForPath(selectedMetadataPath_, metadata) &&
            metadata.title.value[0] != '\0' &&
            std::strcmp(selectedMetadataTitle_, metadata.title.value) != 0) {
            std::strncpy(selectedMetadataTitle_, metadata.title.value,
                         sizeof(selectedMetadataTitle_) - 1);
            const size_t row = playlistSelected_ % kP3AVisibleRows;
            setPath(renderContext_.rows[row].label, sizeof(renderContext_.rows[row].label), selectedMetadataTitle_);
            dirtyRegions_ |= 1U<<(row+1); dirty_ = true;
        }
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
        if (!fonts_.requestUiWindow("加载中",12,0,123,1)) return;
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
        const bool painted=page_==UiPage::Library?libraryVisual_.render(*display_,nowPlaying_.sharedRow(),fonts_,context,millis()):
            settings_.render(*display_,nowPlaying_.sharedRow(),fonts_);
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
    if (!fonts_.requestUiWindow(fixed,12,0,2000,1)) return;
    if(page_==UiPage::Playlist && drawRegion_==7 && !context.playlistCount && !fonts_.requestUiWindow("暂无歌曲",12,0,123,1))return;
    if((page_!=UiPage::Playlist||drawRegion_==0)&&!fonts_.requestUiWindow(context.libraryName,12,0,page_==UiPage::Library?194:123,1)) return;
    if(page_!=UiPage::Playlist||drawRegion_==kP3AVisibleRows+1)
        for(const char* text:{context.hint,context.error})if(text&&!fonts_.requestUiWindow(text,12,0,2000,1))return;
    if(page_==UiPage::Settings && !fonts_.requestUiWindow(ADV_WALKMAN_VERSION,12,0,123,1))return;
    if(page_==UiPage::Playlist && drawRegion_>=1 && drawRegion_<=kP3AVisibleRows) {
        const auto& row=context.rows[drawRegion_-1];
        if(row.valid && !fonts_.requestUiWindow(row.label,12,0,123,1))return;
        if(row.valid){
            CachedUiFont font(&fonts_);display_->setFont(&font);display_->setTextSize(1);
            const char marker[]={row.playing?'>':' ',row.type==LibraryEntryType::Directory?'/':' ',' ',0};
            const int width=display_->textWidth(marker);
            struct Prepare{FontCache* fonts;bool ready;} prepare{&fonts_,true};
            UiTextLayout::visitLines(*display_,row.label,
                {int16_t(7+width),50,int16_t(121-width),15,1,3,true},
                [](const char* text,void* p){auto& c=*static_cast<Prepare*>(p);c.ready&=c.fonts->requestUiWindow(text,12,0,123,1);},&prepare);
            display_->setFont(&fonts::Font0);if(!prepare.ready)return;
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
    fonts_.clearPins(FontCache::Ui);
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
    setPath(context.libraryName, sizeof(context.libraryName), libraryName_);
    context.catalogIndex = catalog_.selectedIndex();
    context.catalogCount = catalog_.count();

    MusicLibrary& library = libraryRuntime_->library();
    if (library.state() == LibraryState::Error && context.error == nullptr) {
        context.error = libraryErrorName(library.error());
    }
    if (page_ == UiPage::Library && navigation_.state == NavigationState::Ready && catalog_.count() && library.state() == LibraryState::Ready &&
        std::strcmp(library.currentPath(), MusicLibrary::kMusicRoot) == 0) {
        LibraryDescriptor descriptor;
        if (catalog_.selected(library, descriptor) == LibraryResult::Ok) {
            // descriptor is local: never leave a dangling pointer in the
            // context handed to the renderer after this function returns.
            setPath(context.libraryName, sizeof(context.libraryName),
                    descriptor.name);
            if(libraryVisualDirty_){libraryVisual_.select(descriptor.path,libraryMoveDirection_);libraryVisualDirty_=false;libraryMoveDirection_=0;}
        } else {
            renderRetryRequested_ = true;
        }
    }
    if(page_==UiPage::Library&&!catalog_.count()&&libraryVisualDirty_){libraryVisual_.select("/Music");libraryVisualDirty_=false;context.libraryName[0]=0;}

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
        row.selected = logical == playlistSelected_;
        row.type = entry.type;
        trackLabel(entry.name, row.label, sizeof(row.label));
        setPath(row.basename,sizeof(row.basename),entry.name);

        char path[kTrackPathCapacity] = {};
        if (entry.type == LibraryEntryType::Track &&
            std::snprintf(path,sizeof(path),"%s/%s",library.currentPath(),entry.name)<int(sizeof(path))) {
            row.playing = lastCurrentTrack_[0] != '\0' &&
                          std::strcmp(path, lastCurrentTrack_) == 0;
            if (row.selected && std::strcmp(path, selectedMetadataPath_) == 0 &&
                selectedMetadataTitle_[0] != '\0') {
                setPath(row.label, sizeof(row.label), selectedMetadataTitle_);
            }
            if (row.selected && std::strcmp(path,selectedMetadataPath_)!=0) {
                setPath(selectedMetadataPath_,sizeof(selectedMetadataPath_),path);
                selectedMetadataTitle_[0]='\0';metadataRequested_=false;
            }
        } else if(row.selected) {
            selectedMetadataPath_[0]=selectedMetadataTitle_[0]='\0';metadataRequested_=false;
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
    selectedMetadataPath_[0] = '\0';
    selectedMetadataTitle_[0] = '\0';
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
    if(brightness!=appliedBrightness_){display_->setBrightness(brightness);appliedBrightness_=brightness;}
}
bool UiCoordinator::physicalActivity(uint64_t mask,uint32_t now){
    const bool was=power_.asleep();const bool allowed=power_.sample(mask,now,page_==UiPage::Player,settings_.store.value);
    if(power_.asleep()!=was){
        if(power_.asleep()){nowPlaying_.setActive(false,now);libraryVisual_.release();}
        else{pageClearRequested_=true;invalidateBrowser();libraryVisualDirty_=true;settings_.invalidate();nowPlaying_.invalidateDisplay();}
        display_->setBrightness(power_.asleep()?0:uint16_t(settings_.store.value.brightness)*255/100);
        appliedBrightness_=power_.asleep()?0:uint16_t(settings_.store.value.brightness)*255/100;
    }
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
