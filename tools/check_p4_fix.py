"""Host contracts for the current P4AB fix; no device-timing claims are made here."""
import re
import unittest
import zlib
from pathlib import Path

from prepare_expanded_libraries import KINO_LIBRARY_SIZE, validate_library_cover
from prepare_p4_fix import TIMES
from validate_p3_free import save_records

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "test-data/local/p3-media/package"


def source(relative):
    return (ROOT / relative).read_text(encoding="utf-8-sig")


class P4FixChecks(unittest.TestCase):
    def test_short_save_records_survive_a_torn_snapshot(self):
        def record(kind, body):
            prefix = f"{kind} {body} ".encode()
            return prefix + f"crc={zlib.crc32(prefix):08x}\n".encode()
        data = record("SAVE_BEGIN", "boot_id=3 ticket=7 page=Playlist player_revision=4 display_revision=2")
        data += b"BEGIN sequence=99\nsnapshot=full\nstore_publish_max_us=123\n"
        data += record("SAVE_END", "boot_id=3 ticket=7 outcome=StateSavedLogFailed failure_stage=snapshot player_revision=4 display_revision=2")
        records = save_records(data)
        self.assertEqual([item["record"] for item in records], ["SAVE_BEGIN", "SAVE_END"])
        self.assertEqual(records[-1]["outcome"], "StateSavedLogFailed")

    def test_log_is_bounded_and_rtc_storage_is_gone(self):
        header = source("src/player/p3abc/FreeSession.h")
        log = source("src/player/p3abc/FreeSession.cpp")
        runtime = source("src/player/ui/RuntimeDiagnostics.cpp")
        self.assertIn("char buffer_[1024]", header)
        self.assertIn("kSummaryIntervalMs=60000", log)
        self.assertIn("p5-free-current.txt", log)
        self.assertIn("kLogLimit=1024U*1024U", log)
        self.assertNotIn("RTC_NOINIT_ATTR", source("src/player/ui/RuntimeDiagnostics.h") + runtime)
        self.assertIn("case 11", log)
        transport = log.split("case 9:{", 1)[1].split("case 11:{", 1)[0]
        self.assertNotIn("persistencePhasePeakUs", transport)
        for stage in ("player_open", "player_write_header", "player_write_payload", "player_close",
                      "player_verify_open", "player_verify_read", "player_flush", "display_open",
                      "display_write", "display_flush", "display_verify_open", "display_verify",
                      "log_open", "log_write", "log_flush", "snapshot", "timeout"):
            self.assertIn(f'"{stage}"', log)
        # Each diagnostic section is formatted independently.  Literal text
        # plus the documented maximum expansion of its format conversions must
        # remain below the 768-byte design ceiling (and therefore below the
        # shared 1 KiB formatter).
        full_switch = log.split("switch(streamSection_++){", 1)[1].split(
            "void FreeSession::startNextTicket", 1
        )[0]
        formats = re.findall(r'append\("((?:[^"\\]|\\.)*)"', full_switch)
        self.assertGreaterEqual(len(formats), 20)
        for item in formats:
            literal_bytes = len(item.encode("utf-8"))
            conversions = item.count("%") - 2 * item.count("%%")
            # ESP counters are 32-bit here: 10 decimal digits plus separator.
            # Path/event and bounded failure strings live in isolated sections;
            # production append() additionally rejects snprintf truncation.
            self.assertLessEqual(literal_bytes + conversions * 12, 768, item[:120])
        # The two looped append formats have fixed iteration limits.
        self.assertLessEqual((len("store_%s_max_us=%lu\\n") + 24) * 15, 768)
        self.assertLessEqual((len("event_%lu=%lu,%s,%s,%d,%d,%u,captured_abs_ms:%lu\\n") + 96) * 4, 768)

    def test_playlist_warm_return_reuses_the_six_row_model(self):
        ui = source("src/player/ui/UiCoordinator.cpp")
        warm = ui.split("bool UiCoordinator::openBrowser", 1)[1].split(
            "void UiCoordinator::navigationFailed", 1
        )[0]
        retained = warm.split("if(page==UiPage::Playlist", 1)[1].split(
            "if (navigation_.state", 1
        )[0]
        self.assertIn("browserContextReady_=retained", retained)
        self.assertIn("prepareRow_=kP3AVisibleRows", retained)
        self.assertIn("metadataReadyRows_=metadataRows", retained)
        self.assertNotIn("entryAt(", retained)
        self.assertNotIn("openPath(", retained)

        move = ui.split("void UiCoordinator::movePlaylistSelection", 1)[1].split(
            "void UiCoordinator::cancelNavigation", 1
        )[0]
        same_window = move.split("if(browserContextReady_", 1)[1].split(
            "}else invalidateBrowser();", 1
        )[0]
        self.assertIn("feedbackRegions_", same_window)
        self.assertNotIn("entryAt(", same_window)
        self.assertNotIn("invalidateBrowser", same_window)

    def test_tab_and_centering_are_production_routes(self):
        controls = source("src/player/ui/P4Controls.h")
        route = source("src/player/ui/PlaybackPageRoute.h")
        renderer = source("src/player/ui/media/LyricsRenderer.cpp")
        self.assertIn("settings Tab returns to Player", controls)
        self.assertNotIn("inSettings", route)
        self.assertIn("(kHeight-used)/2", renderer)
        self.assertIn("sizeof(Glyph)==4", source("src/player/ui/media/LyricsRenderer.h"))

    def test_bilingual_timeline_and_kino_cover(self):
        timelines=[]
        for path in sorted((PACKAGE / "Lyrics/KINO").glob("01 - *.lrc")):
            values=[]
            for line in path.read_text(encoding="utf-8-sig").splitlines():
                match=re.match(r"^\[(\d+):(\d+)\.(\d+)\]",line)
                self.assertIsNotNone(match, (path,line))
                values.append((int(match[1])*60+int(match[2]))*1000+int(match[3])*10)
            timelines.append(tuple(values))
        self.assertEqual(len(timelines),2)
        self.assertEqual(timelines[0],TIMES)
        self.assertEqual(timelines[0],timelines[1])
        cover=PACKAGE / "ADVWalkman/library-covers/folders/KINO/cover.adv"
        self.assertEqual(validate_library_cover(cover),KINO_LIBRARY_SIZE)
        self.assertEqual(KINO_LIBRARY_SIZE, (135,154))
        for library in ("AveMujica","KINO","熱・情","粤语迷幻"):
            item=PACKAGE / "ADVWalkman/library-covers/folders" / library / "cover.adv"
            self.assertEqual(validate_library_cover(item),KINO_LIBRARY_SIZE)


if __name__ == "__main__":
    unittest.main(verbosity=2)
