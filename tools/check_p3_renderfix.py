"""0.8.4 production target-compiler contracts and PC asset references.

The M5GFX pixel self-check is compiled into boot; it is NOT executed by Python.
No tests below claim physical LCD correctness or real-time performance.
"""
import json,subprocess,unittest
from pathlib import Path
from PIL import Image
from prepare_p3_renderfix import PRIVATE,PACKAGE,FACES
from prepare_p3_refine import records
from validate_p3_free import VERSION,evaluate_render,current_boots
ROOT=Path(__file__).resolve().parents[1]
COMPILER=r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'

class RenderfixChecks(unittest.TestCase):
    def test_production_dispatch_commit_cancel_and_wheel(self):
        cmd=[COMPILER,'-std=gnu++14','-fsyntax-only','-Isrc','test/p3abc/render_contracts.cpp']
        good=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(good.returncode,0,good.stderr)
        for macro,reason in [('REPRODUCE_OLD_RENDER_FALLTHROUGH','owned pending stripe'),
                             ('REPRODUCE_OLD_FRAME_COMPLETION','only matching contiguous')]:
            bad=subprocess.run(cmd+['-D'+macro],cwd=ROOT,capture_output=True,text=True)
            self.assertNotEqual(bad.returncode,0);self.assertIn(reason,bad.stderr)
        runtime=subprocess.run([COMPILER,'-std=gnu++11','-fsyntax-only','-Isrc','-x','c++','-'],cwd=ROOT,capture_output=True,text=True,
            input='#include "player/ui/RenderContract.h"\nvoid check(){adv_walkman::player::FrameCommit f;f.begin(1,1,0,188,false);f.submit(1,1,0,18);f.cancel();}')
        self.assertEqual(runtime.returncode,0,runtime.stderr)

    def test_font_repertoire_advance_and_ink_bounds(self):
        report=json.loads((PRIVATE/'fonts.json').read_text(encoding='utf-8'))
        self.assertEqual({r['name'] for r in report},set(FACES))
        for r in report:
            self.assertFalse(r['missing']);self.assertEqual(r['embolden_high_res_radius'],1);self.assertEqual(r['raster_scale'],4)
            name=r['name'];stem=PACKAGE/'ADVWalkman/fonts'/name;oldstem=PRIVATE/'recovery/ADVWalkman/fonts'/name
            new,old=records(stem),records(oldstem);self.assertTrue(set(old)<=set(new));px=int(name.split('-')[-1])
            for cp,g in new.items():
                if cp in old and cp not in (0x2018,0x2019,0x201C,0x201D):self.assertEqual(g[4],old[cp][4])
                self.assertGreater(g[4],0)
                self.assertLessEqual(g[2],px);self.assertLessEqual(g[3]+g[6],px);self.assertGreaterEqual(g[6],0)
            now=stem.with_suffix('.vlw').read_bytes();before=oldstem.with_suffix('.vlw').read_bytes()
            sample='AveMujica' if 'latin' in name else '未分类音乐收藏'
            mass=lambda data,rs:sum(sum(data[rs[ord(c)][1]:rs[ord(c)][1]+rs[ord(c)][2]*rs[ord(c)][3]]) for c in sample)
            self.assertGreater(mass(now,new),mass(before,old)*1.05,name)

    def test_three_actual_glyph_names_and_fixed_regions(self):
        report=json.loads((PRIVATE/'previews.json').read_text(encoding='utf-8'))
        self.assertGreaterEqual(len(report),40)
        for case in report:
            self.assertEqual(case['wheel_top'],196)
            self.assertEqual(len(case['labels']),3 if case['count'] else 0)
            if case['count']==1:self.assertEqual(len(set(case['labels'])),1)
            if case['count']==2:self.assertEqual(len(set(case['labels'])),2)
            with Image.open(PRIVATE/(case['name']+'-135.png')) as im:
                self.assertEqual(im.size,(135,240))
        for stem in ('long-cn','long-en'):
            cases=[r for r in report if r['name'].startswith(stem) and r['name'].endswith('step3')]
            self.assertEqual(len({r['offset'] for r in cases}),3)
            images=[Image.open(PRIVATE/(r['name']+'-135.png')).convert('RGB') for r in cases]
            self.assertEqual(len({im.crop((0,196,135,240)).tobytes() for im in images}),1)
            self.assertEqual(len({im.crop((0,0,135,174)).tobytes() for im in images}),1)

    def test_library_pin_working_set(self):
        # Current marquee window and all three short names share 15KiB/200.
        rs={face:records(PACKAGE/'ADVWalkman/fonts'/face) for face in FACES}
        pins=set()
        for text,small in [('华文行楷音乐收藏',False),('AveMujica Music Collection',False),('未分类',True),('AveMujica',True),('华文行楷音乐收藏',True)]:
            for c in text[:10] if small else text:
                latin=ord(c)<256;size=(14 if latin else 12) if small else (22 if latin else 18)
                pins.add((f'library-{"latin" if latin else "cjk"}-{size}',ord(c)))
        used=sum((rs[face][cp][2]*rs[face][cp][3]+1)//2 for face,cp in pins)
        self.assertLessEqual(used,15360);self.assertLessEqual(len(pins),200)
        print(f'LIBRARY_GLYPHS: bytes={used} metrics={len(pins)}',flush=True)

    def test_false_frame_completion_is_not_acceptance(self):
        good=dict(render_contract='1',render_pixel_selfcheck='PASS',frame_starts='7',frame_rejects='0',frame_repairs='0',frame_failure='none',
            frame_pending='0',frame_expected_rows='188',frame_submitted_rows='188',frame_complete_ms='500',full_frames='5')
        self.assertEqual(evaluate_render(good)[0],'READY_FOR_REVIEW')
        self.assertEqual(evaluate_render({})[0],'INCOMPLETE')
        self.assertEqual(evaluate_render(dict(good,frame_submitted_rows='180'))[0],'FAIL')
        self.assertEqual(evaluate_render(dict(good,frame_pending='1'))[0],'FAIL')
        self.assertEqual(evaluate_render(dict(good,frame_rejects='1',frame_repairs='1'))[0],'FAIL')
        self.assertEqual(evaluate_render(dict(good,full_frames='0',patch_frames='99',fallback_frames='99'))[0],'INCOMPLETE')
        self.assertEqual(evaluate_render(dict(good,render_pixel_selfcheck='render_band_pixels'))[0],'FAIL')
        with self.assertRaises(ValueError):current_boots([dict(version='0.8.3-p3d.refine',boot_id='8')])
        self.assertEqual(VERSION,'0.10.2-p5.monochrome')

if __name__=='__main__':unittest.main(verbosity=2)
