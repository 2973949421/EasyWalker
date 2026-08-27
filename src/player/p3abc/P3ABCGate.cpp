#include "P3ABCGate.h"
#include <SD.h>
#include <M5Cardputer.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include "player/support/AdvStorage.h"
#include "player/ui/InputEdges.h"

namespace adv_walkman { namespace player {
namespace {
constexpr const char* kMarker="/ADVWalkman/state/p3abc-gate.txt";
bool markerEquals(const char* expected) {
    auto f=SD.open(kMarker,FILE_READ);if(!f || f.size()>96)return false;
    char data[97]{};const int n=f.read(reinterpret_cast<uint8_t*>(data),96);return n>0 && std::strcmp(data,expected)==0;
}
bool markerWrite(const char* value) {
    SD.mkdir("/ADVWalkman/state");auto f=SD.open(kMarker,"w");if(!f)return false;
    const bool ok=f.print(value)==std::strlen(value);f.flush();return ok && !f.getWriteError();
}
}
bool P3ABCGate::SingleSource::pathAt(size_t index,char* output,size_t capacity)const {
    if(index || std::strlen(path)>=capacity)return false;std::strcpy(output,path);return true;
}
void P3ABCGate::transition(Phase phase,UiCoordinator& ui,const char* hint){
    phase_=phase;phaseAt_=millis();ui.setHint("");ui.setGateCard(hint);
}
const char* P3ABCGate::phaseName(Phase phase){
    switch(phase){
        case Phase::FileQuota:return "file_quota";case Phase::Preflight:return "preflight";
        case Phase::NavigationCard:return "navigation_card";case Phase::A:return "navigation";
        case Phase::IntroCard:return "intro_card";case Phase::PrepareIntro:return "intro_seek";
        case Phase::Intro:return "intro_display";case Phase::PrepareLyrics:return "lyrics_seek";
        case Phase::Lyrics:return "lyrics_display";case Phase::WaitView:return "view_key";
        case Phase::Cover:return "cover_display";case Phase::PrepareMeasure:return "measure_start";
        case Phase::Warmup:return "warmup";case Phase::Measure:return "continuous_60s";
        case Phase::PauseNotice:return "pause_notice";case Phase::PauseCheck:return "pause_check";
        case Phase::AudioConfirm:return "audio_confirm";case Phase::SeekNotice:return "seek_notice";
        case Phase::SeekCheck:return "seek_check";case Phase::AuxNotice:return "aux_notice";
        case Phase::AuxWait:return "aux_fallback";case Phase::ReturnWait:return "return_track";
        case Phase::SaveNotice:return "save_notice";case Phase::Save:return "save";
        case Phase::RebootCheck:return "reboot_check";default:return "none";
    }
}
bool P3ABCGate::waitsForHuman() const {
    return (phase_==Phase::A && a_.waitingForHuman()) || phase_==Phase::NavigationCard || phase_==Phase::IntroCard || phase_==Phase::Intro || phase_==Phase::Lyrics || phase_==Phase::WaitView || phase_==Phase::Cover || phase_==Phase::AudioConfirm;
}
void P3ABCGate::writeSkipped(const char* path,const char* reason){
    auto f=SD.open(path,"w");if(f)f.printf("result=SKIPPED\nversion=%s\ntask_executed=0\nprimary_failure=%s\nmeasurement_started=0\nsample_rate=NA\nbackpressure=NA\npcm_gap_max_us=NA\n",ADV_WALKMAN_VERSION,reason);
}
void P3ABCGate::begin(UiCoordinator& ui,PlayerRuntime& player,M5GFX& display){
    char marker[96];std::snprintf(marker,sizeof(marker),"PASS %s",ADV_WALKMAN_VERSION);
    if(markerEquals(marker)){phase_=Phase::Disabled;return;}
    std::snprintf(marker,sizeof(marker),"RESUME %s",ADV_WALKMAN_VERSION);
    if(markerEquals(marker)){
        // Consume before any checks: a power interruption cannot create a
        // repeated reboot loop. A failed restore must be visible, not PASS.
        markerWrite("REBOOT_CHECK_STARTED");restoredRun_=true;
        pausedPosition_=player.snapshot().positionMs;bufferAt_=player.snapshot().pcmBuffersSinceReset;
        transition(Phase::RebootCheck,ui,"RESTORE CHECK\nSILENT 3 SEC");
        lastAt_=millis();return;
    }
    SD.mkdir("/ADVWalkman");SD.mkdir("/ADVWalkman/logs");
    writeSkipped("/ADVWalkman/logs/p3a-last.txt","waiting_preflight");
    writeSkipped("/ADVWalkman/logs/p3b-last.txt","waiting_preflight");
    writeSkipped("/ADVWalkman/logs/p3c-last.txt","waiting_preflight");
    player.pause(); // A valid restored song remains silent before Gate A.
    model_=checkP3BModel();drawing_=checkP3BPresenterDrawing(ui.presenterForValidation());
    overlay_=checkP3BOverlayRestoration(ui.presenterForValidation());modelRan_=true;
    if(!model_.passed() || !drawing_.passed() || !overlay_.passed()){fail("p3b_local_drawing",ui,player);return;}
    width_=display.width();height_=display.height();rotation_=display.getRotation();
    inputOk_=checkInputEdges();cRan_=true;
    if(!inputOk_){failureComponent_="input";fail("input_edges_selfcheck",ui,player);return;}
    ui.showLibrary();ui.presenterForValidation().releasePreload();
    ui.fonts().release();if(!ui.fonts().begin()){fail("font_memory",ui,player);return;}
    // No file-system work from the UI/library while intentionally exhausting
    // read-only descriptors. No on-disk test file or state is changed.
    transition(Phase::FileQuota,ui,"CHECK FILES\nPlease wait\nNo keys yet");
    ui.service();lastAt_=millis();
}
bool P3ABCGate::beforeAction(UiAction action,const RawKeyEvent& raw,UiCoordinator& ui,PlayerRuntime& player){
    if(phase_==Phase::Disabled)return false;
    if(phase_==Phase::A)return a_.beforeAction(action,raw,ui.page());
    if(phase_==Phase::Passed && action==UiAction::Confirm){ui.showLibrary();ui.setHint("");phase_=Phase::Disabled;return true;}
    if(phase_==Phase::Failed || phase_==Phase::Passed)return true;
    if(waitsForHuman() && action==UiAction::Back){fail("user_visual_rejected",ui,player);return true;}
    if(phase_==Phase::NavigationCard && action==UiAction::Confirm){
        a_.begin(width_,height_,rotation_,true,true);aStarted_=true;
        transition(Phase::A,ui,"");ui.setHint(a_.hint());
    }else if(phase_==Phase::IntroCard && action==UiAction::Confirm){
        transition(Phase::PrepareIntro,ui,"SCRIPT SEEK\nIntro at 0\nThen view.");
    }else if(phase_==Phase::Intro && action==UiAction::Confirm){
        if(ui.nowPlaying().mediaStatus().lyrics!=MediaState::Ready || ui.nowPlaying().mediaStatus().frames<=frameBaseline_)return true;
        displayOk_=true;transition(Phase::PrepareLyrics,ui,"SCRIPT SEEK\nTO LONG LYRIC");}
    else if(phase_==Phase::Lyrics && action==UiAction::Confirm){
        const auto media=ui.nowPlaying().mediaStatus();
        if(media.current!=13 || media.view!=MediaView::Lyrics || media.frames<=frameBaseline_)return true;
        lyricsOk_=true;viewBase_=media.viewChanges;transition(Phase::WaitView,ui,"PRESS VIEW\nRow 2 key 2\nThen cover\nENTER: OK\nESC: reject");}
    else if(phase_==Phase::WaitView && action==UiAction::ToggleView){
        viewRaw_=raw;
        viewKeyOk_=raw.x==12 && raw.y==1 && !raw.fn && raw.keyCount==1;
        frameBaseline_=ui.nowPlaying().mediaStatus().frames;
        ui.handleAction(action);transition(Phase::Cover,ui,"");
    }else if(phase_==Phase::Cover && action==UiAction::Confirm){
        const auto media=ui.nowPlaying().mediaStatus();
        if(media.cover!=MediaState::Ready || media.frames<=frameBaseline_)return true;
        if(!viewKeyOk_ || media.view!=MediaView::Cover || media.viewChanges!=viewBase_+1){fail("physical_view",ui,player);return true;}
        coverOk_=true;transition(Phase::PrepareMeasure,ui,"PLAY TEST\nAUTO 60 SEC");
    }else if(phase_==Phase::AudioConfirm && action==UiAction::Confirm){
        audioUserConfirmed_=true;transition(Phase::SeekNotice,ui,"SCRIPT SEEK\n60 SEC + RESUME");
    }
    return true;
}
void P3ABCGate::service(UiCoordinator& ui,PlayerRuntime& player){
    if(phase_==Phase::Disabled || phase_==Phase::Passed || phase_==Phase::Failed)return;
    const uint32_t now=millis();if(!waitsForHuman())autoMs_+=now-lastAt_;lastAt_=now;
    minimumHeap_=std::min(minimumHeap_,ESP.getFreeHeap());
    if(autoMs_>300000){fail("automatic_timeout_5min",ui,player);return;}
    const auto snapshot=player.snapshot();const auto media=ui.nowPlaying().mediaStatus();
    const bool realMediaConfirmation=phase_==Phase::Preflight || phase_==Phase::Intro || phase_==Phase::Lyrics || phase_==Phase::WaitView || phase_==Phase::Cover || phase_==Phase::Measure || phase_==Phase::ReturnWait;
    if(phase_!=Phase::FileQuota && !checkResources(ui,player,realMediaConfirmation))return;
    if(phase_==Phase::A){
        a_.service(ui,player);ui.setHint(a_.hint());
        if(a_.finished()){
            if(!a_.passed()){fail("p3a_regression",ui,player);return;}
            player.pause();player.setRepeatMode(RepeatMode::Off);player.setShuffleEnabled(false);
            player.setPreferredNowPlayingView(0);ui.showPlayer();
            transition(Phase::IntroCard,ui,"CHECK VIEWS\nInfo, then\nlong lyrics\nEach view:\nENTER: OK\nESC: reject\nENTER: GO");
        }return;
    }
    switch(phase_){
    case Phase::FileQuota:{
        if(!player.persistenceIdle())break;
        errno=0;FILE* file=std::fopen("/sd/Music/ADVWalkmanBenchmark/benchmark.mp3","rb");
        if(file){
            std::setvbuf(file,nullptr,_IONBF,0);quotaFiles_[quotaCount_++]=file;
            if(quotaCount_==13){closeQuotaFiles();failureComponent_="filesystem";fail("quota_limit_not_enforced",ui,player);}
            break;
        }
        quotaErrno_=errno;const unsigned opened=quotaCount_;closeQuotaFiles();
        errno=0;FILE* recovered=std::fopen("/sd/Music/ADVWalkmanBenchmark/benchmark.mp3","rb");
        quotaOk_=opened>0 && opened<=kAdvSdMaxFiles && recovered && (quotaErrno_==ENFILE || quotaErrno_==EMFILE);
        if(recovered)std::fclose(recovered);
        if(!quotaOk_){failureComponent_="filesystem";resourceFailure_.set("quota_recovery_failed","open",errno);std::snprintf(resourcePath_,sizeof(resourcePath_),"%s",benchmark_.path);fail("file_quota_selftest",ui,player);break;}
        ui.presenterForValidation().preloadTrack(benchmark_.path);
        transition(Phase::Preflight,ui,"CHECK FILES\nLyrics\nCover CRC\nCJK glyphs\nPlease wait");break;}
    case Phase::Preflight:{
        ui.presenterForValidation().serviceMedia();
        const auto status=ui.nowPlaying().mediaStatus();
        if(!checkResources(ui,player,true))break;
        if(status.lyrics!=MediaState::Ready || status.cover!=MediaState::Ready)break;
        const uint32_t glyphs[]={0x6218,0x8056,0x3042,0x306E,'A','?'};
        if(preflightGlyph_<6){
            const auto cp=glyphs[preflightGlyph_];
            if(ui.fonts().request(cp,cp<256?3:2))++preflightGlyph_;
            break;
        }
        if(ui.fonts().busy())break;
        preflightOk_=true;ui.presenterForValidation().releasePreload();
        transition(Phase::NavigationCard,ui,"SINGLE KEYS\nNo Fn.\nLIBRARY:\nLEFT/RIGHT\nENTER: open\nPLAYLIST:\nUP/DOWN\nENTER: play\nENTER: GO");break;}
    case Phase::PrepareIntro:
        if(now-phaseAt_<1000)break;
        if(!player.seekToMs(0)){fail("intro_seek",ui,player);break;}
        frameBaseline_=media.frames;transition(Phase::Intro,ui,"");break;
    case Phase::PrepareLyrics:
        if(now-phaseAt_<1000)break;
        if(!player.seekToMs(140000)){fail("lyric_seek",ui,player);break;}
        // Advance 2s in the script before pausing, to expose real transition
        // rendering of the mixed Latin/Japanese group at 2:20.04, without
        // waiting through the whole song. Never claim rotation was checked
        // against the first line, which contains no Latin characters.
        if(!player.resume()){fail("lyric_resume",ui,player);break;}
        frameBaseline_=media.frames;transition(Phase::SeekCheck,ui,"SCRIPT PLAY\nTHEN PAUSE");pulse_=0;break;
    case Phase::SeekCheck:
        if(pulse_==0){
            if(now-phaseAt_>=4500){player.pause();pausedPosition_=player.snapshot().positionMs;transition(Phase::Lyrics,ui,"");}
        }else if(snapshot.state==PlayerState::Playing && snapshot.sampleRateHz==44100 && now-phaseAt_>=3000){
            seekOk_=snapshot.positionMs>=60000 && snapshot.positionMs<70000;player.pause();transition(Phase::AuxNotice,ui,"RESOURCE TEST\nNO-LYRICS TRACK");
        }break;
    case Phase::PrepareMeasure:
        if(now-phaseAt_<1000 || !player.persistenceIdle())break;
        player.setPreferredNowPlayingView(0);
        if(!player.seekToMs(55000) || !player.resume()){fail("measure_start",ui,player);break;}
        bufferAt_=player.snapshot().pcmBuffersSinceReset;
        transition(Phase::Warmup,ui,"LOADING AUDIO\nKEEP CONNECTED");break;
    case Phase::Warmup:
        if(snapshot.state==PlayerState::Playing && snapshot.sampleRateHz==44100 && snapshot.pcmBuffersSinceReset-bufferAt_>=10){
            player.resetDiagnostics();b_.begin(benchmark_.path,now);bRan_=true;measurementStarted_=true;
            // Force cold *media* resources after diagnostics reset. The audio
            // decoder is NOT restarted; reads and cache misses are measured.
            ui.presenterForValidation().setActive(false,now);
            ui.fonts().release();
            if(!ui.fonts().begin()){fail("cold_font_memory",ui,player);break;}
            ui.presenterForValidation().setActive(true,now);
            ui.presenterForValidation().resetMediaDiagnostics();
            frameBaseline_=0;readBaseline_=ui.nowPlaying().mediaStatus().reads;heapAt_=ESP.getFreeHeap();windowAt_=now;pulse_=0;
            transition(Phase::Measure,ui,"");
        }break;
    case Phase::Measure:{
        measuredMs_=now-windowAt_;
        b_.sample(snapshot,ui.nowPlaying(),now);measured_=snapshot;media_=media;font_=ui.fonts().stats();
        measuredFrames_=media.frames-frameBaseline_;measuredReads_=media.reads>=readBaseline_?media.reads-readBaseline_:media.reads;
        if(b_.audioFailure()){fail("p3bc_audio_continuity",ui,player);break;}
        if(media.layoutError || media.invalidUtf8 || ui.fonts().stats().drawMisses){failureComponent_="renderer";fail("lyrics_layout_or_unready_glyph",ui,player);break;}
        if(media.presentIoViolations || media.presentMaxUs>100000 || media.lyricLateMaxMs>200 || media.missedDeadlines){failureComponent_="renderer";fail(media.presentIoViolations?"sd_read_during_lyrics_present":media.missedDeadlines?"lyrics_missed_deadline":media.presentMaxUs>100000?"lyrics_present_over_100ms":"lyrics_deadline_over_200ms",ui,player);break;}
        if(now-windowAt_>=5000U*(pulse_+1) && pulse_<10){
            if(pulse_%2==0)ui.handleAction(UiAction::ToggleView);
            else ui.notifyVolumeAdjusted(pulse_%3==0?0:(pulse_%3==1?128:255),now);
            ++pulse_;
        }
        if(now-windowAt_>=60000){
            if(measuredFrames_<6 || measuredReads_==0 || !media.deadlineUpdates || media.lyrics!=MediaState::Ready || media.cover!=MediaState::Ready){fail("media_not_exercised",ui,player);break;}
            if(ESP.getFreeHeap()+16384<heapAt_){fail("media_heap_loss",ui,player);break;}
            player.setPreferredNowPlayingView(0);
            transition(Phase::PauseNotice,ui,"SCRIPT PAUSE\n3 SEC CHECK");
        }break;}
    case Phase::PauseNotice:
        if(now-phaseAt_>=1200 && media.view==MediaView::Lyrics){player.pause();pausedPosition_=player.snapshot().positionMs;pausedPage_=media.page;transition(Phase::PauseCheck,ui,"PAUSED 3 SEC\nTITLE MAY MOVE");}break;
    case Phase::PauseCheck:
        if(now-phaseAt_>=3000){pauseOk_=snapshot.state==PlayerState::Paused && snapshot.positionMs==pausedPosition_ && media.page==pausedPage_;
            if(!pauseOk_){fail("pause_lyric_sync",ui,player);break;}transition(Phase::AudioConfirm,ui,"SOUND AND\nOVERLAY OK?\nENTER: YES\nESC: NO");}break;
    case Phase::SeekNotice:
        if(now-phaseAt_>=1200){if(!player.seekToMs(60000) || player.snapshot().state!=PlayerState::Paused || !player.resume()){fail("paused_seek_resume",ui,player);break;}
            pulse_=1;transition(Phase::SeekCheck,ui,"SEEK RECOVERY\n3 SEC PLAY");}break;
    case Phase::AuxNotice:
        if(now-phaseAt_>=1200 && player.persistenceIdle()){
            player.setPreferredNowPlayingView(0);if(!player.replaceQueue(auxiliary_,0,false)){fail("aux_queue",ui,player);break;}
            // Opening then immediately pausing is synchronous: no service call
            // can submit PCM before the explicit pause below.
            if(!player.play() || !player.pause()){fail("aux_open",ui,player);break;}
            transition(Phase::AuxWait,ui,"NO LYRICS\nCOVER FALLBACK");}break;
    case Phase::AuxWait:
        if(media.lyrics==MediaState::Missing && media.cover==MediaState::Missing && now-phaseAt_>=2000){
            const auto before=player.preferredNowPlayingView();ui.handleAction(UiAction::ToggleView);
            fallbackOk_=before==0 && player.preferredNowPlayingView()==0 && media.view==MediaView::Cover;
            if(!fallbackOk_){fail("fallback_preference",ui,player);break;}
            if(!player.persistenceIdle())break;
            player.setPreferredNowPlayingView(1);
            if(!player.replaceQueue(benchmark_,0,false) || !player.play() || !player.pause()){fail("restore_benchmark",ui,player);break;}
            transition(Phase::ReturnWait,ui,"RETURN TRACK\nKEEP COVER VIEW");}break;
    case Phase::ReturnWait:
        if(media.lyrics==MediaState::Ready && media.cover==MediaState::Ready && now-phaseAt_>1000){
            if(media.preferred!=MediaView::Cover || media.view!=MediaView::Cover){fail("cross_track_preference",ui,player);break;}
            transition(Phase::SaveNotice,ui,"SCRIPT REBOOT\nWILL STAY PAUSED");}break;
    case Phase::SaveNotice:
        if(now-phaseAt_>=2500){player.requestCheckpoint();transition(Phase::Save,ui,"SAVING VIEW\nREBOOT ONCE");}break;
    case Phase::Save:
        if(player.persistenceIdle()){
            if(!seekOk_ || !pauseOk_ || !fallbackOk_ || !displayOk_ || !lyricsOk_ || !coverOk_ || !audioUserConfirmed_){fail("incomplete_checks",ui,player);break;}
            if(!b_.writeLog(SD,model_,drawing_,overlay_,displayOk_,audioUserConfirmed_)){fail("p3b_log_write",ui,player);break;}
            if(!writeCLog("RUNNING",ui,player)){fail("p3c_log_write",ui,player);break;}
            char marker[96];std::snprintf(marker,sizeof(marker),"RESUME %s",ADV_WALKMAN_VERSION);
            if(!markerWrite(marker)){fail("reboot_marker",ui,player);break;}
            ESP.restart();
        }break;
    case Phase::RebootCheck:
        if(now-phaseAt_>=3000){
            char path[512];player.currentPath(path,sizeof(path));
            rebootOk_=snapshot.state==PlayerState::Paused && snapshot.positionMs==pausedPosition_ &&
                snapshot.pcmBuffersSinceReset==bufferAt_ && !M5.Speaker.isPlaying() &&
                player.preferredNowPlayingView()==1 && std::strcmp(path,benchmark_.path)==0;
            if(!rebootOk_){fail("restore_not_paused_or_view",ui,player);break;}
            // Append to the pre-reboot evidence; never replace it with zeroed
            // counters from the fresh boot. Host requires final_result.
            auto f=SD.open("/ADVWalkman/logs/p3c-last.txt","a");if(!f){fail("reboot_log",ui,player);break;}
            f.printf("reboot_checked=1\nrestore_paused=1\nrestore_view=cover\nrestore_position_ms=%lu\nfinal_result=PASS\n",(unsigned long)snapshot.positionMs);f.flush();
            if(f.getWriteError()){f.close();fail("reboot_log_flush",ui,player);break;}f.close();
            char marker[96];std::snprintf(marker,sizeof(marker),"PASS %s",ADV_WALKMAN_VERSION);
            if(!markerWrite(marker)){fail("pass_marker",ui,player);break;}
            phase_=Phase::Passed;resultDrawn_=false;
        }break;
    default:break;
    }
    if(!waitsForHuman() && now-phaseAt_>45000 && phase_!=Phase::Measure && phase_!=Phase::A)fail("phase_timeout",ui,player);
}
void P3ABCGate::closeQuotaFiles(){for(auto& f:quotaFiles_)if(f){std::fclose(f);f=nullptr;}}

bool P3ABCGate::checkResources(UiCoordinator& ui,PlayerRuntime& player,bool requireReal){
    const auto& presenter=ui.nowPlaying();const auto media=presenter.mediaStatus();const auto font=ui.fonts().stats();
    if(font.missing || font.ioErrors || font.capacityErrors){
        failureComponent_="font";resourceFailure_=ui.fonts().failure();ui.fonts().failurePath(resourcePath_,sizeof(resourcePath_));
    }else if(media.lyrics==MediaState::Error || (requireReal && media.lyrics==MediaState::Missing)){
        failureComponent_="lyrics";resourceFailure_=presenter.lyrics().failure();presenter.lyrics().failurePath(resourcePath_,sizeof(resourcePath_));
        if(std::strcmp(resourceFailure_.reason,"none")==0)resourceFailure_.set("no_usable_cues","parse");
    }else if(media.cover==MediaState::Error || (requireReal && media.cover==MediaState::Missing)){
        failureComponent_="cover";resourceFailure_=presenter.cover().failure();std::snprintf(resourcePath_,sizeof(resourcePath_),"%s",presenter.cover().path());
    }else return true;
    fail(resourceFailure_.reason,ui,player);return false;
}

void P3ABCGate::captureFailure(UiCoordinator& ui,PlayerRuntime& player){
    if(haveFailure_)return;
    haveFailure_=true;failurePhase_=phase_;failureAudio_=player.snapshot();failureMedia_=ui.nowPlaying().mediaStatus();failureFont_=ui.fonts().stats();
    if(!player.currentPath(failureTrack_,sizeof(failureTrack_)))std::snprintf(failureTrack_,sizeof(failureTrack_),"%s",ui.nowPlaying().model().path);
}
void P3ABCGate::fail(const char* reason,UiCoordinator& ui,PlayerRuntime& player){
    if(phase_==Phase::Failed)return;
    captureFailure(ui,player);reason_=reason;
    // Freeze first-failure evidence BEFORE pause and releasing descriptors.
    player.pause();closeQuotaFiles();ui.presenterForValidation().releasePreload();ui.fonts().release();
    phase_=Phase::Failed;resultDrawn_=false;
    if(!restoredRun_){
        if(!aStarted_)writeSkipped("/ADVWalkman/logs/p3a-last.txt",reason);
        if(bRan_) {
            b_.writeLog(SD,model_,drawing_,overlay_,displayOk_,audioUserConfirmed_);
            auto log=SD.open("/ADVWalkman/logs/p3b-last.txt","a");if(log)log.printf("primary_failure=%s\nprimary_failure_phase=%s\n",reason_,phaseName(failurePhase_));
        }else writeSkipped("/ADVWalkman/logs/p3b-last.txt",reason);
        if(cRan_)writeCLog(std::strcmp(reason,"p3a_regression")==0?"SKIPPED":"FAIL",ui,player);else writeSkipped("/ADVWalkman/logs/p3c-last.txt",reason);
    }else {auto f=SD.open("/ADVWalkman/logs/p3c-last.txt","a");if(f)f.printf("reboot_checked=1\nfinal_result=FAIL\nreboot_failure=%s\n",reason);}
}
bool P3ABCGate::writeCLog(const char* result,UiCoordinator& ui,PlayerRuntime& player){
    auto f=SD.open("/ADVWalkman/logs/p3c-last.txt","w");if(!f){reason_="c_log_write";return false;}
    f.printf("result=%s\nversion=%s\ntask_executed=%d\nprimary_failure=%s\nprimary_failure_phase=%s\nautomatic_ms=%lu\n",result,ADV_WALKMAN_VERSION,std::strcmp(result,"SKIPPED")!=0,reason_,phaseName(failurePhase_),(unsigned long)autoMs_);
    f.printf("resource_component=%s\nresource_operation=%s\nresource_path=%s\nresource_error=%s\nresource_errno=%d\nresource_expected=%ld\nresource_actual=%ld\n",failureComponent_,resourceFailure_.operation,resourcePath_[0]?resourcePath_:"NA",resourceFailure_.reason,resourceFailure_.systemError,(long)resourceFailure_.expected,(long)resourceFailure_.actual);
    f.printf("preflight_pass=%d\ninput_selfcheck=%d\nsd_max_files=%u\nfile_quota_checked=%d\nfile_quota_opened=%u\nfile_quota_errno=%d\n",preflightOk_,inputOk_,kAdvSdMaxFiles,quotaOk_,quotaCount_,quotaErrno_);
    f.printf("sd_additional_global_slot_bytes=%u\n",unsigned(additionalSdFileSlotsBytes()));
    f.printf("measurement_started=%d\nmeasurement_ms=%lu\n",measurementStarted_,(unsigned long)measuredMs_);
    f.printf("failure_captured=%d\n",haveFailure_);
    if(haveFailure_){
        f.printf("failure_track=%s\nfailure_player_state=%s\nfailure_sample_rate=%lu\nfailure_audio_error=%s\nfailure_backpressure=%lu\nfailure_pcm_gap_max_us=%lu\nfailure_pcm_buffers=%lu\n",failureTrack_[0]?failureTrack_:"none",playerStateName(failureAudio_.state),(unsigned long)failureAudio_.sampleRateHz,audioErrorName(failureAudio_.audioError),(unsigned long)failureAudio_.backpressureEvents,(unsigned long)failureAudio_.pcmSubmitGapMaxUs,(unsigned long)failureAudio_.pcmBuffersSinceReset);
        f.printf("failure_resource_generation=%lu\nfailure_lyrics_state=%s\nfailure_cover_state=%s\nfailure_font_io_errors=%lu\nfailure_font_missing=%lu\nfailure_font_capacity_errors=%lu\n",(unsigned long)failureMedia_.generation,mediaStateName(failureMedia_.lyrics),mediaStateName(failureMedia_.cover),(unsigned long)failureFont_.ioErrors,(unsigned long)failureFont_.missing,(unsigned long)failureFont_.capacityErrors);
    }
    f.printf("display_confirmed=%d\nlyrics_confirmed=%d\ncover_confirmed=%d\nview_key_confirmed=%d\npause_checked=%d\nseek_checked=%d\nfallback_checked=%d\n",displayOk_,lyricsOk_,coverOk_,viewKeyOk_,pauseOk_,seekOk_,fallbackOk_);
    f.printf("audio_user_confirmed=%d\n",audioUserConfirmed_);
    f.printf("view_raw_x=%d\nview_raw_y=%d\nview_raw_fn=%d\nview_raw_count=%u\n",viewRaw_.x,viewRaw_.y,viewRaw_.fn,unsigned(viewRaw_.keyCount));
    if(measurementStarted_) {
    f.printf("measurement_frames=%lu\nmeasurement_resource_bytes=%lu\nmedia_service_max_us=%lu\nview_changes=%lu\n",(unsigned long)measuredFrames_,(unsigned long)measuredReads_,(unsigned long)media_.serviceMaxUs,(unsigned long)media_.viewChanges);
    f.printf("font_reads=%lu\nfont_bytes=%lu\nfont_missing=%lu\nfont_io_errors=%lu\nfont_draw_misses=%lu\n",(unsigned long)font_.reads,(unsigned long)font_.bytes,(unsigned long)font_.missing,(unsigned long)font_.ioErrors,(unsigned long)font_.drawMisses);
    f.printf("sample_rate=%lu\naudio_error=%s\naudio_error_events=%lu\nbackpressure=%lu\npcm_gap_max_us=%lu\npcm_buffers=%lu\n",(unsigned long)measured_.sampleRateHz,audioErrorName(measured_.audioError),(unsigned long)measured_.audioErrorEvents,(unsigned long)measured_.backpressureEvents,(unsigned long)measured_.pcmSubmitGapMaxUs,(unsigned long)measured_.pcmBuffersSinceReset);
    f.printf("font_capacity_errors=%lu\nframe_prepare_max_us=%lu\nframe_present_max_us=%lu\nlyric_late_max_ms=%lu\nframe_present_sd_reads=%lu\nframe_cancellations=%lu\nresource_generation=%lu\n",(unsigned long)font_.capacityErrors,(unsigned long)media_.prepareMaxUs,(unsigned long)media_.presentMaxUs,(unsigned long)media_.lyricLateMaxMs,(unsigned long)media_.presentIoViolations,(unsigned long)media_.cancellations,(unsigned long)media_.generation);
    f.printf("frame_id=%lu\nlyric_deadline_updates=%lu\nlyric_missed_deadlines=%lu\nlyrics_layout_error=%d\nlyrics_invalid_utf8=%d\n",(unsigned long)media_.frameId,(unsigned long)media_.deadlineUpdates,(unsigned long)media_.missedDeadlines,media_.layoutError,media_.invalidUtf8);
    }else f.print("measurement_frames=NA\nmeasurement_resource_bytes=NA\nmedia_service_max_us=NA\nview_changes=NA\nfont_reads=NA\nfont_bytes=NA\nfont_missing=NA\nfont_io_errors=NA\nfont_draw_misses=NA\nfont_capacity_errors=NA\nsample_rate=NA\naudio_error=NA\naudio_error_events=NA\nbackpressure=NA\npcm_gap_max_us=NA\npcm_buffers=NA\nframe_prepare_max_us=NA\nframe_present_max_us=NA\nlyric_late_max_ms=NA\nframe_present_sd_reads=NA\nframe_cancellations=NA\n");
    if(!measurementStarted_)f.print("lyric_deadline_updates=NA\nlyric_missed_deadlines=NA\nlyrics_layout_error=NA\nlyrics_invalid_utf8=NA\n");
    f.printf("minimum_heap=%lu\nmedia_budget_bytes=%u\n",(unsigned long)minimumHeap_,unsigned(kMediaBudgetBytes));
    f.flush();
    return f.getWriteError()==0;
}
bool P3ABCGate::renderResult(M5GFX& display){
    if(phase_!=Phase::Passed && phase_!=Phase::Failed)return false;
    if(!resultDrawn_){
        display.setFont(&fonts::Font0);display.fillScreen(0x0861);display.setTextSize(2);display.setTextColor(phase_==Phase::Passed?TFT_GREEN:TFT_ORANGE,0x0861);
        display.drawString(phase_==Phase::Passed?"ABC PASS":"ABC FAIL",8,36);display.setTextSize(1.5f);display.setTextColor(TFT_WHITE,0x0861);
        char detail[192];const char* file=std::strrchr(resourcePath_,'/');file=file?file+1:resourcePath_;
        std::snprintf(detail,sizeof(detail),"%s / %s\n%s\n%s\nerrno %d\nSD -> PC",failureComponent_,phaseName(failurePhase_),reason_,file,resourceFailure_.systemError);
        UiTextLayout::draw(display,phase_==Phase::Passed?"ENTER: LIBRARY\nPaused. No auto play.":detail,{6,76,123,156,9,3,true});resultDrawn_=true;
    }return true;
}
} }
