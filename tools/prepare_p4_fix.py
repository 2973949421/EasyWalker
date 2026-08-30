"""Prepare and validate the current P4AB private SD resources."""
from __future__ import annotations

import argparse
import json
import re

from prepare_p3_media import LOCAL, PACKAGE
from prepare_expanded_libraries import KINO_LIBRARY_SIZE, validate_library_cover, write_kino_portrait_cover

TIMES = (
    44750, 48420, 54110, 60830, 64550, 69630,
    76030, 79390, 84440, 91660, 96190, 100310,
    124310, 128510, 133460, 140670, 144430, 148430,
    155750, 158950, 164110, 171100, 175990, 179990,
)

LRC_LINE = re.compile(r"^\[[0-9]{1,3}:[0-9]{2}(?:[.:][0-9]{1,3})?\](.*)$")
LYRIC_DIRECTORY = PACKAGE / "Lyrics" / "KINO"
LYRIC_NAMES = ("01 - Группа крови.lrc", "01 - Группа крови.zh-Hans.lrc")


def stamp(milliseconds: int) -> str:
    minutes, rest = divmod(milliseconds, 60000)
    seconds, fraction = divmod(rest, 1000)
    return f"[{minutes:02d}:{seconds:02d}.{fraction // 10:02d}]"


def retime_blood_type() -> None:
    for name in LYRIC_NAMES:
        path = LYRIC_DIRECTORY / name
        texts = []
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            match = LRC_LINE.match(line.strip())
            if match and match.group(1).strip():
                texts.append(match.group(1).strip())
        if len(texts) != len(TIMES):
            raise ValueError(f"blood_type_cue_count:{path}:{len(texts)}:{len(TIMES)}")
        path.write_text("".join(f"{stamp(at)}{text}\n" for at, text in zip(TIMES, texts)), encoding="utf-8")
    manifest_path = LOCAL / "expanded-libraries" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["blood_type_timing"] = {
        "audio_duration_ms": 284003,
        "source": "LRCLIB id 1023396, duration 284.0; existing bilingual line segmentation retained",
        "anchors_ms": [TIMES[0], TIMES[6], TIMES[12], TIMES[18]],
        "device_listening": "PENDING",
    }
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def validate() -> None:
    parsed = []
    for name in LYRIC_NAMES:
        values = []
        for line in (LYRIC_DIRECTORY / name).read_text(encoding="utf-8-sig").splitlines():
            match = re.match(r"^\[(\d+):(\d+)\.(\d+)\](.+)$", line)
            if not match:
                raise ValueError(f"blood_type_lrc_syntax:{name}:{line}")
            values.append((int(match.group(1)) * 60 + int(match.group(2))) * 1000 + int(match.group(3)) * 10)
        if tuple(values) != TIMES or any(a >= b for a, b in zip(values, values[1:])) or values[-1] >= 284003:
            raise ValueError(f"blood_type_timeline:{name}")
        parsed.append(values)
    if parsed[0] != parsed[1]:
        raise ValueError("blood_type_bilingual_mismatch")
    cover = PACKAGE / "ADVWalkman" / "library-covers" / "folders" / "KINO" / "cover.adv"
    if validate_library_cover(cover) != KINO_LIBRARY_SIZE:
        raise ValueError("kino_cover_contract")
    print(f"P4_FIX_RESOURCES_PASS cues={len(TIMES)} kino={KINO_LIBRARY_SIZE}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retime-blood-type", action="store_true")
    parser.add_argument("--refresh-kino-cover", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.retime_blood_type:
        retime_blood_type()
    if args.refresh_kino_cover:
        write_kino_portrait_cover()
    if args.verify:
        validate()


if __name__ == "__main__":
    main()
