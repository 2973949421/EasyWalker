"""Production C++ contracts + actual font/log checks; no SD/device writes."""
import struct
import subprocess
import unittest
from pathlib import Path
from prepare_p3_media import PACKAGE
from validate_p3_free import VERSION,evaluate

ROOT=Path(__file__).resolve().parents[1]
def source(path): return (ROOT/path).read_text(encoding='utf-8-sig')

class NavigationChecks(unittest.TestCase):
    def test_production_cpp_decisions(self):
        compiler=r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'
        cmd=[compiler,'-std=gnu++14','-fsyntax-only','-Isrc','test/p3abc/navigation_contracts.cpp']
        good=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(good.returncode,0,good.stderr)
        bad=subprocess.run(cmd+['-DREPRODUCE_STALE_NAVIGATION'],cwd=ROOT,capture_output=True,text=True)
        self.assertNotEqual(bad.returncode,0)
        self.assertIn('old request must not complete',bad.stderr)
        # Firmware remains GNU++11; constexpr is enabled only for compile-time
        # contracts, with exactly the same state-machine body.
        runtime=subprocess.run([compiler,'-std=gnu++11','-fsyntax-only','-Isrc','-x','c++','-'],
            input='#include "player/ui/NavigationLoad.h"\nvoid test(){adv_walkman::player::NavigationLoad n; n.begin(0); n.cancel();}',
            cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(runtime.returncode,0,runtime.stderr)

    def test_page_release_precedes_io_and_visible_warmup(self):
        ui=source('src/player/ui/UiCoordinator.cpp')
        opening=ui.split('bool UiCoordinator::openBrowser(')[1].split('void UiCoordinator::navigationFailed')[0]
        self.assertLess(opening.index('setPage(page)'),opening.index('openRequested_ = true'))
        service=ui.split('void UiCoordinator::service()')[1].split('bool UiCoordinator::handleAction')[0]
        self.assertLess(service.index('if (pageClearRequested_)'),service.index('servicePendingNavigation();'))
        render=ui.split('void UiCoordinator::render()')[1].split('void UiCoordinator::prepareBrowser')[0]
        for forbidden in ('entryAt(', 'entryPathAt(', 'buildRenderContext(', 'cp<127'):
            self.assertNotIn(forbidden,render)
        presenter=source('src/player/ui/NowPlayingPresenter.cpp')
        self.assertIn('media_.suspend();',presenter)
        self.assertNotIn('media_.release();',presenter)
        self.assertIn('if (!model_.active) return false;',presenter)
        region=source('src/player/ui/PageRenderers.cpp').split('void PlaylistPageRenderer::renderRegion')[1].split('void SettingsPageRenderer')[0]
        self.assertNotIn('fillScreen(',region)
        self.assertIn('Enter 重试',region)
        log=source('src/player/p3abc/FreeSession.cpp')
        self.assertLess(log.index('if(font.stats().missing'),log.index('if(ui.page()!=UiPage::Player||'))

    def test_actual_footer_metrics(self):
        raw=(PACKAGE/'ADVWalkman/fonts/latin-14.idx').read_bytes()
        records={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',raw,i) for i in range(16,len(raw),24))}
        advances={cp:r[4] for cp,r in records.items()}
        for text in ('0:00/--:--','59:59/59:59','999:59/999:59','LOG SAVED','LOG ERROR'):
            self.assertLessEqual(sum(advances[ord(c)] for c in text),84,text)
            for c in text:
                g=records[ord(c)]
                self.assertGreaterEqual(2+g[6],0,c)
                self.assertLessEqual(2+g[6]+g[3],18,c)
        p=source('src/player/ui/NowPlayingPresenter.cpp')
        self.assertIn('CachedUiFont font(fonts_,14);if(fonts_)',p)
        self.assertIn('UiTextLayout::measure(display,"ADVWalkmanBenchmark"',p)
        self.assertIn('invalid.layoutError&&ink',p)

    def test_reload_and_diagnostics_are_not_silenced(self):
        font=source('src/player/ui/media/FontCache.cpp')
        self.assertIn('currentFont_==font&&index_&&fontFile_?(old->offset?4:3):1',font)
        self.assertIn('if(currentFont_!=g.font || !fontFile_){phase_=1;}',font)
        ui=source('src/player/ui/UiCoordinator.cpp')
        self.assertIn('navigation_.observe(browserRequest_',ui)
        for field in ('playlist_frames','different_track_selections','queue_count','nav_error','media_track','time_font_px'):
            self.assertIn(field,source('src/player/p3abc/FreeSession.cpp'))

    def test_current_folder_queue_not_implicit_global_queue(self):
        ui=source('src/player/ui/UiCoordinator.cpp')
        self.assertIn('libraryRuntime_->selectTrack(physical, true)',ui)
        for name,next_name in [('returnFromPlaylist','restorePlaylistForCurrentTrack'),('restorePlaylistForCurrentTrack','playlistCount')]:
            body=ui.split('UiCoordinator::'+name+'()')[1].split('UiCoordinator::'+next_name)[0]
            self.assertNotIn('replaceQueue(',body);self.assertNotIn('player_->stop(',body)
        for base in ('ankokutengoku','ether','twomoons'):
            self.assertTrue((PACKAGE/f'Music/AveMujica/{base}.mp3').is_file())
        self.assertEqual(len(list((PACKAGE/'Music/AveMujica').glob('*.mp3'))),11)

    def test_failure_and_coverage_evidence(self):
        r=dict(version=VERSION,mode='free',result='READY_FOR_REVIEW',failure_reason='none',volume='80',speaker_volume_raw='32',speaker_volume_cap='102',audio_errors='0',backpressure='0',pcm_gap_max_us='40000',present_max_us='90000',lyric_late_max_ms='90',a_auto='COVERED',b_auto='COVERED',c_auto='COVERED',longest_playing_ms='61000',lyrics_frames='2',cover_frames='2',lyric_deadline_updates='1',view_events='1',volume_events='1',play_events='1',library_text_ok='1',playlist_frames='1',library_frames='1',different_track_selections='1',time_font_px='14')
        r.update(tab_playing='2',tab_paused='2')
        r.update(input_accept_max_ms='25',selection_feedback_max_ms='50',warm_return_max_ms='100',view_warm_max_ms='100',view_cold_max_ms='1000',view_failures='0',input_queue_overflow='0',warm_returns='2',view_warm_completed='1',view_cold_completed='1')
        self.assertEqual(evaluate(r)[0],'READY_FOR_REVIEW')
        for key,value,expected in [('playlist_frames','0','INCOMPLETE'),('different_track_selections','0','INCOMPLETE'),('nav_errors','1','FAIL'),('display_self_failure','actual_font_layout','FAIL'),('pcm_gap_max_us','154195','FAIL'),('present_max_us','109328','FAIL'),('lyric_late_max_ms','687','FAIL')]:
            self.assertEqual(evaluate(dict(r,**{key:value}))[0],expected,key)

if __name__=='__main__':unittest.main(verbosity=2)
