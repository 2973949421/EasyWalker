"""Closure assets, actual glyph layout, packed cache and reboot-log checks.
PC reference checks are not device timing/visual acceptance.
"""
import struct, unittest
from pathlib import Path
from prepare_p3_media import LOCAL,PACKAGE,GRIDS,RAMP,parse_lrc,pair_cues,validate_cover
from preview_p3_lyrics import Fonts,layout,render,columns
from validate_p3_free import VERSION,current_boots,reboot_evidence,evaluate
from prepare_song_library import MAPPING,probe

class ClosureChecks(unittest.TestCase):
    def test_all_songs_binding_and_tags(self):
        for _,base,lyric in MAPPING:
            audio=PACKAGE/'Music/AveMujica'/f'{base}.mp3'
            info=probe(audio);s=info['streams'][0]
            self.assertEqual((s['sample_rate'],s['channels'],s['bit_rate']),('44100',2,'320000'))
            self.assertTrue(info['format']['tags']['title']);self.assertEqual(info['format']['tags']['artist'],'Ave Mujica')
            self.assertEqual((PACKAGE/'Lyrics/AveMujica'/f'{base}.lrc').exists(),bool(lyric))
            validate_cover((PACKAGE/'ADVWalkman/covers/AveMujica'/f'{base}.cover.adv').read_bytes())
    def test_all_lyrics_real_glyph_bounds_and_pinned_budget(self):
        fonts=Fonts();total=0;max_bitmap=0;max_unique=0
        for original in (PACKAGE/'Lyrics').rglob('*.lrc'):
            if '.zh-' in original.name:continue
            translated=original.with_name(original.stem+'.zh-Hans.lrc')
            for ms,ja,zh in pair_cues(parse_lrc(original.read_bytes()),parse_lrc(translated.read_bytes())):
                _,pages=layout(ja,zh,fonts);total+=1
                for page in range(pages):
                    render(ja,zh,fonts,page);glyphs,_=layout(ja,zh,fonts,page)
                    unique=set(g[0] for g in glyphs);size=sum((fonts.glyph(c)[0].width*fonts.glyph(c)[0].height+1)//2 for c in unique)
                    max_bitmap=max(max_bitmap,size);max_unique=max(max_unique,len(unique))
                    self.assertLessEqual(size+1024,15*1024) # overlay/common UI headroom
                    self.assertLessEqual(len(unique)+32,200)
                # Pure chants deliberately do not duplicate the second language.
                if ja.startswith('(Daa-'):self.assertEqual(zh,'')
        self.assertEqual(total,298)
        print(f'ACTUAL_LYRICS={total} max_pinned_bitmap={max_bitmap} max_unique={max_unique}',flush=True)
    def test_font_set_packed_coverage_and_metrics(self):
        for name in ('cjk-12','cjk-14','cjk-16','cjk-18','latin-10','latin-12','latin-14'):
            p=PACKAGE/'ADVWalkman/fonts'/name;idx=p.with_suffix('.idx').read_bytes();bits=p.with_suffix('.vlw').read_bytes()
            for i in range(16,len(idx),24):
                cp,offset,w,h,advance,dx,dy,px,_=struct.unpack_from('<IIHHhhhHI',idx,i)
                self.assertLessEqual(w,18);self.assertLessEqual(h,18);self.assertGreater(advance,0)
                for alpha in bits[offset:offset+w*h:23]:self.assertLessEqual(abs(alpha-((alpha+8)//17)*17),8)
        # Full name must fit two 97px lines using the same 12px metrics as UI.
        idx=(PACKAGE/'ADVWalkman/fonts/latin-12.idx').read_bytes();m={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',idx,i) for i in range(16,len(idx),24))}
        width=lambda t:sum(m[ord(c)][4] for c in t)
        self.assertGreater(width('ADVWalkmanBenchmark'),97)
        self.assertLessEqual(width('ADVWalkman'),97);self.assertLessEqual(width('Benchmark'),97)
    def test_metadata_and_ui_font_coverage(self):
        required=set()
        for _,base,_ in MAPPING:
            tags=probe(PACKAGE/'Music/AveMujica'/f'{base}.mp3')['format']['tags']
            required.update(ord(c) for key in ('title','artist','album') for c in tags.get(key,'') if ord(c)>=256)
        for name in ('cjk-12','cjk-14'):
            data=(PACKAGE/'ADVWalkman/fonts'/f'{name}.idx').read_bytes()
            available={struct.unpack_from('<I',data,i)[0] for i in range(16,len(data),24)}
            self.assertFalse(required-available,f'{name} missing UI metadata glyphs: {sorted(required-available)}')
    def test_ascii_formats_and_default_grid(self):
        self.assertEqual(RAMP,''.join(map(chr,range(32,127))))
        for p in (PACKAGE/'ADVWalkman/covers').rglob('*.cover.adv'):
            data=p.read_bytes();validate_cover(data);self.assertEqual(struct.unpack_from('<HH',data,12),(40,32))
        self.assertIn((30,24),GRIDS);self.assertIn((48,40),GRIDS)
    def test_boot_grouping_and_manual_checkpoint_comparison(self):
        before=dict(version=VERSION,boot_id='4',manual_checkpoint='1',track='/Music/AveMujica/ether.mp3',position_ms='48000',preferred_view='1')
        after=dict(version=VERSION,boot_id='5',restored_track=before['track'],restored_position_ms='48020',restored_view='1',startup_paused='1',startup_silent='1',startup_observed_ms='3000')
        groups=current_boots([dict(version='0.7.4-p3c.tune',result='FAIL'),before,after])
        self.assertEqual(len(groups),2);self.assertTrue(reboot_evidence(groups))
        after['restored_view']='0';self.assertFalse(reboot_evidence(groups))
        after['restored_view']='1';after['startup_observed_ms']='20';self.assertFalse(reboot_evidence(groups))

if __name__=='__main__':unittest.main(verbosity=2)
