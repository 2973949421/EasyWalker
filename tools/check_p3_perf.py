"""0.8.2 PC asset/format/fault-reference checks, NOT real-time device proof."""
import json,struct,tempfile,unittest,zlib
from pathlib import Path
from prepare_p3_media import PACKAGE,LOCAL,parse_lrc,sha
from font_index_v2 import generate
from check_p3_optimization import OptimizationChecks,source

def validate_index(raw,vlw,old):
    magic,version,stride,slots,size,header=struct.unpack_from('<4sHHIII',raw)
    if (magic,version,stride)!=(b'FIDX',2,16) or slots not in (256,65536) or len(raw)!=512+slots*16:raise ValueError('header')
    if size!=len(vlw) or header!=zlib.crc32(vlw[:24]):raise ValueError('pair')
    known={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',old,i) for i in range(16,len(old),24))}
    for cp in range(slots):
        r=struct.unpack_from('<IHHhhhH',raw,512+cp*16)
        if cp not in known:
            if any(r):raise ValueError('missing record')
        else:
            g=known[cp]
            if r!=(*g[1:7],0):raise ValueError('record mismatch')
            if r[0]+r[1]*r[2]>size:raise ValueError('bounds')
    return len(known)

class PerfChecks(unittest.TestCase):
    def test_all_direct_index_records_and_bad_pairs(self):
        total=0
        for path in (PACKAGE/'ADVWalkman/fonts').glob('*.idx'):
            old=path.read_bytes();vlw=path.with_suffix('.vlw').read_bytes();raw=path.with_suffix('.idx2').read_bytes()
            total+=validate_index(raw,vlw,old)
            for at in (0,4,6,8,12,16):
                bad=bytearray(raw);bad[at]^=1
                with self.assertRaises(ValueError):validate_index(bad,vlw,old)
            wrong=bytearray(vlw);wrong[8]^=1
            with self.assertRaises(ValueError):validate_index(raw,wrong,old)
            cp=struct.unpack_from('<I',old,16)[0];bad=bytearray(raw);struct.pack_into('<I',bad,512+cp*16,len(vlw)+1)
            with self.assertRaises(ValueError):validate_index(bad,vlw,old)
        self.assertGreater(total,90000)
        print('IDX2 exact records:',total)

    def test_direct_lookup_is_one_page_and_stripes_exact(self):
        for cp in range(65536):
            page=512+(cp//32)*512;offset=(cp%32)*16
            self.assertEqual(page+offset,512+cp*16);self.assertLessEqual(offset+16,512)
        # Byte-level RGB565 reference: chunk boundaries may cut a scanline,
        # never a pixel. Same source formulas as the production ImageBand.
        for width,height,top in ((120,144,22),(135,135,26),(135,173,0),(87,174,0)):
            seen=set();bands=0
            for y in range(0,top+height,18):
                first=max(y,top);end=min(y+18,top+height);length=max(0,end-first)*width*2
                start=max(0,first-top)*width*2;done=0
                while done<length:
                    n=min(512,length-done);self.assertEqual(n%2,0)
                    for offset in range(done,done+n,2):
                        pixel=(start+offset)//2;x=(135-width)//2+pixel%width;local=top+pixel//width-y
                        self.assertTrue(0<=x<135 and 0<=local<18);self.assertNotIn(pixel,seen);seen.add(pixel)
                    done+=n
                if length:bands+=1
            self.assertEqual(len(seen),width*height);self.assertLess(bands,height)
        presenter=source('src/player/ui/NowPlayingPresenter.cpp')
        self.assertIn('media_.presentingLyrics()||media_.bandActive()',presenter)
        cover=source('src/player/ui/media/CoverRenderer.h')
        self.assertIn('band_.cancel()',cover)

    def test_resource_binding_and_originals(self):
        report=json.loads((LOCAL/'perf-resources.json').read_text())
        self.assertEqual(report['cues'],298);self.assertEqual(len(report['lyrics']),10)
        self.assertEqual(report['audio_packet_hash'],'SHA256=276f256a136f17fc6c74a4431a914c80198284b732c84d51e801db38c8ec3905')
        for entry in report['lyrics']:
            base=entry['song'];original=PACKAGE/f'Lyrics/AveMujica/{base}.lrc';translated=original.with_name(base+'.zh-Hans.lrc')
            self.assertEqual(sha(original),entry['original_sha256']);self.assertEqual(sha(translated),entry['translation_sha256'])
            stamps={t for t,_ in parse_lrc(original.read_bytes())}
            self.assertTrue(all(t in stamps for t,_ in parse_lrc(translated.read_bytes())))
            self.assertTrue((PACKAGE/f'ADVWalkman/covers/AveMujica/{base}.cover.adv').exists())
        self.assertFalse(list((PACKAGE/'Lyrics/AveMujica').glob('ankokutengoku*')))
        self.assertEqual(len(list((PACKAGE/'Music/AveMujica').glob('*.mp3'))),11)

    def test_storage_failure_reference_and_separated_operations(self):
        # A/B reference interruption matrix: inactive queue published first,
        # then matching session. Every incomplete new pair leaves old pair.
        for interrupted in range(17):
            slots={'qa':(1,True),'sa':(1,True),'qb':(0,False),'sb':(0,False)}
            for step in range(interrupted):
                if step==0:slots['qb']=(2,False)
                if step==7:slots['qb']=(2,True)
                if step==8:slots['sb']=(2,False)
                if step==15:slots['sb']=(2,True)
            pairs=[q[0] for q in (slots['qa'],slots['qb']) for s in (slots['sa'],slots['sb']) if q[1] and s[1] and q[0]==s[0]]
            self.assertEqual(max(pairs),2 if interrupted==16 else 1)
        store=source('src/player/storage/PlayerStateStore.cpp')
        for name,next_name in [('serviceQueuePrepare','serviceOpenTarget'),('serviceQueueFetch','serviceCloseTarget')]:
            body=store.split('void PlayerStateStore::'+name+'()')[1].split('void PlayerStateStore::'+next_name)[0]
            self.assertEqual(body.count('pathAt('),1);self.assertNotIn('while (',body)
        opening=store.split('void PlayerStateStore::serviceOpenTarget()')[1].split('void PlayerStateStore::serviceWriteHeader')[0]
        self.assertNotIn('ensureStateDirectory',opening);self.assertNotIn('remove(',opening)
        closing=store.split('void PlayerStateStore::serviceCloseTarget()')[1].split('void PlayerStateStore::serviceOpenVerify')[0]
        self.assertNotIn('flush(',closing);self.assertNotIn('open(',closing)
        complete=store.split('void PlayerStateStore::complete(')[1]
        self.assertLess(complete.index('if (jobFile_)'),complete.index('currentQueueSlot_ = slot'))
        self.assertIn('phase_ = JobPhase::Cleanup',complete)

if __name__=='__main__':unittest.main(verbosity=2)
