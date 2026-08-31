"""Current state ownership, save-ticket and streaming-log host contracts.

These tests exercise production pure state types with the target compiler.
They do not claim LCD timing, SD latency, or audio continuity on hardware.
"""
import re
import subprocess
import unittest
import zlib
from pathlib import Path

from validate_p3_free import VERSION, checkpoints, full_records

ROOT = Path(__file__).resolve().parents[1]
COMPILER = r'B:\PlatformIO\packages\toolchain-xtensa-esp32s3\bin\xtensa-esp32s3-elf-g++.exe'


class StabilityChecks(unittest.TestCase):
    def test_production_transactions_and_1000_retargets(self):
        run = subprocess.run(
            [COMPILER, '-std=gnu++14', '-fsyntax-only', '-Isrc',
             'test/p3abc/render_contracts.cpp'], cwd=ROOT,
            capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)

    def test_old_ready_band_cannot_gate_new_browser_model(self):
        ui = (ROOT/'src/player/ui/UiCoordinator.cpp').read_text(encoding='utf-8')
        scheduler = (ROOT/'src/player/ui/UiWorkScheduler.h').read_text(encoding='utf-8')
        self.assertNotIn('resourceTurn_', ui)
        self.assertLess(scheduler.index('(!value.player&&!value.browserReady)'),
                        scheduler.index('value.libraryBandWaiting'))
        self.assertIn('libraryPage_.checkProgress', ui)
        self.assertIn('LibraryRecoveryAction::Error', ui)

    def test_log_is_streamed_with_one_kib_scratch(self):
        header = (ROOT/'src/player/p3abc/FreeSession.h').read_text(encoding='utf-8')
        source = (ROOT/'src/player/p3abc/FreeSession.cpp').read_text(encoding='utf-8')
        self.assertRegex(header, r'char buffer_\[1024\]')
        self.assertNotIn('buffer_[16384]', header)
        self.assertIn('std::min<size_t>(512,length_-written_)', source)
        self.assertIn('streamCrc_=mediaCrc(', source)
        self.assertIn('snapshot=summary', source)
        self.assertIn('snapshot=full', source)
        self.assertIn('Event events_[32]', header)

    def test_summary_cannot_impersonate_full_checkpoint(self):
        def record(sequence, snapshot, body):
            start = f'BEGIN sequence={sequence}\n'.encode() + \
                    f'snapshot={snapshot}\nversion={VERSION}\nmode=free\n'.encode() + body
            return start + f'END sequence={sequence} crc={zlib.crc32(start):08x}\n'.encode()
        records = checkpoints(record(1, 'summary', b'result=INCOMPLETE\n') +
                              record(2, 'full', b'result=INCOMPLETE\n'))
        self.assertEqual(len(records), 2)
        self.assertEqual([r['sequence'] for r in full_records(records)], ['2'])

    def test_stage_a_version_and_single_keyboard_update(self):
        self.assertEqual(VERSION, '0.10.2-p5.monochrome')
        main = (ROOT/'src/player/app/PlayerDevMain.cpp').read_text(encoding='utf-8')
        loop = main.split('void loop()')[1]
        self.assertEqual(loop.count('M5Cardputer.update()'), 1)
        collect = main.split('void collectInput()')[1].split('void dispatchInput()')[0]
        self.assertNotIn('updateKeyList', collect)

    def test_page_state_and_font_leases_have_distinct_owners(self):
        header = (ROOT/'src/player/ui/UiCoordinator.h').read_text(encoding='utf-8')
        playlist = (ROOT/'src/player/ui/PlaylistPageController.h').read_text(encoding='utf-8')
        fonts = (ROOT/'src/player/ui/media/FontCache.h').read_text(encoding='utf-8')
        media = (ROOT/'src/player/ui/media/NowPlayingMedia.cpp').read_text(encoding='utf-8')
        self.assertIn('private PlaylistPageController', header)
        for field in ('playlistSelected_', 'metadataReadyRows_', 'dirtyRegions_',
                      'retainedPlaylist_', 'locateCurrent_'):
            self.assertIn(field, playlist)
            self.assertNotIn(field, header)
        self.assertIn('Playlist=4', fonts)
        self.assertIn('Library=8', fonts)
        self.assertIn('Player=Next|Current|Ui', fonts)
        self.assertNotIn('fonts_->clearPins();', media)


if __name__ == '__main__':
    unittest.main(verbosity=2)
