#include "FreeSession.h"
#include "player/ui/InputEdges.h"
#include "player/ui/RuntimeDiagnostics.h"
#include <esp_heap_caps.h>
#include "player/ui/media/MediaTypes.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
namespace adv_walkman { namespace player {
namespace {constexpr const char* kLog="/ADVWalkman/logs/p3-free-last.txt";}
void FreeSession::begin(){
    started_=lastSaved_=millis();inputCheck_=checkInputEdges();
    if(!inputCheck_)fail("input","edge_selfcheck");
    if(!SD.exists("/ADVWalkman/logs"))SD.mkdir("/ADVWalkman/logs");
    // Stream the history once at boot.  A full snapshot is deliberately much
    // larger than the one-KiB scratch, so a tail-only search can miss boot_id.
    auto previous=SD.open(kLog,"r");if(previous){
        static const char needle[]="boot_id=";uint8_t match=0;bool digits=false;uint32_t value=0;
        while(previous.available()){
            const int got=previous.read(reinterpret_cast<uint8_t*>(buffer_),sizeof(buffer_));if(got<=0)break;
            for(int i=0;i<got;++i){const char ch=buffer_[i];
                if(digits){if(ch>='0'&&ch<='9')value=value*10+unsigned(ch-'0');
                    else{bootId_=std::max(bootId_,value+1);digits=false;value=0;match=ch==needle[0]?1:0;}}
                else if(ch==needle[match]){if(++match==sizeof(needle)-1){digits=true;match=0;}}
                else match=ch==needle[0]?1:0;
            }
        }
        if(digits)bootId_=std::max(bootId_,value+1);previous.close();
    }
    auto f=SD.open(kLog,"a");if(f){f.println();f.close();}else fail("logging","open_log");
}
void FreeSession::fail(const char* component,const char* reason){
    if(failure_[0])return;
    std::snprintf(component_,sizeof(component_),"%s",component);
    std::snprintf(failure_,sizeof(failure_),"%s",reason);
    requestSave_=true;
}
void FreeSession::action(UiAction action,const RawKeyEvent& raw,UiPage page,bool accepted){
    events_[eventCount_%32]={millis()-started_,action,page,raw.x,raw.y,accepted,raw.capturedAtMs};++eventCount_;++actions_;
    if(action==UiAction::SaveDiagnostics)return;
    if(action==UiAction::ToggleView&&!accepted&&page==UiPage::Player)noLyricsPending_=true;
    if(!accepted)return;
    switch(action){
        case UiAction::Left:nav_|=1;break;case UiAction::Right:nav_|=2;break;
        case UiAction::Up:nav_|=4;break;case UiAction::Down:nav_|=8;break;
        case UiAction::Confirm:nav_|=16;break;case UiAction::Back:nav_|=32;break;
        case UiAction::OpenSettings:nav_|=64;break;
        case UiAction::VolumeUp:case UiAction::VolumeDown:++volumeEvents_;break;
        case UiAction::TogglePlayback:++playEvents_;break;
        case UiAction::ToggleView:++viewEvents_;break;default:break;
    }
}
void FreeSession::observe(const UiCoordinator& ui,const PlayerRuntime& player){
    const uint32_t now=millis();const auto s=player.snapshot();const auto m=ui.nowPlaying().mediaStatus();
    const auto& font=ui.fonts();
    if(!startupCaptured_){startupCaptured_=true;player.currentPath(restoredTrack_,sizeof(restoredTrack_));restoredPosition_=s.positionMs;
        restoredView_=previousPreferred_=player.preferredNowPlayingView();startupPaused_=s.state==PlayerState::Paused||s.state==PlayerState::Empty;}
    if(!actions_){startupObservedMs_=std::min<uint32_t>(3000,now-started_);if(s.pcmBuffersSinceReset)startupSilent_=false;}
    if(noLyricsPending_){if(m.lyrics==MediaState::Missing&&m.view==MediaView::Cover&&player.preferredNowPlayingView()==previousPreferred_)++noLyricsView_;noLyricsPending_=false;}
    if(ui.nowPlaying().model().active && m.frames && m.view==MediaView::Lyrics && ui.nowPlaying().model().headerVisible)fail("layout","lyrics_header_visible");
    minimumHeap_=std::min(minimumHeap_,ESP.getFreeHeap());
    if(s.pcmSubmitGapMaxUs>pcmGapMaxUs_){pcmPeakAt_=now-started_;pcmPeakGeneration_=m.generation;pcmPeakPage_=ui.page();player.currentPath(pcmPeakTrack_,sizeof(pcmPeakTrack_));}
    pcmGapMaxUs_=std::max(pcmGapMaxUs_,s.pcmSubmitGapMaxUs);
    backpressure_=std::max(backpressure_,s.backpressureEvents);audioErrors_=std::max(audioErrors_,s.audioErrorEvents);
    const char* path=ui.nowPlaying().model().path;
    const bool changed=std::strcmp(track_,path)!=0;
    if(changed){if(track_[0]&&previousPreferred_==player.preferredNowPlayingView())++preferenceTransitions_;std::snprintf(track_,sizeof(track_),"%s",path);playing_=false;loading_=false;}
    previousPreferred_=player.preferredNowPlayingView();
    if(s.state==PlayerState::Playing && s.sampleRateHz==44100){
        if(!playing_){playing_=true;playingAt_=now;}
        longestPlaying_=std::max(longestPlaying_,now-playingAt_);
    }else playing_=false;
    if(s.audioError!=AudioError::None || s.error!=PlayerError::None || audioErrors_)fail("audio","player_audio_error");
    if(backpressure_)fail("audio","backpressure");
    if(player.rawSpeakerVolume()!=VolumePolicy::toRaw(player.volume()))fail("audio","volume_policy");
    if(pcmGapMaxUs_>70000)fail("audio","pcm_gap_over_70ms");
    if(playing_ && now-playingAt_>2000 && s.pcmLastSubmitAgeUs>2000000)fail("audio","pcm_stalled");
    const auto u=ui.stats();
    if(!textSeen_&&u.displaySelfChecks>=3&&!u.displaySelfFailure){textSeen_=textOk_=true;}
    if(m.viewFailures)fail("view",m.viewFailure);
    if(ui.inputOverflow())fail("input","event_queue_overflow");
    if(u.completedPages!=lastPageCount_ || u.trackSelections!=lastSelections_ || u.navigationErrors!=lastNavigationErrors_){
        // Keep evidence in RAM; the periodic/manual checkpoint coalesces a
        // burst of highlight/page changes instead of writing on each row.
        lastPageCount_=u.completedPages;lastSelections_=u.trackSelections;lastNavigationErrors_=u.navigationErrors;
    }
    if(u.tabStateErrors)fail("navigation","tab_changed_transport");
    for(unsigned i=0;i<2;++i)if((i?m.cover:m.lyrics)==MediaState::Error && lastErrorGeneration_[i]!=m.generation){
        lastErrorGeneration_[i]=m.generation;auto& e=mediaErrors_[i];
        if(!e.count){e.failure=i?ui.nowPlaying().cover().failure():ui.nowPlaying().lyrics().failure();
            if(i)std::snprintf(e.path,sizeof(e.path),"%s",ui.nowPlaying().cover().path());
            else ui.nowPlaying().lyrics().failurePath(e.path,sizeof(e.path));
            e.generation=m.generation;e.at=now-started_;
        }
        ++e.count;
    }
    if(u.navigationErrors){
        auto& e=mediaErrors_[3];
        if(!e.count){std::snprintf(firstNavigationReason_,sizeof(firstNavigationReason_),"%s",ui.navigationError());
            e.failure.set(firstNavigationReason_,"navigate");std::snprintf(e.path,sizeof(e.path),"%s",ui.navigationTarget());e.at=now-started_;e.generation=u.navigationGeneration;}
        e.count=u.navigationErrors;
        if(!failure_[0])std::snprintf(resourcePath_,sizeof(resourcePath_),"%s",ui.navigationTarget());
        fail("navigation",ui.navigationError());
    }
    if(u.displaySelfFailure)fail("display_selfcheck",u.displaySelfFailure);
    if(u.libraryTextIsBenchmark){textSeen_=true;libraryWidth_=u.libraryText.maxLineWidthPx;
        textOk_=u.libraryText.lineCount==2 && libraryWidth_<=u.libraryText.availableWidthPx &&
            !u.libraryText.truncated && !u.libraryText.invalidUtf8 && !u.libraryText.layoutError;
        if(!textOk_)fail("layout","library_text_layout");
    }
    if(font.stats().missing||font.stats().ioErrors||font.stats().capacityErrors||font.stats().drawMisses){
        auto& e=mediaErrors_[2];
        if(!e.count){e.failure=font.failure();e.at=now-started_;e.generation=m.generation;
            if(font.stats().drawMisses){e.failure.set("glyph_draw_missing","draw");std::snprintf(e.path,sizeof(e.path),"glyph:face=%u,cp=U+%04lx,page=%s",font.stats().firstDrawFont,(unsigned long)font.stats().firstDrawCodepoint,uiPageName(ui.page()));}
            else font.failurePath(e.path,sizeof(e.path));}
        e.count=font.stats().missing+font.stats().ioErrors+font.stats().capacityErrors+font.stats().drawMisses;
        if(!failure_[0]){
            if(font.stats().drawMisses)std::snprintf(resourcePath_,sizeof(resourcePath_),"glyph:face=%u,cp=U+%04lx,page=%s",font.stats().firstDrawFont,(unsigned long)font.stats().firstDrawCodepoint,uiPageName(ui.page()));
            else font.failurePath(resourcePath_,sizeof(resourcePath_));
            resourceErrno_=font.failure().systemError;resourceExpected_=font.failure().expected;resourceActual_=font.failure().actual;std::snprintf(resourceOperation_,sizeof(resourceOperation_),"%s",font.failure().operation);
        }
        fail("font",font.stats().drawMisses?"glyph_draw_missing":font.stats().capacityErrors?"glyph_cache_capacity":font.failure().reason);
    }
    if(ui.settings().store.errors)fail("settings",ui.settings().store.error());
    if(ui.settings().returnErrors)fail("launcher",ui.settings().returnError());
    if(ui.libraryVisual().cover.errors){const auto& cover=ui.libraryVisual().cover;
        if(!failure_[0]){std::snprintf(resourcePath_,sizeof(resourcePath_),"%s",cover.path());resourceErrno_=cover.failure().systemError;
            std::snprintf(resourceOperation_,sizeof(resourceOperation_),"%s",cover.failure().operation);}
        fail("library_cover",cover.failure().reason);}
    if(ui.nowPlaying().stats().frameRejects)fail("render",ui.nowPlaying().stats().frameFailure);
    if(ui.page()!=UiPage::Player||ui.screenPower().asleep()){loading_=false;return;}
    lyricFrames_=std::max(lyricFrames_,m.lyricsFrames);coverFrames_=std::max(coverFrames_,m.coverFrames);
    deadlineUpdates_=std::max(deadlineUpdates_,m.deadlineUpdates);
    if(m.lyricLateMaxMs>lateMaxMs_){player.currentPath(lyricPeakTrack_,sizeof(lyricPeakTrack_));lyricPeakGeneration_=m.generation;lyricPeakDue_=m.lyricDueMs;lyricPeakPrepared_=m.lyricPreparedMs;lyricPeakSubmitted_=m.lyricSubmittedMs;}
    prepareMaxUs_=std::max(prepareMaxUs_,m.prepareMaxUs);presentMaxUs_=std::max(presentMaxUs_,m.presentMaxUs);lateMaxMs_=std::max(lateMaxMs_,m.lyricLateMaxMs);
    lyricsReady_|=m.lyrics==MediaState::Ready;coverReady_|=m.cover==MediaState::Ready;
    if(m.layoutError||m.invalidUtf8)fail("layout","lyric_layout");
    if(m.presentIoViolations)fail("media","io_during_lyric_present");
    if(m.presentMaxUs>100000)fail("media","lyric_present_over_100ms");
    if(m.lyricLateMaxMs>200 || m.missedDeadlines)fail("media","lyric_deadline_over_200ms");
    if(m.lyrics==MediaState::Error || m.cover==MediaState::Error){
        const bool lyric=m.lyrics==MediaState::Error;
        const auto& f=lyric?ui.nowPlaying().lyrics().failure():ui.nowPlaying().cover().failure();
        if(!failure_[0]){if(lyric)ui.nowPlaying().lyrics().failurePath(resourcePath_,sizeof(resourcePath_));else std::snprintf(resourcePath_,sizeof(resourcePath_),"%s",ui.nowPlaying().cover().path());resourceErrno_=f.systemError;resourceExpected_=f.expected;resourceActual_=f.actual;std::snprintf(resourceOperation_,sizeof(resourceOperation_),"%s",f.operation);}
        fail(lyric?"lyrics":"cover",f.reason);
    }
    const bool pending=m.lyrics==MediaState::Loading||m.cover==MediaState::Loading;
    if(pending){
        if(!loading_||loadingGeneration_!=m.generation){loading_=true;loadingGeneration_=m.generation;loadAt_=now;}
        if(now-loadAt_>10000)fail("media",m.lyrics==MediaState::Loading?"lyrics_loading_stalled":"cover_loading_stalled");
    }else loading_=false;
}
void FreeSession::append(const char* fmt,...){
    if(overflow_)return;va_list args;va_start(args,fmt);
    const int n=std::vsnprintf(buffer_+length_,sizeof(buffer_)-length_,fmt,args);va_end(args);
    if(n<0 || size_t(n)>=sizeof(buffer_)-length_){overflow_=true;return;}length_+=n;
}
void FreeSession::beginSnapshot(const UiCoordinator&,const PlayerRuntime&,bool detailed){
    length_=written_=0;overflow_=false;++sequence_;streamSection_=0;
    streamDetailed_=detailed;streamFinalChunk_=false;streamCrc_=~0U;
}

void FreeSession::prepareNext(const UiCoordinator& ui,const PlayerRuntime& player){
    length_=written_=0;overflow_=false;streamFinalChunk_=false;
    const auto s=player.snapshot();const auto m=ui.nowPlaying().mediaStatus();const auto& r=ui.nowPlaying().stats();
    const auto u=ui.stats();const auto library=ui.browser().stats();const auto& ds=ui.settings();
    const auto& power=ui.screenPower();const auto& visual=ui.libraryVisual();const auto& wake=ui.wakeStats();
    const bool a=textSeen_&&textOk_&&u.playlistFrames&&u.libraryFrames&&u.differentTrackSelections;
    const bool b=longestPlaying_>=60000&&volumeEvents_&&playEvents_&&r.titleDraws&&r.timeDraws;
    const bool c=lyricsReady_&&coverReady_&&lyricFrames_&&coverFrames_&&deadlineUpdates_&&viewEvents_;
    const bool d=visual.coverFrames&&visual.frames&&ds.changes&&ds.store.writes&&power.sleeps&&power.wakes;
    const char* result=failure_[0]?"FAIL":a&&b&&c&&d?"READY_FOR_REVIEW":"INCOMPLETE";
    if(!streamDetailed_){
        switch(streamSection_++){
            case 0:
                append("BEGIN sequence=%lu\nsnapshot=summary\nschema=2\nversion=%s\nmode=free\nboot_id=%lu\nelapsed_ms=%lu\nresult=%s\nfailure_component=%s\nfailure_reason=%s\n",
                    (unsigned long)sequence_,ADV_WALKMAN_VERSION,(unsigned long)bootId_,(unsigned long)(millis()-started_),result,
                    component_[0]?component_:"none",failure_[0]?failure_:"none");
                append("page=%s\nplayer_state=%s\nposition_ms=%lu\naudio_errors=%lu\nbackpressure=%lu\npcm_gap_max_us=%lu\npcm_buffers=%lu\n",
                    uiPageName(ui.page()),playerStateName(s.state),(unsigned long)s.positionMs,(unsigned long)audioErrors_,
                    (unsigned long)backpressure_,(unsigned long)pcmGapMaxUs_,(unsigned long)s.pcmBuffersSinceReset);return;
            case 1:
                append("library_requests=%lu\nlibrary_stalls=%lu\nlibrary_recoveries=%lu\nlibrary_failures=%lu\nlibrary_stale_rejects=%lu\nlibrary_transaction_state=%u\n",
                    (unsigned long)u.libraryRequests,(unsigned long)u.libraryStalls,(unsigned long)u.libraryRecoveries,
                    (unsigned long)u.libraryFailures,(unsigned long)u.libraryStaleRejects,unsigned(visual.transaction().state()));
                append("save_requested_ticket=%lu\nsave_completed_ticket=%lu\nsave_status=%u\ninput_accept_max_ms=%lu\nwarm_return_max_ms=%lu\nview_warm_max_ms=%lu\nselection_feedback_max_ms=%lu\nminimum_heap=%lu\n",
                    (unsigned long)save_.requested(),(unsigned long)save_.completed(),unsigned(save_.status()),
                    (unsigned long)ui.inputLatencyMaxMs(),(unsigned long)u.warmReturnMaxMs,(unsigned long)m.viewWarmMaxMs,
                    (unsigned long)u.selectionFeedbackMaxMs,(unsigned long)minimumHeap_);return;
            default:
                streamFinalChunk_=true;append("END sequence=%lu crc=%08lx\n",(unsigned long)sequence_,(unsigned long)(streamCrc_^~0U));return;
        }
    }
    switch(streamSection_++){
        case 0:
            append("BEGIN sequence=%lu\nsnapshot=full\nschema=2\nversion=%s\nmode=free\nresult=%s\nhuman_review=PENDING\nboot_id=%lu\nmanual_checkpoint=%u\na_auto=%s\nb_auto=%s\nc_auto=%s\nd_auto=%s\n",
                (unsigned long)sequence_,ADV_WALKMAN_VERSION,result,(unsigned long)bootId_,writingManual_,a?"COVERED":"INCOMPLETE",b?"COVERED":"INCOMPLETE",c?"COVERED":"INCOMPLETE",d?"COVERED":"INCOMPLETE");return;
        case 1:
            append("render_contract=1\nrender_pixel_selfcheck=%s\nframe_starts=%lu\nframe_cancels=%lu\nframe_rejects=%lu\nframe_repairs=%lu\nframe_failure=%s\nframe_id=%lu\nframe_generation=%lu\nframe_pending=%u\nframe_expected_rows=%u\nframe_submitted_rows=%u\nfull_frames=%lu\npatch_frames=%lu\nfallback_frames=%lu\nframe_start_ms=%lu\nframe_first_ms=%lu\nframe_complete_ms=%lu\n",
                u.displaySelfFailure?u.displaySelfFailure:"PASS",(unsigned long)r.frameStarts,(unsigned long)r.frameCancels,(unsigned long)r.frameRejects,(unsigned long)r.frameRepairs,r.frameFailure,
                (unsigned long)m.frameId,(unsigned long)m.generation,ui.nowPlaying().framePending(),ui.nowPlaying().expectedRows(),ui.nowPlaying().submittedRows(),
                (unsigned long)m.frames,(unsigned long)m.patchFrames,(unsigned long)m.fallbackFrames,(unsigned long)r.frameStartedAt,(unsigned long)r.frameFirstAt,(unsigned long)r.frameCompletedAt);return;
        case 2:
            append("input_accept_max_ms=%lu\ninput_queue_overflow=%lu\ninput_stale_events=%lu\nwarm_returns=%lu\nwarm_return_max_ms=%lu\nselection_feedback_max_ms=%lu\npage_first_frame_max_ms=%lu\nview_pending=%u\nview_coalesced=%lu\nview_warm_max_ms=%lu\nview_cold_max_ms=%lu\nview_warm_completed=%lu\nview_cold_completed=%lu\nview_failures=%lu\nview_failure=%s\ntab_events=%lu\ntab_state_errors=%lu\ntab_playing=%lu\ntab_paused=%lu\n",
                (unsigned long)ui.inputLatencyMaxMs(),(unsigned long)ui.inputOverflow(),(unsigned long)ui.inputStale(),(unsigned long)u.warmReturns,
                (unsigned long)u.warmReturnMaxMs,(unsigned long)u.selectionFeedbackMaxMs,(unsigned long)u.firstFrameMaxMs,m.viewPending,
                (unsigned long)m.viewCoalesced,(unsigned long)m.viewWarmMaxMs,(unsigned long)m.viewColdMaxMs,(unsigned long)m.viewWarmCompleted,
                (unsigned long)m.viewColdCompleted,(unsigned long)m.viewFailures,m.viewFailure,(unsigned long)u.tabEvents,(unsigned long)u.tabStateErrors,
                (unsigned long)u.tabPlaying,(unsigned long)u.tabPaused);return;
        case 3:
            append("library_requests=%lu\nlibrary_stalls=%lu\nlibrary_recoveries=%lu\nlibrary_failures=%lu\nlibrary_stale_rejects=%lu\nlibrary_transaction_state=%u\nlibrary_cover_validation_hits=%lu\nlibrary_cover_stale_rejects=%lu\nlibrary_cover_frames=%lu\nvinyl_frames=%lu\nlibrary_cover_state=%u\nlibrary_cover_errors=%lu\nlibrary_cover_service_max_us=%lu\nwindow_builds=%lu\nhighlight_updates=%lu\nplaylist_frames=%lu\nlibrary_frames=%lu\ntrack_selections=%lu\ndifferent_track_selections=%lu\n",
                (unsigned long)u.libraryRequests,(unsigned long)u.libraryStalls,(unsigned long)u.libraryRecoveries,(unsigned long)u.libraryFailures,
                (unsigned long)u.libraryStaleRejects,unsigned(visual.transaction().state()),(unsigned long)visual.cover.validationHits,
                (unsigned long)visual.cover.staleRejects,(unsigned long)visual.coverFrames,(unsigned long)visual.frames,unsigned(visual.cover.state()),
                (unsigned long)visual.cover.errors,(unsigned long)visual.cover.serviceMaxUs,(unsigned long)u.windowBuilds,(unsigned long)u.highlightUpdates,
                (unsigned long)u.playlistFrames,(unsigned long)u.libraryFrames,(unsigned long)u.trackSelections,(unsigned long)u.differentTrackSelections);return;
        case 4:
            append("longest_playing_ms=%lu\naudio_errors=%lu\nbackpressure=%lu\npcm_gap_max_us=%lu\npcm_buffers=%lu\naudio_source_read_max_us=%lu\naudio_service_max_us=%lu\ntransport_service_max_us=%lu\npersistence_service_max_us=%lu\nlyrics_state=%u\ncover_state=%u\nlyrics_frames=%lu\ncover_frames=%lu\nlyric_deadline_updates=%lu\nprepare_max_us=%lu\npresent_max_us=%lu\nlyric_late_max_ms=%lu\nresource_bytes=%lu\nmedia_service_max_us=%lu\n",
                (unsigned long)longestPlaying_,(unsigned long)audioErrors_,(unsigned long)backpressure_,(unsigned long)pcmGapMaxUs_,
                (unsigned long)s.pcmBuffersSinceReset,(unsigned long)player.sourceReadMaxUs(),(unsigned long)audioMax_,
                (unsigned long)player.transportServiceMaxUs(),(unsigned long)player.persistenceServiceMaxUs(),unsigned(m.lyrics),unsigned(m.cover),
                (unsigned long)lyricFrames_,(unsigned long)coverFrames_,(unsigned long)deadlineUpdates_,(unsigned long)prepareMaxUs_,
                (unsigned long)presentMaxUs_,(unsigned long)lateMaxMs_,(unsigned long)m.reads,(unsigned long)m.serviceMaxUs);return;
        case 5:
            append("nav_state=%u\nnav_generation=%lu\nnav_errors=%lu\nnav_error=%s\npage_frame_complete=%u\nqueue_count=%u\nselected_queue_count=%lu\ncurrent_index=%u\ntime_font_px=14\nbrowser_state=%u\nbrowser_error=%u\nlast_browser_error=%lu\nscanned_entries=%lu\nscratch_allocation_failures=%lu\nbrowser_prepare_max_us=%lu\nbrowser_draw_max_us=%lu\nscan_max_us=%lu\nentry_read_max_us=%lu\nnavigation_work_max_us=%lu\n",
                unsigned(u.navigationState),(unsigned long)u.navigationGeneration,(unsigned long)u.navigationErrors,ui.navigationError()[0]?ui.navigationError():"none",
                u.pageFirstFrameComplete,unsigned(s.queueCount),(unsigned long)u.lastQueueCount,unsigned(s.currentIndex),unsigned(ui.browser().state()),unsigned(ui.browser().error()),
                (unsigned long)u.lastLibraryError,(unsigned long)library.scannedEntries,(unsigned long)library.scratchAllocationFailures,(unsigned long)u.prepareMaxUs,
                (unsigned long)u.renderMaxUs,(unsigned long)library.scanMaxUs,(unsigned long)library.entryReadMaxUs,(unsigned long)u.navigationMaxUs);return;
        case 6:
            append("brightness=%u\nplayer_timeout_ms=%lu\nother_timeout_ms=%lu\ndisplay_settings_loaded=%u\nrestored_brightness=%u\nrestored_player_timeout_ms=%lu\nrestored_other_timeout_ms=%lu\nsettings_changes=%lu\nsettings_writes=%lu\nsettings_errors=%lu\nsettings_saved=%u\nsettings_service_max_us=%lu\nscreen_asleep=%u\nwake_swallowing=%u\nscreen_sleeps=%lu\nscreen_wakes=%lu\nlauncher_requests=%lu\nlauncher_errors=%lu\nlauncher_error=%s\n",
                ds.store.value.brightness,(unsigned long)displayTimeoutMs(ds.store.value.playerTimeout),(unsigned long)displayTimeoutMs(ds.store.value.otherTimeout),ds.store.loaded,
                ds.store.restored.brightness,(unsigned long)displayTimeoutMs(ds.store.restored.playerTimeout),(unsigned long)displayTimeoutMs(ds.store.restored.otherTimeout),
                (unsigned long)ds.changes,(unsigned long)ds.store.writes,(unsigned long)ds.store.errors,ui.displaySettingsSaved(),(unsigned long)ds.store.serviceMaxUs,
                power.asleep(),power.swallowing(),(unsigned long)power.sleeps,(unsigned long)power.wakes,(unsigned long)ds.returnRequests,(unsigned long)ds.returnErrors,ds.returnError());return;
        case 7:{
            const char* scenes[]={"lyrics","cover","playlist","library","settings"};
            append("reset_reason=%lu\nprevious_phase_valid=%u\nprevious_phase=%lu\nprevious_phase_ms=%lu\nwake_resume_pcm=%lu\nwake_unfinished_count=%lu\nwake_captured_ms=%lu\nwake_backlight_ms=%lu\nwake_resume_ms=%lu\nwake_first_frame_ms=%lu\nwake_unlock_ms=%lu\nwake_resume_position_ms=%lu\nwake_complete=%s\n",
                (unsigned long)runtimeDiagnostics.resetReason,runtimeDiagnostics.previousValid,(unsigned long)runtimeDiagnostics.previousPhase,(unsigned long)runtimeDiagnostics.previousMs,
                (unsigned long)wake.pcm,(unsigned long)ui.unfinishedWakes(),(unsigned long)wake.captured,(unsigned long)wake.backlight,(unsigned long)wake.resume,
                (unsigned long)wake.firstFrame,(unsigned long)wake.unlocked,(unsigned long)wake.position,!power.wakes?"NA":wake.firstFrame&&wake.unlocked?"YES":"PENDING");
            for(unsigned i=0;i<5;++i)append("sleep_%s=%u\nwake_%s=%u\n",scenes[i],ui.sleepsOn(i),scenes[i],ui.wakesOn(i));return;}
        case 8:{
            static const char* phases[]={"idle","queue_path_prepare","open_target","write_header","write_payload","close_target","open_verify","read_verify","check_size","close_resize","create_target","flush_target","read_header","queue_path_fetch","publish","cleanup_close"};
            append("save_requested_ticket=%lu\nsave_active_ticket=%lu\nsave_completed_ticket=%lu\nsave_status=%u\nstate_writes=%lu\n",
                (unsigned long)save_.requested(),(unsigned long)save_.activeThrough(),(unsigned long)save_.completed(),unsigned(save_.status()),(unsigned long)player.stateWriteCount());
            for(unsigned i=1;i<16;++i)append("store_%s_max_us=%lu\n",phases[i],(unsigned long)player.persistencePhasePeakUs(i));return;}
        case 9:
            append("failure_component=%s\nfailure_reason=%s\nresource_operation=%s\nresource_errno=%d\nresource_expected=%ld\nresource_actual=%ld\nlyrics_failure=%s\ncover_failure=%s\nfont_failure=%s\nnavigation_failure=%s\ndisplay_selfchecks=%u\ndisplay_self_failure=%s\nfont_io_errors=%lu\nfont_draw_misses=%lu\nmetadata_fallbacks=%lu\nmetadata_fallback_cause=%u\n",
                component_[0]?component_:"none",failure_[0]?failure_:"none",resourceOperation_[0]?resourceOperation_:"NA",resourceErrno_,(long)resourceExpected_,(long)resourceActual_,
                mediaErrors_[0].count?mediaErrors_[0].failure.reason:"none",mediaErrors_[1].count?mediaErrors_[1].failure.reason:"none",
                mediaErrors_[2].count?mediaErrors_[2].failure.reason:"none",mediaErrors_[3].count?mediaErrors_[3].failure.reason:"none",
                u.displaySelfChecks,u.displaySelfFailure?u.displaySelfFailure:"none",(unsigned long)ui.fonts().stats().ioErrors,
                (unsigned long)ui.fonts().stats().drawMisses,(unsigned long)ui.metadataFallbacks(),ui.metadataFallbackCause());return;
        case 10:
            append("elapsed_ms=%lu\npage=%s\nplayer_state=%s\nposition_ms=%lu\nsample_rate=%lu\nvolume=%u\npreferred_view=%u\neffective_view=%u\nheader_visible=%u\nrepeat_raw=%u\nshuffle_raw=%u\nspeaker_volume_raw=%u\nspeaker_volume_cap=%u\nminimum_heap=%lu\nheap_free=%lu\nheap_largest_block=%lu\nmedia_budget_bytes=%u\nmedia_plus_events_bytes=%lu\ninput_selfcheck=%u\nactions=%lu\nnav_mask=%lu\nvolume_events=%lu\nplay_events=%lu\nview_events=%lu\nlibrary_text_seen=%u\nlibrary_text_ok=%u\nno_lyrics_view_noop=%lu\npreference_track_transitions=%lu\ncoverage_scope=free_main_path\nnot_exercised=playing_seek;manual_reboot_requires_host_comparison\nspeaker_distortion=DEFERRED\n",
                (unsigned long)(millis()-started_),uiPageName(ui.page()),playerStateName(s.state),(unsigned long)s.positionMs,(unsigned long)s.sampleRateHz,player.volume(),player.preferredNowPlayingView(),unsigned(m.view),
                ui.nowPlaying().model().headerVisible,unsigned(s.repeatMode),s.shuffleEnabled,player.rawSpeakerVolume(),VolumePolicy::maximumRaw,(unsigned long)minimumHeap_,(unsigned long)ESP.getFreeHeap(),
                (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),unsigned(kP3DMediaBudgetBytes),(unsigned long)p3MemoryReport[1],inputCheck_,(unsigned long)actions_,(unsigned long)nav_,
                (unsigned long)volumeEvents_,(unsigned long)playEvents_,(unsigned long)viewEvents_,textSeen_,textOk_,(unsigned long)noLyricsView_,(unsigned long)preferenceTransitions_);return;
        case 11:
            append("media_track_ref=track\nmedia_generation=%lu\nmedia_active=%u\n",
                (unsigned long)m.generation,ui.nowPlaying().model().active);
            append("track=%s\n",track_);return;
        case 12:
            append("restored_track=%s\nrestored_position_ms=%lu\nrestored_view=%u\nstartup_paused=%u\nstartup_silent=%u\nstartup_observed_ms=%lu\n",
                restoredTrack_,(unsigned long)restoredPosition_,restoredView_,startupPaused_,startupSilent_,(unsigned long)startupObservedMs_);return;
        case 13:append("resource_path=%s\n",resourcePath_[0]?resourcePath_:"NA");return;
        case 14:append("nav_target=%s\n",ui.navigationTarget());return;
        case 15:append("browser_path=%s\n",ui.browser().currentPath());return;
        case 16:case 17:case 18:case 19:{
            const uint32_t block=streamSection_-17;const uint32_t first=eventCount_>32?eventCount_-32:0;
            const uint32_t from=first+block*8,to=std::min<uint32_t>(eventCount_,from+8);
            for(uint32_t i=from;i<to;++i){const auto& e=events_[i%32];append("event_%lu=%lu,%s,%s,%d,%d,%u,captured_abs_ms:%lu\n",
                (unsigned long)i,(unsigned long)e.ms,uiPageName(e.page),uiActionName(e.action),e.x,e.y,e.accepted,(unsigned long)e.captured);}return;}
        default:
            streamFinalChunk_=true;append("END sequence=%lu crc=%08lx\n",(unsigned long)sequence_,(unsigned long)(streamCrc_^~0U));return;
    }
}
void FreeSession::service(UiCoordinator& ui,const PlayerRuntime& player,uint32_t uiBurstUs){
    uiBurstMaxUs_=std::max(uiBurstMaxUs_,uiBurstUs);
    const uint32_t now=millis();
    if(save_.active()&&save_.timedOut(now)){
        if(file_)file_.close();file_=fs::File();logPrepared_=false;streamFinalChunk_=false;writingManual_=false;requestSave_=save_.needsNext();
        const uint32_t ticket=save_.activeThrough();save_.timeout(now);lastSaveOk_=false;ui.notifySaveFinished(ticket,false,"超时");return;
    }
    if(save_.active()){
        if(player.persistedCheckpointRevision()<save_.requiredPlayerRevision()){save_.stage(SaveStatus::WaitingPlayer);return;}
        if(ui.settings().store.savedRevision()<save_.requiredDisplayRevision()){save_.stage(SaveStatus::WaitingDisplay);return;}
    }else if(!player.persistenceIdle())return;
    if(ui.nowPlaying().presentingLyrics() || ui.settingsBusy())return;
    const uint32_t start=micros();
    if(file_){
        if(written_<length_){const size_t n=std::min<size_t>(512,length_-written_);
            if(file_.write(reinterpret_cast<const uint8_t*>(buffer_+written_),n)!=n){logWriteOk_=false;fail("logging","write_log");written_=length_;streamFinalChunk_=true;}
            else{if(!streamFinalChunk_)streamCrc_=mediaCrc(streamCrc_,reinterpret_cast<const uint8_t*>(buffer_+written_),n);written_+=n;}}
        else if(!streamFinalChunk_){prepareNext(ui,player);if(overflow_){logWriteOk_=false;fail("logging","checkpoint_section");streamFinalChunk_=true;}return;}
        else if(!logFlushed_){file_.flush();logWriteOk_&=file_.getWriteError()==0;logFlushed_=true;}
        else{const bool logOk=logWriteOk_;file_.close();lastSaved_=millis();
            const bool persisted=player.stateStoreAvailable()&&player.lastPersistenceResult()==PersistenceResult::Ok;
            if(!logOk)fail("logging","flush_log");if(writingManual_&&!persisted)fail("persistence","manual_checkpoint_not_saved");
            lastSaveOk_=logOk&&persisted&&ui.displaySettingsSaved();
            if(writingManual_){const uint32_t ticket=save_.activeThrough();save_.finish(lastSaveOk_,millis());ui.notifySaveFinished(ticket,lastSaveOk_,lastSaveOk_?nullptr:"校验");}
            writingManual_=false;if(save_.needsNext()){save_.begin(millis());requestSave_=true;}}
    }else if(logPrepared_){
        file_=SD.open(kLog,"a");logPrepared_=false;logFlushed_=false;logWriteOk_=true;
        if(file_){if(!file_.setBufferSize(512)){logWriteOk_=false;written_=length_;fail("logging","log_buffer");}}
        else{const uint32_t ticket=save_.activeThrough();writingManual_=false;fail("logging","open_log");if(save_.active()){save_.finish(false,millis());ui.notifySaveFinished(ticket,false,"日志打开");}lastSaved_=millis();}
    }else if(requestSave_ || millis()-lastSaved_>=15000){
        writingManual_=save_.active();if(writingManual_)save_.stage(SaveStatus::WritingDiagnostics);
        beginSnapshot(ui,player,writingManual_||failure_[0]);prepareNext(ui,player);requestSave_=false;
        lastSaveOk_=false;
        if(overflow_){const uint32_t ticket=save_.activeThrough();writingManual_=false;fail("logging","checkpoint_section");if(save_.active()){save_.finish(false,millis());ui.notifySaveFinished(ticket,false,"日志过大");}lastSaved_=millis();return;}
        logPrepared_=true;
    }
    writeMaxUs_=std::max<uint32_t>(writeMaxUs_,micros()-start);
}
} }
