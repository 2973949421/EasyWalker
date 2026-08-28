"""Target-compiler contracts + PC resources/log tests; NOT device validation."""
import subprocess
import unittest
import zlib
from pathlib import Path
from prepare_p3_media import LOCAL,PACKAGE,parse_lrc,pair_cues
from preview_p3_lyrics import Fonts,columns,layout,render
from validate_p3_free import VERSION,checkpoints,evaluate
ROOT=Path(__file__).resolve().parents[1]
def read(path):return (ROOT/path).read_text(encoding='utf-8-sig')
class FreeChecks(unittest.TestCase):
    def test_production_cpp_contracts_and_old_counterexample(self):
        compiler=r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'
        cmd=[compiler,'-std=gnu++14','-fsyntax-only','-Isrc','test/p3abc/free_contracts.cpp']
        good=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(good.returncode,0,good.stderr)
        old=subprocess.run(cmd+['-DREPRODUCE_OLD_COVER_PRIORITY'],cwd=ROOT,capture_output=True,text=True)
        self.assertNotEqual(old.returncode,0)
        self.assertIn('cold lyrics starvation regression',old.stderr)
    def test_current_cue_complete_and_intro_blank(self):
        fonts=Fonts();ja=parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes());zh=parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes())
        _,original,chinese=pair_cues(ja,zh)[0]
        glyphs,pages=layout(original,chinese,fonts)
        self.assertEqual(pages,1)
        self.assertEqual(len(glyphs),len(original)+len(chinese))
        self.assertGreater(len(set(g[1] for g in glyphs)),1)
        intro,_=render(original,chinese,fonts,intro=True)
        self.assertEqual(len(set(intro.crop((0,28,135,216)).get_flattened_data())),1)
        source=read('src/player/ui/media/LyricsRenderer.cpp')
        self.assertIn('if(stats_.intro)return true;',source)
        self.assertNotIn('timeline.text(0',source)
        self.assertNotIn('timeline.text(2',source)
        self.assertNotIn('y+16<=164',source)
    def test_all_real_cues_and_pages_no_lost_characters(self):
        fonts=Fonts();cues=pair_cues(parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes()),parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes()))
        self.assertEqual(len(cues),29)
        for _,ja,zh in cues:
            _,pages=layout(ja,zh,fonts)
            for page in range(pages):render(ja,zh,fonts,page)
        self.assertNotIn('——',(LOCAL/'crucifix-x.zh-Hans.lrc').read_text(encoding='utf-8'))
        self.assertEqual((LOCAL/'crucifix-x.user.ja.lrc').read_bytes(),(PACKAGE/'Lyrics/ADVWalkmanBenchmark/benchmark.lrc').read_bytes())
    def test_english_words_move_whole_and_overlong_words_progress(self):
        fonts=Fonts()
        for word in ('never','alive',"don't",'still-alive'):
            text='界'*10+word+' die'
            values=columns(text,fonts)
            self.assertEqual(''.join(c for column in values for c,_ in column),text)
            self.assertTrue(any(word in ''.join(c for c,_ in column) for column in values))
            render(text,'中文',fonts)
        text='i'*1024
        values=columns(text,fonts)
        self.assertGreater(len(values),1)
        self.assertEqual(sum(len(col) for col in values),1024)
    def test_no_opaque_volume_panel_or_full_screen_sprite(self):
        source=read('src/player/ui/NowPlayingPresenter.cpp')
        self.assertNotIn('fillRect(G::overlayX',source)
        self.assertIn('row_.setTextColor(kText);',source)
        self.assertIn('overlayPending_ && media_.canPatchOverlay()',source)
        self.assertIn('if(framePartial_)display.setClipRect',source)
        self.assertNotIn('createSprite',source)
    def test_observer_cannot_drive_player(self):
        source=read('src/player/p3abc/FreeSession.cpp')
        for call in ('player.pause(', 'player.resume(', 'player.seek', 'setPreferred', 'setActive(', 'ESP.restart(', 'handleAction('):self.assertNotIn(call,source)
        main=read('src/player/app/PlayerDevMain.cpp')
        self.assertNotIn('P3ABCGate gate',main)
        self.assertIn('FreeSession session',main)
        self.assertIn('session.observe(ui,player)',main)
    def test_real_controls_before_display(self):
        source=read('src/player/ui/UiCoordinator.cpp')
        volume=source.split('if(action==UiAction::VolumeUp')[1].split('if(action==UiAction::ToggleView)')[0]
        self.assertLess(volume.index('player_->setVolume'),volume.index('notifyVolumeAdjusted'))
        self.assertIn('player_->pause():player_->play()',source)
        restore=source.split('bool UiCoordinator::restorePlaylistForCurrentTrack()')[1]
        self.assertIn('openBrowser(lastPlaylistPath_',restore)
        opening=source.split('bool UiCoordinator::openBrowser(')[1].split('void UiCoordinator::navigationFailed')[0]
        self.assertLess(opening.index('setPage(page)'),opening.index('openRequested_ = true'))
        runtime=read('src/player/app/PlayerRuntime.cpp')
        self.assertLess(runtime.index('setVolume(VolumePolicy::initialLevel)'),runtime.index('engine_.begin()'))
        self.assertIn('engine_.setVolume(VolumePolicy::toRaw',read('src/player/app/PlayerRuntime.h'))
    def test_checkpoint_torn_tail_and_crc(self):
        body=b'BEGIN sequence=1\nversion='+VERSION.encode()+b'\nmode=free\n'
        good=body+f'END sequence=1 crc={zlib.crc32(body):08x}\n'.encode()
        self.assertEqual(len(checkpoints(good+b'BEGIN sequence=2\npartial')),1)
        with self.assertRaises(ValueError):checkpoints(good.replace(b'mode=free',b'mode=xxxx'))
        with self.assertRaises(ValueError):checkpoints(b'BEGIN sequence=1\npartial')
    def test_incomplete_is_not_pass_and_fail_cannot_hide(self):
        data=dict(version=VERSION,mode='free',failure_reason='none',audio_errors='0',backpressure='0',pcm_gap_max_us='42404',present_max_us='70000',lyric_late_max_ms='50',a_auto='COVERED',b_auto='COVERED',c_auto='INCOMPLETE',volume='80',speaker_volume_raw='32',speaker_volume_cap='102')
        self.assertEqual(evaluate(data)[0],'INCOMPLETE')
        data.update(speaker_volume_raw='128')
        self.assertIn('volume_policy',evaluate(data)[1])
        data.update(speaker_volume_raw='32')
        data.update(result='PASS',pcm_gap_max_us='70001')
        self.assertEqual(evaluate(data)[0],'FAIL')
        data.update(pcm_gap_max_us='40000',failure_reason='lyrics_loading_stalled')
        self.assertEqual(evaluate(data)[0],'FAIL')
if __name__=='__main__':unittest.main(verbosity=2)
