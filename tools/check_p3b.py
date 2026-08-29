"""P3B local geometry/reference/source-contract checks; never opens SD/serial.

Compile-time tests in P3BChecks.cpp exercise production timing helpers.
Injected-clock and M5GFX pixel tests are compiled for the later device Gate;
this script does NOT report those runtime tests as having run on the ADV.
"""

import argparse
import configparser
import hashlib
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "src/player/ui"


def read(path):
    return path.read_text(encoding="utf-8-sig")


def constants():
    return {name: int(value) for name, value in re.findall(
        r"static constexpr (?:int|uint32_t) (\w+) = (\d+);",
        read(UI / "NowPlayingModel.h"))}


class P3BLocalChecks(unittest.TestCase):
    def test_regions_tile_canvas(self):
        c = constants()
        self.assertEqual((c["width"], c["height"]), (135, 240))
        self.assertEqual(c["headerHeight"], c["contentY"])
        self.assertEqual(c["contentY"] + c["contentHeight"], c["footerY"])
        self.assertEqual(c["footerY"] + c["footerHeight"], 240)

    def test_overlay_stays_in_content(self):
        c = constants()
        self.assertGreaterEqual(c["overlayY"], c["contentY"])
        self.assertLessEqual(c["overlayY"] + c["overlayHeight"], c["footerY"])
        self.assertGreaterEqual(c["overlayX"], 0)
        self.assertLessEqual(c["overlayX"] + c["overlayWidth"], c["width"])

    def test_row_memory_and_actual_text_boxes(self):
        c = constants()
        self.assertEqual(c["width"] * c["rowHeight"] * 2, 4860)
        for x, y, w, h in [(6, 1, 123, 15), (6, 0, 123, 12), (33, 2, 96, 16)]:
            self.assertGreaterEqual(min(x, y), 0)
            self.assertLessEqual(x + w, 135)
            self.assertLessEqual(y + h, 18)
        self.assertNotIn('UiTextLayout::draw(row_, "Original"',read(UI/'NowPlayingPresenter.cpp'))

    def test_long_title_utf8_reference(self):
        title = "曲名-日本語-" * 15
        encoded = title.encode("utf-8")
        self.assertGreater(len(encoded), 128)
        # Reference chunk boundary contract: no valid UTF-8 split, no lost tail.
        chunks, current = [], ""
        for char in title:
            if len((current + char).encode("utf-8")) >= 128:
                chunks.append(current)
                current = ""
            current += char
        chunks.append(current)
        self.assertEqual("".join(chunks), title)
        self.assertTrue(all(len(s.encode("utf-8")) < 128 for s in chunks))
        self.assertIn("while (offset < length)", read(UI / "UiTextLayout.cpp"))

    def test_reference_timing_boundaries(self):
        c = constants()
        speed, hold = c["kScrollPixelsPerSecond"], c["kHoldMs"]
        self.assertEqual((speed, hold, c["kAnimationIntervalMs"]), (24, 5000, 50))
        for distance in [1, 120, 1024, 5000]:
            travel = (distance * 1000 + speed - 1) // speed
            self.assertLess((travel - 1) * speed // 1000, distance)
            self.assertGreaterEqual(travel * speed // 1000, distance)
        self.assertEqual(c["kVolumeDurationMs"], 3000)

    def test_snapshot_not_ui_clock(self):
        model = read(UI / "NowPlayingModel.cpp")
        tick = model.split("void NowPlayingModel::tick(")[1].split(
            "void NowPlayingModel::notifyVolumeAdjusted")[0]
        self.assertNotIn("positionMs", tick)
        self.assertNotIn("lastPlayerSecond_", read(UI / "UiCoordinator.cpp"))
        presenter = read(UI / "NowPlayingPresenter.cpp")
        self.assertIn("snapshot.positionMs, snapshot.durationMs", presenter)
        self.assertNotIn("Mp3Probe", presenter)

    def test_path_qualified_metadata(self):
        runtime = read(ROOT / "src/player/app/LibraryRuntime.cpp")
        request = runtime.split("LibraryRuntime::requestMetadataPath(")[1].split(
            "LibraryResult LibraryRuntime::startMetadataReader")[0]
        self.assertNotIn("openDirectory", request)
        self.assertNotIn("selectTrack", request)
        self.assertIn("metadataFromEntry_ = false", request)
        self.assertIn("metadataRequestActive_ && metadataFromEntry_", runtime)
        self.assertIn("metadataForPath(model_.path", read(UI / "NowPlayingPresenter.cpp"))
        self.assertIn("cachedMetadataForPath(path,metadata", read(UI / "UiCoordinator.cpp"))

    def test_renderer_has_no_sd_or_volume_write(self):
        source = read(UI / "NowPlayingPresenter.cpp")
        draw = source.split("void NowPlayingPresenter::prepareRow")[1]
        for forbidden in ["SD.", "fs.open", "requestMetadata", "setVolume", "createSprite"]:
            self.assertNotIn(forbidden, draw)
        self.assertIn("display.setClipRect(G::overlayX", draw)
        self.assertIn("DirtyOverlay", draw)

    def test_gate_checks_do_not_weaken_audio(self):
        source = read(UI / "P3BChecks.cpp")
        self.assertIn("pcmSubmitGapMaxUs > 70000", source)
        self.assertIn("audio.backpressureEvents", source)
        self.assertIn('"p3b_display"', source)
        self.assertIn('"p3b_audio"', source)
        self.assertIn('"SKIPPED"', source)
        self.assertIn("static_assert(NowPlayingModel::offsetAt", source)

    def test_source_filters_and_versions(self):
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(ROOT / "platformio.ini", encoding="utf-8-sig")
        for env in ["player-dev", "player-p3a-gate"]:
            section = ini[f"env:{env}"]
            self.assertIn("0.9.1-p4ab.fix", section["build_flags"])
            self.assertEqual(int(section["custom_launcher_app_limit"], 0), 0x140000)
        for env in ["player-p1-gate-a", "player-p1-gate-b", "player-p2-gate"]:
            self.assertIn("-<player/ui/**>", ini[f"env:{env}"]["build_src_filter"])


def check_artifacts():
    for env, filename in [
        ("player-dev", "ADV-Walkman-Dev.bin"),
        ("player-p3a-gate", "ADV-Walkman-P3A-Gate.bin"),
        ("player-p2-gate", "ADV-Walkman-P2-Gate.bin"),
    ]:
        artifact = ROOT / "artifacts" / filename
        built = ROOT / ".pio/build" / env / "firmware.bin"
        data = artifact.read_bytes()
        assert 0 < len(data) <= 0x140000, (filename, len(data))
        assert data == built.read_bytes(), f"stale artifact: {filename}"
        if env != "player-p2-gate":
            assert (ROOT / ".pio/build" / env / "src/player/ui/P3BChecks.cpp.o").is_file()
        print(f"{filename} size={len(data)} sha256={hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", action="store_true")
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(P3BLocalChecks)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.artifacts:
        check_artifacts()
    print("LOCAL CHECKS PASS; ADV runtime/drawing/audio validation remains DEVICE TEST.")
