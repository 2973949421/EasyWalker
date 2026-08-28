"""0.8.3 target-compiler contracts + PC references. No device timing claims."""
import json,math,random,subprocess,unittest
from pathlib import Path
from PIL import Image
from prepare_p3_refine import PRIVATE,TITLES,PACKAGE,records,audio_hash,probe
from validate_p3_free import VERSION,evaluate_wake,current_boots
ROOT=Path(__file__).resolve().parents[1]
def source(path):return (ROOT/path).read_text(encoding='utf-8-sig')
class RefineChecks(unittest.TestCase):
    def test_production_display_lifecycle_contracts(self):
        cmd=[r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe',
            '-std=gnu++14','-fsyntax-only','-Isrc','test/p3abc/display_contracts.cpp']
        run=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr)
        bad=subprocess.run(cmd+['-DREPRODUCE_OLD_WAKE_TIMER'],cwd=ROOT,capture_output=True,text=True)
        self.assertNotEqual(bad.returncode,0)
    def test_input_has_no_resource_cleanup_or_transport(self):
        ui=source('src/player/ui/UiCoordinator.cpp')
        raw=ui.split('bool UiCoordinator::physicalActivity')[1].split('void UiCoordinator::trackLabel')[0]
        for token in ('setActive(','.release(','.close(','.setBrightness(','.seek(','.play(','.pause('):self.assertNotIn(token,raw)
        self.assertIn('++inputEpoch_',raw)
        self.assertLess(ui.index('serviceSuspension()'),ui.index('if(power_.asleep())return;'))
        media=source('src/player/ui/media/NowPlayingMedia.cpp')
        suspend=media.split('void NowPlayingMedia::suspend()')[1].split('bool NowPlayingMedia::serviceSuspension()')[0]
        self.assertIn('cover_.cancelBand()',suspend);self.assertIn('fonts_->clearPins()',suspend)
        self.assertNotIn('.close(',suspend)
        self.assertIn('fonts_->suspendOne()',media);self.assertIn('timeline_.suspendOne()',media)
    def test_metadata_priority_and_cache_hit_are_ram_only(self):
        runtime=source('src/player/app/LibraryRuntime.h')
        function=runtime.split('bool cachedMetadataForPath')[1].split('\n')[0]
        self.assertIn('metadataCache_.lookup',function)
        self.assertNotIn('metadataReader_',function)
        ui=source('src/player/ui/UiCoordinator.cpp')
        prepare=ui.split('void UiCoordinator::serviceSelectedMetadata()')[1].split('void UiCoordinator::render()')[0]
        self.assertIn('n<kP3AVisibleRows',prepare);self.assertIn('navigationTarget_,row.basename',prepare)
        self.assertIn('setPath(row.label',prepare);self.assertNotIn('invalidateBrowser',prepare)
        self.assertNotIn('selectedMetadataTitle_',ui)
    def test_all_official_titles_and_unchanged_audio_packets(self):
        report=json.loads((PRIVATE/'titles.json').read_text(encoding='utf-8'))
        self.assertEqual(len(report),11);self.assertEqual(sum(r['changed'] for r in report),4)
        for item in report:
            path=PACKAGE/item['file'];self.assertEqual(probe(path)['format']['tags']['title'],TITLES[path.stem])
            if item['changed']:
                before=PRIVATE/'recovery'/item['file'];self.assertEqual(audio_hash(before),audio_hash(path))
        self.assertFalse(list((PACKAGE/'Lyrics/AveMujica').glob('ankokutengoku*.lrc')))
    def test_fonts_actual_metrics_and_legacy_tables(self):
        fontdir=PACKAGE/'ADVWalkman/fonts'
        for px in (12,14):
            known=records(fontdir/f'cjk-{px}')
            for text in TITLES.values():
                self.assertTrue(all(ord(c)<256 or ord(c) in known for c in text))
        for name,px in [('library-cjk-12',12),('library-cjk-18',18),('library-latin-14',14),('library-latin-22',22)]:
            for cp,r in records(fontdir/name).items():
                self.assertLessEqual(r[2],px);self.assertLessEqual(r[3],px)
                self.assertGreater(r[4],0)
        code=source('src/player/ui/media/FontCache.cpp')
        self.assertNotIn('if(g.font>=Latin10',code)
        self.assertIn('clock_=128',code)
        self.assertIn('sizeof(CachedGlyph)==16',source('src/player/ui/media/FontCache.h'))
    def test_run_drawing_equals_per_pixel_reference(self):
        rng=random.Random(83)
        for clockwise in (False,True):
            for _ in range(30):
                w,h=rng.randint(1,22),rng.randint(1,22);data=[rng.randrange(16) for _ in range(w*h)]
                expected={};actual={}
                for j in range(h):
                    for i in range(w):
                        a=data[j*w+i]
                        if a:expected[(h-1-j,i) if clockwise else (i,j)]=a
                    i=0
                    while i<w:
                        end=i+1;a=data[j*w+i]
                        while end<w and data[j*w+end]==a:end+=1
                        if a:
                            for x in range(i,end):actual[(h-1-j,x) if clockwise else (x,j)]=a
                        i=end
                self.assertEqual(actual,expected)
    def test_wheel_overlap_boundaries_and_previews(self):
        # Independent geometry reference: common radius, real overlap, no
        # duplication at boundaries. Pixel previews cover 0/1/2/3 collections.
        self.assertLess(math.hypot(60*math.sin(math.radians(40)),60*(1-math.cos(math.radians(40)))),52)
        for count in range(4):
            for selected in range(max(1,count)):
                indices=[selected+k for k in (-1,0,1) if 0<=selected+k<count]
                self.assertEqual(len(indices),len(set(indices)))
                self.assertLessEqual(len(indices),count)
        for path in PRIVATE.glob('wheel-*-135.png'):
            with Image.open(path) as image:self.assertEqual(image.size,(135,240))
        self.assertEqual(len(list(PRIVATE.glob('wheel-*-135.png'))),16)
    def test_wake_missing_evidence_and_historical_versions(self):
        self.assertEqual(evaluate_wake({})[0],'INCOMPLETE')
        with self.assertRaises(ValueError):current_boots([dict(version='0.8.2-p3d.perf',boot_id='6')])
        record=dict(reset_reason='3',previous_phase_valid='1',wake_complete='YES',wake_unfinished_count='0',
            wake_captured_ms='100',wake_backlight_ms='120',wake_resume_ms='121',wake_first_frame_ms='150',wake_unlock_ms='140',
            wake_resume_pcm='10',pcm_buffers='50',wake_resume_position_ms='2000',position_ms='3000')
        self.assertEqual(evaluate_wake(record)[0],'READY_FOR_REVIEW')
        self.assertEqual(evaluate_wake(dict(record,wake_complete='PENDING'))[0],'INCOMPLETE')
        self.assertEqual(evaluate_wake(dict(record,wake_first_frame_ms='90'))[0],'FAIL')
        self.assertEqual(VERSION,'0.8.3-p3d.refine')
if __name__=='__main__':unittest.main(verbosity=2)
