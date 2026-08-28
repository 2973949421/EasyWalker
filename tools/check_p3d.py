"""P3D production decision contracts and PC resource/log tests, not device PASS."""
import io
import re
import struct
import subprocess
import unittest
import zlib
from pathlib import Path
from PIL import Image
from prepare_p3_media import PACKAGE
from prepare_p3d import compile_cover,validate_cover,font_coverage,DEST
from validate_p3_free import evaluate_p3d
ROOT=Path(__file__).resolve().parents[1]
def source(p):return (ROOT/p).read_text(encoding='utf-8-sig')
class P3DChecks(unittest.TestCase):
    def test_production_screen_policy(self):
        compiler=r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'
        cmd=[compiler,'-std=gnu++14','-fsyntax-only','-Isrc','test/p3abc/display_contracts.cpp']
        good=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True)
        self.assertEqual(good.returncode,0,good.stderr)
        bad=subprocess.run(cmd+['-DREPRODUCE_OLD_WAKE_TIMER'],cwd=ROOT,capture_output=True,text=True)
        self.assertNotEqual(bad.returncode,0)
        self.assertIn('old five-second timer',bad.stderr)
    def test_cover_roundtrip_and_corruption(self):
        data=(PACKAGE/DEST).read_bytes();self.assertEqual(validate_cover(data),(135,173))
        for offset in (0,4,6,8,10,12,14,16,20,100):
            broken=bytearray(data);broken[offset]^=1
            with self.assertRaises(ValueError):validate_cover(broken)
        with self.assertRaises(ValueError):validate_cover(data[:-1])
        source_image=io.BytesIO();Image.new('RGB',(80,160),(255,0,0)).save(source_image,format='PNG');source_image.seek(0)
        image,data=compile_cover(source_image)
        self.assertEqual(image.size,(87,174));self.assertEqual(image.getpixel((0,60)),(255,0,0))
        self.assertEqual(struct.unpack_from('<H',data,24+(60*87+60)*2)[0],0xf800)
    def test_ui_fonts_and_arc_capacity(self):
        self.assertGreater(font_coverage(),50)
        fixed=source('src/player/ui/SettingsPanel.cpp').split('const char* fixed="')[1].split('";')[0]
        self.assertLess(len(set(fixed)),170)
        data=(PACKAGE/'ADVWalkman/fonts/latin-12.idx').read_bytes()
        metrics={r[0]:r for r in (struct.unpack_from('<IIHHhhhHI',data,i) for i in range(16,len(data),24))}
        for text in ('0.8.1-p3d.fix','Cardputer ADV','ADVWalkman','Benchmark'):
            self.assertLessEqual(sum(metrics[ord(c)][4] for c in text),123)
    def test_settings_binary_reference(self):
        # Exact production DSPL v1 wire format, independently parsed.
        raw=struct.pack('<4sHHII4B',b'DSPL',1,24,7,4,70,3,1,0)
        record=raw+struct.pack('<I',zlib.crc32(raw))
        self.assertEqual(len(record),24);self.assertEqual(struct.unpack_from('<I',record,20)[0],zlib.crc32(record[:20]))
        code=source('src/player/ui/DisplaySettingsStore.cpp')
        self.assertIn('std::memcmp(check,bytes_,24)',code);self.assertIn('writingRevision_=revision_',code)
        self.assertIn('dirty_=revision_!=writingRevision_',code);self.assertNotIn('Preferences',code.split('void DisplaySettingsStore::service')[1].replace('DisplayPreferences',''))
    def test_render_and_wake_lifecycle(self):
        ui=source('src/player/ui/UiCoordinator.cpp');main=source('src/player/app/PlayerDevMain.cpp')
        self.assertIn('if(power_.asleep())return;',ui)
        self.assertLess(main.index('ui.physicalActivity'),main.index('input.capture'))
        self.assertIn('input.capture(mask,now,ui.inputEpoch(),!allowed)',main)
        presenter=source('src/player/ui/NowPlayingPresenter.cpp')
        self.assertEqual(presenter.count('row_.setBuffer('),1)
        self.assertIn('row_.setBuffer(pixels_, G::width, G::rowHeight, 16)',presenter)
        borrow=source('src/player/ui/NowPlayingPresenter.h').split('M5Canvas* sharedRow()')[1].split('\n')[0]
        self.assertNotIn('setBuffer',borrow);self.assertIn('commit_.active?nullptr',borrow)
        for p in ('LibraryVisual.cpp','SettingsPanel.cpp'):
            render=source('src/player/ui/'+p).split('bool '+('LibraryVisual' if p.startswith('Library') else 'SettingsPanel')+'::render(')[1]
            self.assertNotIn('SD.',render);self.assertNotIn('pushImage',render);self.assertIn('pushSprite',render)
        self.assertNotIn('deep_sleep',ui+main)
    def test_launcher_has_no_dangerous_fallback(self):
        code=source('src/player/ui/SettingsPanel.cpp')
        self.assertIn('ESP_PARTITION_SUBTYPE_APP_TEST',code)
        for bad in ('APP_FACTORY','erase','player.stop(','0x10000'):
            self.assertNotIn(bad,code)
        self.assertIn('player.requestCheckpoint()',code);self.assertIn('if(!logOk)',code)
        self.assertLess(code.index('esp_ota_set_boot_partition'),code.index('esp_restart()'))
    def test_p3d_log_evidence(self):
        record=dict(settings_errors='0',launcher_errors='0',library_cover_errors='0',d_auto='COVERED',
            settings_changes='1',settings_writes='1',screen_sleeps='1',screen_wakes='1',library_cover_frames='1',vinyl_frames='4')
        self.assertEqual(evaluate_p3d(record)[0],'READY_FOR_REVIEW')
        for k in ('settings_errors','launcher_errors','library_cover_errors'):
            self.assertEqual(evaluate_p3d(dict(record,**{k:'1'}))[0],'FAIL')
        for k in ('settings_changes','settings_writes','screen_sleeps','screen_wakes','library_cover_frames'):
            self.assertEqual(evaluate_p3d(dict(record,**{k:'0'}))[0],'INCOMPLETE')
    def test_checkpoint_capacity_and_failure_exit(self):
        code=source('src/player/p3abc/FreeSession.cpp')
        body=code.split('void FreeSession::prepare(')[1].split('void FreeSession::service(')[0]
        strings=re.findall(r'append\("((?:\\.|[^"\\])*)"',body)
        paths={'restored_track':511,'nav_target':511,'browser_path':511,'media_track':511,'track':511,'resource_path':559,'pcm_peak_track':511,'lyric_peak_track':511}
        bounds={'version':32,'mode':4,'result':16,'a_auto':10,'b_auto':10,'c_auto':10,'d_auto':10,
            'launcher_error':64,'nav_error':64,'display_self_failure':64,'failure_component':23,'failure_reason':63,
            'resource_operation':23,'player_state':16,'page':16}
        total=0
        for encoded in strings:
            fmt=encoded.replace('\\n','\n')
            if fmt.startswith('%s_'):
                # Exact bounded fields, not an arbitrary per-component margin.
                numeric=re.sub(r'%l[ud]', '%d',fmt)
                for component in ('lyrics','cover','font','navigation'):
                    total+=len((numeric%(component,'x'*63,component,'x'*559,component,
                        4294967295,4294967295,4294967295,'x'*23,-2147483648,-2147483648,-2147483648)).encode())
                continue
            if fmt.startswith('event_'):
                total+=12*128;continue
            if fmt.startswith('store_%s'):
                phases=re.findall(r'"([a-z_]+)"',body.split('storagePhases[]={')[1].split('};')[0])[1:]
                total+=sum(len(fmt.replace('%s',phase).replace('%lu','9'*10).encode()) for phase in phases);continue
            if fmt.startswith('sleep_%s'):
                total+=sum(len((fmt.replace('%s',scene).replace('%u','255')).encode()) for scene in ('lyrics','cover','playlist','library','settings'));continue
            for line in fmt.splitlines(keepends=True):
                key=line.split('=',1)[0];limit=paths.get(key,bounds.get(key,32))
                value=re.sub(r'%[0-9]*[lu]*[udx]',lambda m:'9'*10,line)
                value=value.replace('%s','x'*limit);total+=len(value.encode())
        capacity=int(re.search(r'buffer_\[(\d+)\]',source('src/player/p3abc/FreeSession.h'))[1])
        print(f'LOG_BOUND: {total}/{capacity} bytes')
        self.assertLessEqual(total,capacity,'bounded paths must fit a complete log checkpoint')
        service=code.split('void FreeSession::service(')[1]
        for failure in ('checkpoint_buffer','log_buffer','open_log','write_log'):
            self.assertIn('fail("logging","'+failure+'")',service)
        self.assertIn('else if(!logFlushed_)',service)
        self.assertIn('lastSaveOk_=logOk&&persisted&&ui.displaySettingsSaved()',service)
        self.assertIn('navigationError_',source('src/player/ui/UiCoordinator.cpp'))
if __name__=='__main__':unittest.main(verbosity=2)
