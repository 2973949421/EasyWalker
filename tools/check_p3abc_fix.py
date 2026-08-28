"""Host reference/source-contract checks, NOT ADV rendering or audio tests.

The actual C++ input selfcheck, file-quota exercise and frame timing assertions
are also compiled into the Gate and must run on the device.
"""
import struct
import re
import unittest
from pathlib import Path
from prepare_p3_media import LOCAL, PACKAGE, parse_lrc, pair_cues
from preview_p3_lyrics import Fonts, layout
from validate_p3abc_gate import fields, diagnose

ROOT = Path(__file__).resolve().parents[1]


def source(path):
    return (ROOT / path).read_text(encoding='utf-8-sig')


class FixChecks(unittest.TestCase):
    def test_input_is_bitmap_not_count_and_no_fn_hint(self):
        router = source('src/player/ui/InputRouter.cpp')
        edges = source('src/player/ui/InputEdges.h')
        self.assertNotIn('isChange()', router)
        self.assertIn('keyList()', router)
        self.assertIn('>=25', edges)
        self.assertIn('mask!=raw_', edges)
        self.assertIn('mask&(mask-1)', edges)
        for file in ('src/player/ui/PageRenderers.cpp', 'src/player/p3a/P3AGate.cpp',
                     'src/player/p3abc/P3ABCGate.cpp', 'src/player/ui/NowPlayingPresenter.cpp'):
            self.assertNotIn('FN+', source(file))
            self.assertNotIn('FN +', source(file))
        self.assertIn('checkInputEdges()', source('src/player/p3abc/P3ABCGate.cpp'))

    def test_real_frame_unique_bitmaps_fit_arena(self):
        fonts = Fonts()
        cues = pair_cues(parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes()),
                         parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes()))
        for _, original, chinese in cues:
            _, pages = layout(original, chinese, fonts)
            for page in range(pages):
                glyphs, _ = layout(original, chinese, fonts, page)
                unique = {g[0] for g in glyphs}
                total = 0
                for char in unique:
                    name = 'latin-14' if ord(char)<256 else 'cjk-18'
                    record = fonts.records[name][ord(char)]
                    total += (record[2]*record[3]+1)//2
                self.assertLessEqual(total, 15*1024)
                self.assertLessEqual(len(unique), 200)

    def test_compact_metrics_cover_actual_assets_and_preflight(self):
        for name in ('cjk-12','cjk-14','cjk-16','cjk-18','latin-10','latin-12','latin-14'):
            data = (PACKAGE/f'ADVWalkman/fonts/{name}.idx').read_bytes()
            records = [struct.unpack_from('<IIHHhhhHI',data,i) for i in range(16,len(data),24)]
            for cp,offset,w,h,advance,dx,dy,px,reserved in records:
                self.assertLessEqual(cp,65535)
                self.assertLessEqual(max(w,h),18)
                self.assertTrue(-128<=dx<=127 and -128<=dy<=127)
            if name=='cjk-16':
                self.assertTrue({0x6218,0x8056,0x3042,0x306e}<={r[0] for r in records})
        cache = source('src/player/ui/media/FontCache.cpp')
        self.assertIn('if(!length){glyph.arena=0;return true;}', cache)
        self.assertIn('g.width && g.height', cache)  # spaces cannot alias a moved bitmap

    def test_prepare_present_barrier_and_cancel_contract(self):
        media = source('src/player/ui/media/NowPlayingMedia.cpp')
        worker = media.split('void NowPlayingMedia::service()')[1].split('bool NowPlayingMedia::wantsFrame')[0]
        self.assertIn('presentingLyrics())return', worker)
        stripe = media.split('bool NowPlayingMedia::prepareStripe')[1].split('void NowPlayingMedia::drawStripe')[0]
        self.assertIn('MediaView::Lyrics)return true;', stripe)
        self.assertIn('displayedRenderer_=renderer_;fonts_->promotePins()',media)
        self.assertIn('frameFromPrepared_=glyphsReady_',media)
        self.assertNotIn('fonts_->request', stripe)
        self.assertIn('layoutReady_&&positionMs_>=preparedUntil_', media)
        self.assertIn('fonts_->setPresenting(true)', media)
        self.assertNotIn('positionMs_+2000', media)
        self.assertIn('++missedDeadlines_', media)
        renderer = source('src/player/ui/media/LyricsRenderer.cpp')
        self.assertNotIn('"前奏"', renderer)
        self.assertIn('fonts.request(next,face,pin)', renderer)

    def test_cards_do_not_overlay_real_media(self):
        ui = source('src/player/ui/UiCoordinator.cpp')
        card = ui.split('if(gateCard_[0])')[1].split('if (page_ == UiPage::Player)')[0]
        self.assertIn('return;', card)
        gate = source('src/player/p3abc/P3ABCGate.cpp')
        for phase in ('Intro','Lyrics','Cover','Measure'):
            self.assertIn(f'transition(Phase::{phase},ui,"")', gate)
        # Font0 at 1.75x has 10.5px cells: 11 ASCII cells fit 123px.
        # Human-action cards must not ellipsize the final confirmation key.
        for phase in ('NavigationCard','IntroCard','WaitView','AudioConfirm'):
            card = re.search(r'transition\(Phase::'+phase+r',ui,"([^"]+)"\)', gate).group(1)
            lines = card.split('\\n')
            self.assertLessEqual(len(lines),10)
            self.assertTrue(all(len(line)<=11 for line in lines),phase)

    def test_io_error_classes_and_capture_before_pause(self):
        io = source('src/player/ui/media/ResourceIo.h')
        self.assertIn('statError==ENOENT', io)
        self.assertIn('"open_failed"', io)
        self.assertIn('"buffer_failed"', io)
        gate = source('src/player/p3abc/P3ABCGate.cpp')
        failure = gate.split('void P3ABCGate::fail(')[1].split('bool P3ABCGate::writeCLog')[0]
        self.assertLess(failure.index('captureFailure(ui,player)'),failure.index('player.pause()'))
        self.assertIn('quotaErrno_==ENFILE', gate)
        self.assertNotIn('real_track_media_missing_or_bad', gate)

    def test_failure_log_keeps_na_and_path_spaces(self):
        log=fields('result=FAIL\nresource_path=/Lyrics/Test Name/track.lrc\n'
                   'resource_component=lyrics\nresource_operation=open\n'
                   'primary_failure_phase=preflight\nprimary_failure=open_failed\n'
                   'resource_errno=23\nresource_expected=-1\nresource_actual=-1\n'
                   'pcm_gap_max_us=NA\nfailure_pcm_gap_max_us=42000\n')
        self.assertEqual(log['resource_path'],'/Lyrics/Test Name/track.lrc')
        report=diagnose([{'result':'SKIPPED'},{'result':'SKIPPED'},log])
        self.assertIn('measured_pcm_gap=NA',report)
        self.assertIn('failure_pcm_gap=42000',report)
        self.assertIn('phase=preflight',report)

    def test_thresholds_and_memory_are_explicit(self):
        gate=source('src/player/p3abc/P3ABCGate.cpp')
        for value in ('presentMaxUs>100000','lyricLateMaxMs>200','now-windowAt_>=60000','media.missedDeadlines'):
            self.assertIn(value,gate)
        self.assertIn('pcmSubmitGapMaxUs > 70000',source('src/player/ui/P3BChecks.cpp'))
        self.assertIn('kMediaBudgetBytes<=48*1024',source('src/player/ui/media/NowPlayingMedia.h'))
        self.assertIn('kAdvSdMaxFiles = 12',source('src/player/support/AdvStorage.h'))
        self.assertEqual(7*(4136+1),28959)  # global mount-table delta, NOT glyph memory


if __name__=='__main__':
    unittest.main(verbosity=2)
