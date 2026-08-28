"""Production constexpr decisions + actual assets; not device timing evidence."""
import struct,subprocess,unittest,json
from pathlib import Path
from prepare_p3_media import PACKAGE,LOCAL,parse_lrc,pair_cues,validate_cover
from preview_p3_lyrics import Fonts,layout
from validate_p3_free import VERSION,current_boots,evaluate
ROOT=Path(__file__).resolve().parents[1]
def source(p):return (ROOT/p).read_text(encoding='utf-8-sig')
class OptimizationChecks(unittest.TestCase):
    def test_production_contracts_and_old_cover_failure(self):
        compiler=r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'
        production=subprocess.run([compiler,'-std=gnu++11','-fsyntax-only','-x','c++','src/player/ui/media/MediaLayout.h'],cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(production.returncode,0,production.stderr)
        cmd=[compiler,'-std=gnu++14','-fsyntax-only','-Isrc','test/p3abc/optimization_contracts.cpp']
        good=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(good.returncode,0,good.stderr)
        bad=subprocess.run(cmd+['-DREPRODUCE_COVER_CLOSE_BUG'],cwd=ROOT,capture_output=True,text=True)
        self.assertNotEqual(bad.returncode,0)
        self.assertIn('placeholder must not close validation file',bad.stderr)
    def test_actual_adjacent_frames_plus_ui_budget(self):
        fonts=Fonts();ui=set();records={}
        for name in ('cjk-12','cjk-14','cjk-18','latin-10','latin-12','latin-14'):
            raw=(PACKAGE/'ADVWalkman/fonts'/f'{name}.idx').read_bytes()
            records[name]={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',raw,i) for i in range(16,len(raw),24))}
        for px,text in ((14,'0123456789:/-?... 已保存 保存失败'),(10,'0123456789%1AS?')):
            ui|={(f'latin-{px}' if ord(c)<256 else f'cjk-{px}',ord(c)) for c in text}
        count=maximum=unique_max=0
        for path in (PACKAGE/'Lyrics').rglob('*.lrc'):
            if '.zh-' in path.name:continue
            translated=path.with_name(path.stem+'.zh-Hans.lrc');frames=[]
            for _,ja,zh in pair_cues(parse_lrc(path.read_bytes()),parse_lrc(translated.read_bytes())):
                count+=1;_,pages=layout(ja,zh,fonts)
                for page in range(pages):
                    glyphs,_=layout(ja,zh,fonts,page)
                    frames.append({('latin-14' if ord(g[0])<256 else 'cjk-18',ord(g[0])) for g in glyphs})
            for i,current in enumerate(frames):
                pins=current|(frames[i+1] if i+1<len(frames) else set())|ui
                size=sum((records[face][cp][2]*records[face][cp][3]+1)//2 for face,cp in pins)
                maximum=max(maximum,size);unique_max=max(unique_max,len(pins))
                self.assertLessEqual(size,15360);self.assertLessEqual(len(pins),200)
        self.assertEqual(count,298)
        print(f'ADJACENT_PLUS_UI: cues={count} bitmap_max={maximum} metrics_max={unique_max}',flush=True)
    def test_no_transport_in_tab_and_no_window_rebuild_on_highlight(self):
        ui=source('src/player/ui/UiCoordinator.cpp')
        tab=ui.split('if(action==UiAction::ToggleCurrentPlaybackPage)')[1].split('if (page_ != UiPage::Player')[0]
        for forbidden in ('selectTrack(', 'replaceQueue(', '->play(', '->stop(', 'seekToMs('):self.assertNotIn(forbidden,tab)
        move=ui.split('if(browserContextReady_ && old/kP3AVisibleRows==next/kP3AVisibleRows)')[1].split('}else invalidateBrowser();')[0]
        for forbidden in ('entryAt(', 'entryPathAt(', 'invalidateBrowser(', 'buildRenderContext('):self.assertNotIn(forbidden,move)
        self.assertIn('at(0,1)',source('src/player/ui/InputRouter.cpp'))
        self.assertIn('UiTextLayout::visitLines',ui)
        self.assertIn('250',ui)
    def test_all_full_width_covers_and_source_ownership(self):
        report=json.loads((LOCAL/'p3d-fix-covers.json').read_text())
        self.assertEqual(len(report),11)
        for item in report:
            data=(PACKAGE/item['path']).read_bytes();w,h=validate_cover(data)
            self.assertEqual(w,135);self.assertLessEqual(h,188)
            self.assertEqual(struct.unpack_from('<HH',data,12),(40,32))
        self.assertTrue(any('sophie' in r['path'] for r in report))
        self.assertTrue(any('blackbirthday' in r['path'] for r in report))
    def test_error_components_and_historical_version(self):
        with self.assertRaises(ValueError):current_boots([dict(version='0.8.0-p3d.ui',boot_id='3')])
        base=dict(version=VERSION,mode='free',failure_reason='font_draw_missing',volume='80',speaker_volume_raw='32',speaker_volume_cap='102',audio_errors='0',backpressure='0',pcm_gap_max_us='50000',present_max_us='70000',lyric_late_max_ms='50',cover_failure='cover_crc')
        status,reasons=evaluate(base)
        self.assertEqual(status,'FAIL');self.assertIn('cover_failure',reasons);self.assertIn('font_draw_missing',reasons)
if __name__=='__main__':unittest.main(verbosity=2)
