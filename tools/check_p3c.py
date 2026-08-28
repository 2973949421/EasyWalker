"""Host resource/reference checks. Device rendering/audio still requires Gate."""
import struct
import unittest
import zlib
from pathlib import Path
from PIL import Image
from prepare_p3_media import LOCAL, PACKAGE, parse_lrc, pair_cues, validate_cover
from inspect_player_state import parse_session_payload
from preview_p3_lyrics import Fonts,layout,render
from validate_p3abc_gate import validate,fields,VERSION


class P3CChecks(unittest.TestCase):
    def test_real_lyrics_and_original_preserved(self):
        source=(LOCAL/'crucifix-x.user.ja.lrc').read_bytes()
        bound=(PACKAGE/'Lyrics/ADVWalkmanBenchmark/benchmark.lrc').read_bytes()
        self.assertEqual(source,bound)
        original=parse_lrc(source)
        translated=parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes())
        self.assertEqual(len(original),29)
        self.assertEqual(len(pair_cues(original,translated)),29)
        self.assertTrue(all(c[2] for c in pair_cues(original,translated)))

    def test_bom_multi_precision_offset(self):
        self.assertEqual(parse_lrc(b'\xef\xbb\xbf[offset:-100]\n[00:01.2][00:02.34]hi'),[(1100,'hi'),(2240,'hi')])
        self.assertEqual(parse_lrc(b'[00:00.001]x\n[offset:200]'),[(201,'x')])

    def test_nearest_unused_stable_pair(self):
        self.assertEqual(pair_cues([(1000,'a'),(1010,'b'),(5000,'c')],[(1010,'B'),(990,'A')]),[(1000,'a','A'),(1010,'b','B'),(5000,'c','')])

    def test_empty_and_bad_utf8(self):
        self.assertEqual(parse_lrc(b''),[])
        with self.assertRaises(UnicodeError):parse_lrc(b'[00:01]bad\xff')

    def test_limits(self):
        with self.assertRaises(ValueError):parse_lrc(b'x'*131073)
        with self.assertRaises(ValueError):parse_lrc(b'x'*1025)
        with self.assertRaises(ValueError):parse_lrc(b'[00:01]x\n'*513)
        with self.assertRaises(ValueError):parse_lrc(b'[00:99]x')
        self.assertEqual(len(parse_lrc(b'[00:01]x\n'*512)),512)

    def test_font_index_bounds_and_required_coverage(self):
        required={ord(c) for _,s in parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes())+parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes()) for c in s if ord(c)>=256}
        for size in (12,14,16):
            stem=PACKAGE/f'ADVWalkman/fonts/cjk-{size}'
            data=stem.with_suffix('.idx').read_bytes();vlw=stem.with_suffix('.vlw').read_bytes()
            magic,v,record,count,length=struct.unpack('<4sHHII',data[:16])
            self.assertEqual((magic,v,record,length),(b'FIDX',1,24,len(vlw)))
            self.assertEqual(len(data),16+24*count)
            records=[struct.unpack_from('<IIHHhhhHI',data,16+i*24) for i in range(count)]
            self.assertEqual([r[0] for r in records],sorted(set(r[0] for r in records)))
            self.assertTrue(required.issubset({r[0] for r in records}))
            for cp,offset,w,h,adv,dx,dy,px,_ in records:
                self.assertLessEqual(offset+w*h,len(vlw))
                self.assertLessEqual(w*h,256)
                self.assertEqual(px,size)

    def test_actual_multi_column_pixel_bounds(self):
        fonts=Fonts()
        cues=pair_cues(parse_lrc((LOCAL/'crucifix-x.user.ja.lrc').read_bytes()),parse_lrc((LOCAL/'crucifix-x.zh-Hans.lrc').read_bytes()))
        for _,original,chinese in cues:
            glyphs,pages=layout(original,chinese,fonts)
            self.assertGreater(len(glyphs),0)
            for page in range(pages):render(original,chinese,fonts,page)

    def test_long_page_and_right_to_left(self):
        fonts=Fonts()
        glyphs,pages=layout('界'*120,'中文',fonts)
        self.assertGreater(pages,1)
        first_chinese=[g for g in glyphs if g[0] in '中文']
        next_page,_=layout('界'*120,'中文',fonts,1)
        self.assertEqual(first_chinese,[g for g in next_page if g[0] in '中文'])
        self.assertGreater(first_chinese[0][1],next(g[1] for g in glyphs if g[0]=='界'))
        # Same-language continuation is to the left, reading downward first.
        original=[g for g in glyphs if g[0]=='界']
        self.assertGreater(original[0][1],original[12][1])
        for page in range(pages):render('界'*120,'中文',fonts,page)

    def test_dense_latin_page_uses_bounded_full_capacity(self):
        fonts=Fonts()
        glyphs,pages=layout('i'*1024,'中文',fonts)
        self.assertGreater(len(glyphs),100)
        self.assertLessEqual(len(glyphs),258)
        for page in range(pages):render('i'*1024,'中文',fonts,page)

    def test_pause_seek_pagination_reference(self):
        choose=lambda ms,n,start,end:min(n-1,max(0,ms-start)*n//max(1,end-start))
        self.assertEqual(choose(5000,4,0,10000),2)
        self.assertEqual(choose(5000,4,0,10000),choose(5000,4,0,10000))
        self.assertEqual(choose(1000,4,0,10000),0)
        self.assertEqual(choose(9999,4,0,10000),3)

    def test_cover_crc_and_rgb565(self):
        data=(PACKAGE/'ADVWalkman/covers/ADVWalkmanBenchmark/benchmark.cover.adv').read_bytes()
        self.assertEqual(validate_cover(data),(135,135))
        for malformed in (data[:20],data[:-1],b'BAD!'+data[4:],data[:30]+bytes([data[30]^1])+data[31:]):
            with self.assertRaises(ValueError):validate_cover(malformed)
        self.assertEqual(struct.unpack_from('<HH',data,12),(40,32))
        preview=Image.open(LOCAL/'previews/p3d-fix/ADVWalkmanBenchmark/benchmark.png').convert('RGB')
        for i,(r,g,b) in enumerate(preview.get_flattened_data()):
            self.assertEqual(struct.unpack_from('<H',data,28+i*2)[0],((r>>3)<<11)|((g>>2)<<5)|(b>>3))

    def test_five_grids_are_glyphs_not_thumbnail(self):
        for cols,rows in ((26,20),(30,24),(34,26),(40,32),(48,40)):
            lines=(LOCAL/f'previews/crucifix-x-{cols}x{rows}.txt').read_text().splitlines()
            self.assertEqual(len(lines),rows)
            self.assertTrue(all(len(s)==cols for s in lines))
            self.assertGreater(len(set(''.join(lines))),4)

    def test_session_reserved_byte_compatibility(self):
        payload=bytearray(24)
        for value,expected in ((0,'lyrics'),(1,'cover'),(2,'lyrics'),(255,'lyrics')):
            payload[21]=value
            self.assertEqual(parse_session_payload(payload)['preferred_now_playing_view'],expected)

    def test_view_fallback_does_not_overwrite_preference(self):
        view=lambda has,preferred:preferred if has else 1
        for preferred in (0,1):self.assertEqual([view(has,preferred) for has in (True,False,True)],[preferred,1,preferred])

    def test_log_validator_rejects_missing_or_weakened_evidence(self):
        # Synthetic dictionaries test the validator, NOT device acceptance.
        a=dict(version=VERSION,result='PASS',library_text_lines='2',library_text_width_px='90',
               library_text_available_px='97',library_text_truncated='0',library_text_invalid_utf8='0',
               library_text_layout_error='0',library_text_is_benchmark='1',orientation='135x240',rotation='2',
               sample_rate='44100',player_state='PLAYING',backpressure='0',pcm_gap_over_100ms='0',
               player_error='NONE',audio_error='none',pcm_buffers='20',compact_navigation='1',automatic_routes='1',
               events=''.join('LIBRARY:'+a+':x1:y1:fn0:n1|' for a in ('LEFT','RIGHT','UP','DOWN','CONFIRM','BACK')))
        audio=dict(sample_rate='44100',backpressure='0',audio_error='none',audio_error_events='0',pcm_buffers='100',pcm_gap_max_us='70000')
        b=dict(audio,version=VERSION,result='PASS',task_executed='1',measurement_ms='60000',
               model_failure='none',draw_failure='none',overlay_failure='none',audio_failure='none')
        c=dict(audio,version=VERSION,result='RUNNING',final_result='PASS',restore_view='cover',
               measurement_frames='20',measurement_resource_bytes='1000',media_budget_bytes='48000',
               preflight_pass='1',input_selfcheck='1',file_quota_checked='1',sd_max_files='12',file_quota_opened='10',
               measurement_started='1',measurement_ms='60000',primary_failure='none',failure_captured='0',
               frame_present_max_us='90000',lyric_late_max_ms='180',lyric_deadline_updates='4')
        for key in ('task_executed','display_confirmed','lyrics_confirmed','cover_confirmed','view_key_confirmed',
                    'pause_checked','seek_checked','fallback_checked','audio_user_confirmed','reboot_checked','restore_paused'):
            c[key]='1'
        for key in ('font_missing','font_io_errors','font_draw_misses','font_capacity_errors','lyrics_layout_error','lyrics_invalid_utf8','frame_present_sd_reads','lyric_missed_deadlines'):c[key]='0'
        self.assertTrue(validate([a,b,c]))
        for which,key,value in ((0,'result','FAIL'),(0,'library_text_lines','1'),(0,'rotation','0'),
                                (1,'task_executed','0'),(1,'pcm_gap_max_us','70001'),(2,'final_result','FAIL'),
                                (2,'reboot_checked','0'),(2,'audio_user_confirmed','0'),(2,'font_draw_misses','1'),
                                (2,'measurement_resource_bytes','0'),(2,'media_budget_bytes','49153'),(2,'version','old')):
            logs=[a.copy(),b.copy(),c.copy()];logs[which][key]=value
            with self.subTest(which=which,key=key):
                with self.assertRaises(ValueError):validate(logs)
        for key,value in (('frame_present_max_us','100001'),('lyric_late_max_ms','201'),('frame_present_sd_reads','1'),
                          ('preflight_pass','0'),('measurement_ms','59999'),('pcm_gap_max_us','NA')):
            logs=[a.copy(),b.copy(),c.copy()];logs[2][key]=value
            with self.subTest(key=key):
                with self.assertRaises(ValueError):validate(logs)
        self.assertEqual(fields('final_result=PASS\nfinal_result=FAIL')['final_result'],'FAIL')


if __name__=='__main__':unittest.main(verbosity=2)
