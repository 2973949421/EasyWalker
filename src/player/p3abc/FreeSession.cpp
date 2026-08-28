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
    // Reuse the existing buffer for a bounded tail read; never truncate history.
    auto previous=SD.open(kLog,"r");if(previous){const size_t n=std::min<size_t>(sizeof(buffer_)-1,previous.size());previous.seek(previous.size()-n);
        const int got=previous.read(reinterpret_cast<uint8_t*>(buffer_),n);buffer_[got>0?got:0]=0;previous.close();
        const char* p=buffer_;while((p=std::strstr(p,"boot_id="))){bootId_=std::max(bootId_,uint32_t(std::strtoul(p+8,nullptr,10)+1));p+=8;}}
    auto f=SD.open(kLog,"a");if(f){f.println();f.close();}else fail("logging","open_log");
}
void FreeSession::fail(const char* component,const char* reason){
    if(failure_[0])return;
    std::snprintf(component_,sizeof(component_),"%s",component);
    std::snprintf(failure_,sizeof(failure_),"%s",reason);
    requestSave_=true;
}
void FreeSession::action(UiAction action,const RawKeyEvent& raw,UiPage page,bool accepted){
    events_[eventCount_%12]={millis()-started_,action,page,raw.x,raw.y,accepted,raw.capturedAtMs};++eventCount_;++actions_;
    if(action==UiAction::SaveDiagnostics){requestSave_=manual_=true;return;}
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
void FreeSession::prepare(const UiCoordinator& ui,const PlayerRuntime& player){
    length_=written_=0;overflow_=false;++sequence_;
    const auto s=player.snapshot();const auto m=ui.nowPlaying().mediaStatus();const auto& r=ui.nowPlaying().stats();
    const auto u=ui.stats();const auto library=ui.browser().stats();
    const bool a=textSeen_&&textOk_&&u.playlistFrames&&u.libraryFrames&&u.differentTrackSelections;
    const bool b=longestPlaying_>=60000 && volumeEvents_ && playEvents_ && r.titleDraws && r.timeDraws;
    const bool c=lyricsReady_&&coverReady_&&lyricFrames_&&coverFrames_&&deadlineUpdates_&&viewEvents_;
    const auto& ds=ui.settings();const auto& power=ui.screenPower();const auto& visual=ui.libraryVisual();
    const bool d=visual.coverFrames&&visual.frames&&ds.changes&&ds.store.writes&&power.sleeps&&power.wakes;
    append("BEGIN sequence=%lu\n",(unsigned long)sequence_);
    append("render_contract=1\nrender_pixel_selfcheck=%s\nframe_starts=%lu\nframe_cancels=%lu\nframe_rejects=%lu\nframe_repairs=%lu\nframe_failure=%s\n",
        u.displaySelfFailure?u.displaySelfFailure:"PASS",(unsigned long)r.frameStarts,(unsigned long)r.frameCancels,(unsigned long)r.frameRejects,(unsigned long)r.frameRepairs,r.frameFailure);
    append("frame_id=%lu\nframe_generation=%lu\nframe_pending=%u\nframe_expected_rows=%u\nframe_submitted_rows=%u\nfull_frames=%lu\npatch_frames=%lu\nfallback_frames=%lu\n",
        (unsigned long)m.frameId,(unsigned long)m.generation,ui.nowPlaying().framePending(),ui.nowPlaying().expectedRows(),ui.nowPlaying().submittedRows(),
        (unsigned long)m.frames,(unsigned long)m.patchFrames,(unsigned long)m.fallbackFrames);
    append("frame_start_ms=%lu\nframe_first_ms=%lu\nframe_complete_ms=%lu\nstripe_submit_max_us=%lu\nstripe_draw_max_us=%lu\nstripe_wait_wall_max_us=%lu\nstripe_waits=%lu\n",
        (unsigned long)r.frameStartedAt,(unsigned long)r.frameFirstAt,(unsigned long)r.frameCompletedAt,(unsigned long)r.submitMaxUs,
        (unsigned long)r.drawMaxUs,(unsigned long)r.bandWaitWallMaxUs,(unsigned long)r.bandWaits);
    const char* scenes[]={"lyrics","cover","playlist","library","settings"};
    for(unsigned i=0;i<5;++i)append("sleep_%s=%u\nwake_%s=%u\n",scenes[i],ui.sleepsOn(i),scenes[i],ui.wakesOn(i));
    append("pcm_peak_track=%s\nlyric_peak_track=%s\nlyric_peak_generation=%lu\nlyric_peak_due_ms=%lu\nlyric_peak_prepared_ms=%lu\nlyric_peak_submitted_ms=%lu\nmedia_peak_work=%u\n",pcmPeakTrack_,lyricPeakTrack_,(unsigned long)lyricPeakGeneration_,(unsigned long)lyricPeakDue_,(unsigned long)lyricPeakPrepared_,(unsigned long)lyricPeakSubmitted_,m.peakWork);
    append("warm_returns=%lu\nwarm_return_max_ms=%lu\nselection_feedback_max_ms=%lu\n",(unsigned long)u.warmReturns,(unsigned long)u.warmReturnMaxMs,(unsigned long)u.selectionFeedbackMaxMs);
    append("input_accept_max_ms=%lu\ninput_queue_overflow=%lu\ninput_stale_events=%lu\nview_pending=%u\nview_coalesced=%lu\nview_warm_max_ms=%lu\nview_cold_max_ms=%lu\nview_failures=%lu\nlibrary_text_evidence=synthetic_layout_not_real_library\n",
        (unsigned long)ui.inputLatencyMaxMs(),(unsigned long)ui.inputOverflow(),(unsigned long)ui.inputStale(),m.viewPending,
        (unsigned long)m.viewCoalesced,(unsigned long)m.viewWarmMaxMs,(unsigned long)m.viewColdMaxMs,(unsigned long)m.viewFailures);
    const char* storagePhases[]={"idle","queue_path_prepare","open_target","write_header","write_payload","close_target","open_verify","read_verify","check_size","close_resize","create_target","flush_target","read_header","queue_path_fetch","publish","cleanup_close"};
    append("view_requested_at_ms=%lu\nview_ready_at_ms=%lu\nview_first_stripe_at_ms=%lu\nview_completed_at_ms=%lu\n",(unsigned long)m.viewRequestedMs,(unsigned long)m.viewReadyMs,(unsigned long)m.viewFirstStripeMs,(unsigned long)m.viewCompletedMs);
    append("view_warm_completed=%lu\nview_cold_completed=%lu\nview_failure=%s\naudio_source_read_max_us=%lu\nmedia_plus_events_bytes=%lu\n",(unsigned long)m.viewWarmCompleted,(unsigned long)m.viewColdCompleted,m.viewFailure,(unsigned long)player.sourceReadMaxUs(),(unsigned long)p3MemoryReport[1]);
    for(unsigned i=1;i<16;++i)append("store_%s_max_us=%lu\n",storagePhases[i],(unsigned long)player.persistencePhasePeakUs(i));
    append("tab_playing=%lu\ntab_paused=%lu\ntab_before_ms=%lu\ntab_after_ms=%lu\ntab_state=%u\n",(unsigned long)u.tabPlaying,(unsigned long)u.tabPaused,(unsigned long)u.tabBeforeMs,(unsigned long)u.tabAfterMs,u.tabState);
    append("tab_events=%lu\ntab_state_errors=%lu\nwindow_builds=%lu\nhighlight_updates=%lu\npage_first_frame_max_ms=%lu\ntransport_service_max_us=%lu\npersistence_service_max_us=%lu\n",
        (unsigned long)u.tabEvents,(unsigned long)u.tabStateErrors,(unsigned long)u.windowBuilds,(unsigned long)u.highlightUpdates,(unsigned long)u.firstFrameMaxMs,
        (unsigned long)player.transportServiceMaxUs(),(unsigned long)player.persistenceServiceMaxUs());
    append("lyric_due_ms=%lu\nlyric_prepared_ms=%lu\nlyric_submitted_ms=%lu\ncover_opens=%lu\nfont_opens=%lu\nfont_index_page_hits=%lu\nfont_first_draw_cp=%lu\nfont_first_draw_face=%u\npcm_peak_at_ms=%lu\npcm_peak_generation=%lu\npcm_peak_page=%s\n",
        (unsigned long)m.lyricDueMs,(unsigned long)m.lyricPreparedMs,(unsigned long)m.lyricSubmittedMs,(unsigned long)m.coverOpens,
        (unsigned long)ui.fonts().stats().opens,(unsigned long)ui.fonts().stats().indexPageHits,(unsigned long)ui.fonts().stats().firstDrawCodepoint,ui.fonts().stats().firstDrawFont,
        (unsigned long)pcmPeakAt_,(unsigned long)pcmPeakGeneration_,uiPageName(pcmPeakPage_));
    const char* components[]={"lyrics","cover","font","navigation"};
    for(unsigned i=0;i<4;++i){const auto& e=mediaErrors_[i];
        append("%s_failure=%s\n%s_failure_path=%s\n%s_failure_info=%lu,%lu,%lu,%s,%d,%ld,%ld\n",components[i],e.count?e.failure.reason:"none",
            components[i],e.count?e.path:"NA",components[i],(unsigned long)e.count,(unsigned long)e.generation,(unsigned long)e.at,
            e.failure.operation,e.failure.systemError,(long)e.failure.expected,(long)e.failure.actual);
    }
    append("boot_id=%lu\nmanual_checkpoint=%u\nstate_writes=%lu\nrestored_track=%s\nrestored_position_ms=%lu\nrestored_view=%u\nstartup_paused=%u\nstartup_silent=%u\n",(unsigned long)bootId_,manual_,(unsigned long)player.stateWriteCount(),restoredTrack_,(unsigned long)restoredPosition_,restoredView_,startupPaused_,startupSilent_);
    append("startup_observed_ms=%lu\nmuted_media_selfcheck=current_resource_or_no_track_cancel\n",(unsigned long)startupObservedMs_);
    append("effective_view=%u\nheader_visible=%u\nno_lyrics_view_noop=%lu\npreference_track_transitions=%lu\nui_latin_font=Times_New_Roman\nui_cjk_font=KaiTi\nlatin_lyric_px=14\nfont_coverage_bits=4\n",unsigned(m.view),ui.nowPlaying().model().headerVisible,(unsigned long)noLyricsView_,(unsigned long)preferenceTransitions_);
    append("audio_service_max_us=%lu\nlibrary_service_max_us=%lu\ninput_work_max_us=%lu\nfont_service_max_us=%lu\nmedia_service_max_us=%lu\n",(unsigned long)audioMax_,(unsigned long)libraryMax_,(unsigned long)inputMax_,(unsigned long)ui.fonts().stats().serviceMaxUs,(unsigned long)m.serviceMaxUs);
    append("schema=1\nversion=%s\nmode=free\nresult=%s\nhuman_review=PENDING\na_auto=%s\nb_auto=%s\nc_auto=%s\nd_auto=%s\n",ADV_WALKMAN_VERSION,failure_[0]?"FAIL":a&&b&&c&&d?"READY_FOR_REVIEW":"INCOMPLETE",a?"COVERED":"INCOMPLETE",b?"COVERED":"INCOMPLETE",c?"COVERED":"INCOMPLETE",d?"COVERED":"INCOMPLETE");
    append("brightness=%u\nplayer_timeout_ms=%lu\nother_timeout_ms=%lu\ndisplay_settings_loaded=%u\nrestored_brightness=%u\nrestored_player_timeout_ms=%lu\nrestored_other_timeout_ms=%lu\nsettings_changes=%lu\nsettings_writes=%lu\nsettings_errors=%lu\nsettings_saved=%u\nsettings_service_max_us=%lu\n",
        ds.store.value.brightness,(unsigned long)displayTimeoutMs(ds.store.value.playerTimeout),(unsigned long)displayTimeoutMs(ds.store.value.otherTimeout),ds.store.loaded,ds.store.restored.brightness,
        (unsigned long)displayTimeoutMs(ds.store.restored.playerTimeout),(unsigned long)displayTimeoutMs(ds.store.restored.otherTimeout),(unsigned long)ds.changes,(unsigned long)ds.store.writes,(unsigned long)ds.store.errors,ui.displaySettingsSaved(),(unsigned long)ds.store.serviceMaxUs);
    append("screen_asleep=%u\nwake_swallowing=%u\nscreen_sleeps=%lu\nscreen_wakes=%lu\nlibrary_cover_frames=%lu\nvinyl_frames=%lu\nlibrary_cover_state=%u\nlibrary_cover_errors=%lu\nlibrary_cover_service_max_us=%lu\nlauncher_requests=%lu\nlauncher_errors=%lu\nlauncher_error=%s\n",
        power.asleep(),power.swallowing(),(unsigned long)power.sleeps,(unsigned long)power.wakes,(unsigned long)visual.coverFrames,(unsigned long)visual.frames,unsigned(visual.cover.state()),(unsigned long)visual.cover.errors,(unsigned long)visual.cover.serviceMaxUs,(unsigned long)ds.returnRequests,(unsigned long)ds.returnErrors,ds.returnError());
    const auto& wake=ui.wakeStats();
    append("metadata_fallback_cause=%u\n",ui.metadataFallbackCause());
    append("previous_phase_valid=%u\nwake_resume_pcm=%lu\nwake_unfinished_count=%lu\nmetadata_fallbacks=%lu\n",
        runtimeDiagnostics.previousValid,(unsigned long)wake.pcm,(unsigned long)ui.unfinishedWakes(),(unsigned long)ui.metadataFallbacks());
    append("reset_reason=%lu\nprevious_phase=%lu\nprevious_phase_ms=%lu\nspeaker_distortion=DEFERRED\n",
        (unsigned long)runtimeDiagnostics.resetReason,(unsigned long)runtimeDiagnostics.previousPhase,(unsigned long)runtimeDiagnostics.previousMs);
    append("wake_captured_ms=%lu\nwake_backlight_ms=%lu\nwake_resume_ms=%lu\nwake_first_frame_ms=%lu\nwake_unlock_ms=%lu\nwake_resume_position_ms=%lu\nwake_complete=%s\n",
        (unsigned long)wake.captured,(unsigned long)wake.backlight,(unsigned long)wake.resume,(unsigned long)wake.firstFrame,
        (unsigned long)wake.unlocked,(unsigned long)wake.position,
        !power.wakes?"NA":wake.firstFrame&&wake.unlocked?"YES":"PENDING");
    append("heap_largest_block=%lu\n",(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    append("runtime_peaks_us=%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
        (unsigned long)runtimeDiagnostics.maxima[1],(unsigned long)runtimeDiagnostics.maxima[2],
        (unsigned long)runtimeDiagnostics.maxima[3],(unsigned long)runtimeDiagnostics.maxima[4],
        (unsigned long)runtimeDiagnostics.maxima[5],(unsigned long)runtimeDiagnostics.maxima[6],
        (unsigned long)runtimeDiagnostics.maxima[7]);
    append("wake_mask_high=%lu\nwake_mask_low=%lu\nwake_suppressed_actions=%lu\nsettings_invalid_slots=%lu\nsettings_open_failures=%lu\n",
        (unsigned long)(power.lastWakeMask>>32),(unsigned long)power.lastWakeMask,(unsigned long)ui.suppressedActions(),(unsigned long)ds.store.invalidSlots,(unsigned long)ds.store.openFailures);
    append("nav_state=%u\nnav_generation=%lu\nnav_target=%s\nnav_error=%s\nnav_errors=%lu\npage_frame_complete=%u\nplaylist_frames=%lu\nlibrary_frames=%lu\ntrack_selections=%lu\ndifferent_track_selections=%lu\nqueue_count=%u\nselected_queue_count=%lu\ncurrent_index=%u\ntime_font_px=14\n",
        unsigned(u.navigationState),(unsigned long)u.navigationGeneration,ui.navigationTarget(),ui.navigationError()[0]?ui.navigationError():"none",(unsigned long)u.navigationErrors,u.pageFirstFrameComplete,
        (unsigned long)u.playlistFrames,(unsigned long)u.libraryFrames,(unsigned long)u.trackSelections,(unsigned long)u.differentTrackSelections,unsigned(s.queueCount),(unsigned long)u.lastQueueCount,unsigned(s.currentIndex));
    append("browser_path=%s\nbrowser_state=%u\nbrowser_error=%u\nlast_browser_error=%lu\nscanned_entries=%lu\nscratch_allocation_failures=%lu\nlargest_block_at_nav_error=%lu\nbrowser_prepare_max_us=%lu\nbrowser_draw_max_us=%lu\nscan_max_us=%lu\nentry_read_max_us=%lu\n",
        ui.browser().currentPath(),unsigned(ui.browser().state()),unsigned(ui.browser().error()),(unsigned long)u.lastLibraryError,(unsigned long)library.scannedEntries,(unsigned long)library.scratchAllocationFailures,
        (unsigned long)u.largestFreeBlock,(unsigned long)u.prepareMaxUs,(unsigned long)u.renderMaxUs,(unsigned long)library.scanMaxUs,(unsigned long)library.entryReadMaxUs);
    // Independent evidence survives a prior self-check/navigation failure.
    append("audio_limits_ok=%u\nmedia_limits_ok=%u\n",!audioErrors_&&!backpressure_&&pcmGapMaxUs_<=70000,presentMaxUs_<=100000&&lateMaxMs_<=200);
    append("navigation_work_max_us=%lu\n",(unsigned long)u.navigationMaxUs);
    // track is already the media model's path; do not duplicate up to 511
    // bytes per checkpoint. Keep the explicit reference and generation.
    append("media_track_ref=track\nmedia_generation=%lu\nmedia_active=%u\nfont_io_errors=%lu\nfont_draw_misses=%lu\n",(unsigned long)m.generation,ui.nowPlaying().model().active,(unsigned long)ui.fonts().stats().ioErrors,(unsigned long)ui.fonts().stats().drawMisses);
    append("coverage_scope=free_main_path\nnot_exercised=playing_seek;manual_reboot_requires_host_comparison\ndisplay_selfchecks=%u\ndisplay_self_failure=%s\n",ui.stats().displaySelfChecks,ui.stats().displaySelfFailure?ui.stats().displaySelfFailure:"none");
    append("resource_expected=%ld\nresource_actual=%ld\nrepeat_raw=%u\nshuffle_raw=%u\n",(long)resourceExpected_,(long)resourceActual_,unsigned(s.repeatMode),s.shuffleEnabled);
    append("speaker_volume_raw=%u\nspeaker_volume_cap=%u\nheap_free=%lu\n",player.rawSpeakerVolume(),VolumePolicy::maximumRaw,(unsigned long)ESP.getFreeHeap());
    append("lyric_font_px=%u\nlyric_columns=%u\nadjacent_preview=0\n",unsigned(MediaLayout::cell),unsigned(MediaLayout::columns));
    append("failure_component=%s\nfailure_reason=%s\nresource_path=%s\nresource_operation=%s\nresource_errno=%d\n",component_[0]?component_:"none",failure_[0]?failure_:"none",resourcePath_[0]?resourcePath_:"NA",resourceOperation_[0]?resourceOperation_:"NA",resourceErrno_);
    append("elapsed_ms=%lu\ntrack=%s\npage=%s\nplayer_state=%s\nposition_ms=%lu\nsample_rate=%lu\nvolume=%u\npreferred_view=%u\n",(unsigned long)(millis()-started_),track_,uiPageName(ui.page()),playerStateName(s.state),(unsigned long)s.positionMs,(unsigned long)s.sampleRateHz,player.volume(),player.preferredNowPlayingView());
    append("input_selfcheck=%u\nactions=%lu\nnav_mask=%lu\nvolume_events=%lu\nplay_events=%lu\nview_events=%lu\nlibrary_text_seen=%u\nlibrary_text_ok=%u\nlibrary_width_px=%lu\n",inputCheck_,(unsigned long)actions_,(unsigned long)nav_,(unsigned long)volumeEvents_,(unsigned long)playEvents_,(unsigned long)viewEvents_,textSeen_,textOk_,(unsigned long)libraryWidth_);
    append("longest_playing_ms=%lu\naudio_errors=%lu\nbackpressure=%lu\npcm_gap_max_us=%lu\npcm_buffers=%lu\n",(unsigned long)longestPlaying_,(unsigned long)audioErrors_,(unsigned long)backpressure_,(unsigned long)pcmGapMaxUs_,(unsigned long)s.pcmBuffersSinceReset);
    append("lyrics_state=%u\ncover_state=%u\nlyrics_frames=%lu\ncover_frames=%lu\nlyric_deadline_updates=%lu\nprepare_max_us=%lu\npresent_max_us=%lu\nlyric_late_max_ms=%lu\nresource_bytes=%lu\n",unsigned(m.lyrics),unsigned(m.cover),(unsigned long)lyricFrames_,(unsigned long)coverFrames_,(unsigned long)deadlineUpdates_,(unsigned long)prepareMaxUs_,(unsigned long)presentMaxUs_,(unsigned long)lateMaxMs_,(unsigned long)m.reads);
    append("overlay_patches=%lu\nrender_max_us=%lu\nui_burst_max_us=%lu\nlog_write_max_us=%lu\nminimum_heap=%lu\nmedia_budget_bytes=%u\n",(unsigned long)r.overlayPatches,(unsigned long)r.renderMaxUs,(unsigned long)uiBurstMaxUs_,(unsigned long)writeMaxUs_,(unsigned long)minimumHeap_,unsigned(kP3DMediaBudgetBytes));
    const uint32_t first=eventCount_>12?eventCount_-12:0;
    for(uint32_t i=first;i<eventCount_;++i){const auto& e=events_[i%12];append("event_%lu=%lu,%s,%s,%d,%d,%u,captured_abs_ms:%lu\n",(unsigned long)i,(unsigned long)e.ms,uiPageName(e.page),uiActionName(e.action),e.x,e.y,e.accepted,(unsigned long)e.captured);}
    const uint32_t crc=mediaCrc(~0U,reinterpret_cast<const uint8_t*>(buffer_),length_)^~0U;
    append("END sequence=%lu crc=%08lx\n",(unsigned long)sequence_,(unsigned long)crc);
}
void FreeSession::service(UiCoordinator& ui,const PlayerRuntime& player,uint32_t uiBurstUs){
    uiBurstMaxUs_=std::max(uiBurstMaxUs_,uiBurstUs);
    if(ui.nowPlaying().presentingLyrics() || !player.persistenceIdle() || ui.settingsBusy())return;
    if(manual_&&!ui.settings().store.idle())return;
    const uint32_t start=micros();
    if(file_){
        if(written_<length_){const size_t n=std::min<size_t>(512,length_-written_);if(file_.write(reinterpret_cast<const uint8_t*>(buffer_+written_),n)!=n){logWriteOk_=false;fail("logging","write_log");written_=length_;}else written_+=n;}
        else if(!logFlushed_){file_.flush();logWriteOk_&=file_.getWriteError()==0;logFlushed_=true;}
        else{const bool logOk=logWriteOk_;file_.close();lastSaved_=millis();
            const bool persisted=player.stateStoreAvailable()&&player.lastPersistenceResult()==PersistenceResult::Ok;
            if(!logOk)fail("logging","flush_log");if(writingManual_&&!persisted)fail("persistence","manual_checkpoint_not_saved");
            lastSaveOk_=logOk&&persisted&&ui.displaySettingsSaved();
            if(writingManual_)ui.notifyLogSaved(lastSaveOk_);writingManual_=false;}
    }else if(logPrepared_){
        file_=SD.open(kLog,"a");logPrepared_=false;logFlushed_=false;logWriteOk_=true;
        if(file_){if(!file_.setBufferSize(512)){logWriteOk_=false;written_=length_;fail("logging","log_buffer");}}
        else{writingManual_=false;fail("logging","open_log");ui.notifyLogSaved(false);lastSaved_=millis();}
    }else if(requestSave_ || millis()-lastSaved_>=15000){
        prepare(ui,player);requestSave_=false;writingManual_=manual_;manual_=false;
        lastSaveOk_=false;
        if(overflow_){writingManual_=false;fail("logging","checkpoint_buffer");ui.notifyLogSaved(false);lastSaved_=millis();return;}
        logPrepared_=true;
    }
    writeMaxUs_=std::max<uint32_t>(writeMaxUs_,micros()-start);
}
} }
