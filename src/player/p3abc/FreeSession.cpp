#include "FreeSession.h"
#include "player/ui/InputEdges.h"
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
    events_[eventCount_%12]={millis()-started_,action,page,raw.x,raw.y,accepted};++eventCount_;++actions_;
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
    if(u.displaySelfFailure)fail("display_selfcheck",u.displaySelfFailure);
    if(u.libraryTextIsBenchmark){textSeen_=true;libraryWidth_=u.libraryText.maxLineWidthPx;
        textOk_=u.libraryText.lineCount==2 && libraryWidth_<=u.libraryText.availableWidthPx &&
            !u.libraryText.truncated && !u.libraryText.invalidUtf8 && !u.libraryText.layoutError;
        if(!textOk_)fail("layout","library_text_layout");
    }
    if(ui.page()!=UiPage::Player){loading_=false;return;}
    lyricFrames_=std::max(lyricFrames_,m.lyricsFrames);coverFrames_=std::max(coverFrames_,m.coverFrames);
    deadlineUpdates_=std::max(deadlineUpdates_,m.deadlineUpdates);
    prepareMaxUs_=std::max(prepareMaxUs_,m.prepareMaxUs);presentMaxUs_=std::max(presentMaxUs_,m.presentMaxUs);lateMaxMs_=std::max(lateMaxMs_,m.lyricLateMaxMs);
    lyricsReady_|=m.lyrics==MediaState::Ready;coverReady_|=m.cover==MediaState::Ready;
    if(m.layoutError||m.invalidUtf8)fail("layout","lyric_layout");
    if(m.presentIoViolations)fail("media","io_during_lyric_present");
    if(m.presentMaxUs>100000)fail("media","lyric_present_over_100ms");
    if(m.lyricLateMaxMs>200 || m.missedDeadlines)fail("media","lyric_deadline_over_200ms");
    if(font.stats().missing||font.stats().ioErrors||font.stats().capacityErrors||font.stats().drawMisses){
        if(!failure_[0]){font.failurePath(resourcePath_,sizeof(resourcePath_));resourceErrno_=font.failure().systemError;resourceExpected_=font.failure().expected;resourceActual_=font.failure().actual;std::snprintf(resourceOperation_,sizeof(resourceOperation_),"%s",font.failure().operation);}
        fail("font",font.stats().drawMisses?"glyph_draw_missing":font.stats().capacityErrors?"glyph_cache_capacity":font.failure().reason);
    }
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
    const bool a=textSeen_&&textOk_&&nav_==127;
    const bool b=longestPlaying_>=60000 && volumeEvents_ && playEvents_ && r.titleDraws && r.timeDraws;
    const bool c=lyricsReady_&&coverReady_&&lyricFrames_&&coverFrames_&&deadlineUpdates_&&viewEvents_;
    append("BEGIN sequence=%lu\n",(unsigned long)sequence_);
    append("boot_id=%lu\nmanual_checkpoint=%u\nstate_writes=%lu\nrestored_track=%s\nrestored_position_ms=%lu\nrestored_view=%u\nstartup_paused=%u\nstartup_silent=%u\n",(unsigned long)bootId_,manual_,(unsigned long)player.stateWriteCount(),restoredTrack_,(unsigned long)restoredPosition_,restoredView_,startupPaused_,startupSilent_);
    append("startup_observed_ms=%lu\nmuted_media_selfcheck=position_pagination_cancel\n",(unsigned long)startupObservedMs_);
    append("effective_view=%u\nheader_visible=%u\nno_lyrics_view_noop=%lu\npreference_track_transitions=%lu\nui_latin_font=Times_New_Roman\nui_cjk_font=KaiTi\nlatin_lyric_px=14\nfont_coverage_bits=4\n",unsigned(m.view),ui.nowPlaying().model().headerVisible,(unsigned long)noLyricsView_,(unsigned long)preferenceTransitions_);
    append("audio_service_max_us=%lu\nlibrary_service_max_us=%lu\ninput_work_max_us=%lu\nfont_service_max_us=%lu\nmedia_service_max_us=%lu\n",(unsigned long)audioMax_,(unsigned long)libraryMax_,(unsigned long)inputMax_,(unsigned long)ui.fonts().stats().serviceMaxUs,(unsigned long)m.serviceMaxUs);
    append("schema=1\nversion=%s\nmode=free\nresult=%s\nhuman_review=PENDING\na_auto=%s\nb_auto=%s\nc_auto=%s\n",ADV_WALKMAN_VERSION,failure_[0]?"FAIL":a&&b&&c?"READY_FOR_REVIEW":"INCOMPLETE",a?"COVERED":"INCOMPLETE",b?"COVERED":"INCOMPLETE",c?"COVERED":"INCOMPLETE");
    append("coverage_scope=free_main_path\nnot_exercised=playing_seek;manual_reboot_requires_host_comparison\ndisplay_selfchecks=%u\ndisplay_self_failure=%s\n",ui.stats().displaySelfChecks,ui.stats().displaySelfFailure?ui.stats().displaySelfFailure:"none");
    append("resource_expected=%ld\nresource_actual=%ld\nrepeat_raw=%u\nshuffle_raw=%u\n",(long)resourceExpected_,(long)resourceActual_,unsigned(s.repeatMode),s.shuffleEnabled);
    append("speaker_volume_raw=%u\nspeaker_volume_cap=%u\nheap_free=%lu\n",player.rawSpeakerVolume(),VolumePolicy::maximumRaw,(unsigned long)ESP.getFreeHeap());
    append("lyric_font_px=%u\nlyric_columns=%u\nadjacent_preview=0\n",unsigned(MediaLayout::cell),unsigned(MediaLayout::columns));
    append("failure_component=%s\nfailure_reason=%s\nresource_path=%s\nresource_operation=%s\nresource_errno=%d\n",component_[0]?component_:"none",failure_[0]?failure_:"none",resourcePath_[0]?resourcePath_:"NA",resourceOperation_[0]?resourceOperation_:"NA",resourceErrno_);
    append("elapsed_ms=%lu\ntrack=%s\npage=%s\nplayer_state=%s\nposition_ms=%lu\nsample_rate=%lu\nvolume=%u\npreferred_view=%u\n",(unsigned long)(millis()-started_),track_,uiPageName(ui.page()),playerStateName(s.state),(unsigned long)s.positionMs,(unsigned long)s.sampleRateHz,player.volume(),player.preferredNowPlayingView());
    append("input_selfcheck=%u\nactions=%lu\nnav_mask=%lu\nvolume_events=%lu\nplay_events=%lu\nview_events=%lu\nlibrary_text_seen=%u\nlibrary_text_ok=%u\nlibrary_width_px=%lu\n",inputCheck_,(unsigned long)actions_,(unsigned long)nav_,(unsigned long)volumeEvents_,(unsigned long)playEvents_,(unsigned long)viewEvents_,textSeen_,textOk_,(unsigned long)libraryWidth_);
    append("longest_playing_ms=%lu\naudio_errors=%lu\nbackpressure=%lu\npcm_gap_max_us=%lu\npcm_buffers=%lu\n",(unsigned long)longestPlaying_,(unsigned long)audioErrors_,(unsigned long)backpressure_,(unsigned long)pcmGapMaxUs_,(unsigned long)s.pcmBuffersSinceReset);
    append("lyrics_state=%u\ncover_state=%u\nlyrics_frames=%lu\ncover_frames=%lu\nlyric_deadline_updates=%lu\nprepare_max_us=%lu\npresent_max_us=%lu\nlyric_late_max_ms=%lu\nresource_bytes=%lu\n",unsigned(m.lyrics),unsigned(m.cover),(unsigned long)lyricFrames_,(unsigned long)coverFrames_,(unsigned long)deadlineUpdates_,(unsigned long)prepareMaxUs_,(unsigned long)presentMaxUs_,(unsigned long)lateMaxMs_,(unsigned long)m.reads);
    append("overlay_patches=%lu\nrender_max_us=%lu\nui_burst_max_us=%lu\nlog_write_max_us=%lu\nminimum_heap=%lu\nmedia_budget_bytes=%u\n",(unsigned long)r.overlayPatches,(unsigned long)r.renderMaxUs,(unsigned long)uiBurstMaxUs_,(unsigned long)writeMaxUs_,(unsigned long)minimumHeap_,unsigned(kMediaBudgetBytes));
    const uint32_t first=eventCount_>12?eventCount_-12:0;
    for(uint32_t i=first;i<eventCount_;++i){const auto& e=events_[i%12];append("event_%lu=%lu,%s,%s,%d,%d,%u\n",(unsigned long)i,(unsigned long)e.ms,uiPageName(e.page),uiActionName(e.action),e.x,e.y,e.accepted);}
    const uint32_t crc=mediaCrc(~0U,reinterpret_cast<const uint8_t*>(buffer_),length_)^~0U;
    append("END sequence=%lu crc=%08lx\n",(unsigned long)sequence_,(unsigned long)crc);
}
void FreeSession::service(UiCoordinator& ui,const PlayerRuntime& player,uint32_t uiBurstUs){
    uiBurstMaxUs_=std::max(uiBurstMaxUs_,uiBurstUs);
    if(ui.nowPlaying().presentingLyrics() || !player.persistenceIdle())return;
    const uint32_t start=micros();
    if(file_){
        if(written_<length_){const size_t n=std::min<size_t>(512,length_-written_);if(file_.write(reinterpret_cast<const uint8_t*>(buffer_+written_),n)!=n){file_.close();fail("logging","write_log");ui.notifyLogSaved(false);length_=written_=0;lastSaved_=millis();return;}written_+=n;}
        else{file_.flush();const bool logOk=file_.getWriteError()==0;file_.close();lastSaved_=millis();
            const bool persisted=player.stateStoreAvailable()&&player.lastPersistenceResult()==PersistenceResult::Ok;
            if(!logOk)fail("logging","flush_log");if(writingManual_&&!persisted)fail("persistence","manual_checkpoint_not_saved");
            if(writingManual_)ui.notifyLogSaved(logOk&&persisted);writingManual_=false;}
    }else if(requestSave_ || millis()-lastSaved_>=15000){
        prepare(ui,player);requestSave_=false;writingManual_=manual_;manual_=false;
        if(overflow_){fail("logging","checkpoint_buffer");lastSaved_=millis();return;}
        file_=SD.open(kLog,"a");
        if(file_){if(!file_.setBufferSize(512)){file_.close();fail("logging","log_buffer");ui.notifyLogSaved(false);lastSaved_=millis();}}
        else{fail("logging","open_log");ui.notifyLogSaved(false);lastSaved_=millis();}
    }
    writeMaxUs_=std::max<uint32_t>(writeMaxUs_,micros()-start);
}
} }
