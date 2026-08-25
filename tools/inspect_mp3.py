#!/usr/bin/env python3
"""Read-only inspection of the local P0 benchmark MP3.

Uses only the Python standard library. It reports the file hash and the first
valid MPEG Layer III frame. The script never modifies the input file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


BITRATES_KBPS = {
    "mpeg1": [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0],
    "mpeg2": [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0],
}
SAMPLE_RATES_HZ = [44_100, 48_000, 32_000]
CHANNEL_MODES = ["stereo", "joint_stereo", "dual_channel", "mono"]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def id3v2_size(data: bytes) -> int:
    if len(data) < 10 or data[:3] != b"ID3":
        return 0
    size_bytes = data[6:10]
    if any(value & 0x80 for value in size_bytes):
        raise ValueError("invalid ID3v2 synchsafe size")
    payload_size = 0
    for value in size_bytes:
        payload_size = (payload_size << 7) | value
    footer_size = 10 if data[5] & 0x10 else 0
    return 10 + payload_size + footer_size


def parse_frame_header(header: bytes) -> dict[str, object] | None:
    if len(header) != 4:
        return None
    value = int.from_bytes(header, "big")
    if (value >> 21) & 0x7FF != 0x7FF:
        return None

    version_bits = (value >> 19) & 0x3
    layer_bits = (value >> 17) & 0x3
    bitrate_index = (value >> 12) & 0xF
    sample_rate_index = (value >> 10) & 0x3
    padding = (value >> 9) & 0x1
    channel_mode_index = (value >> 6) & 0x3

    if version_bits == 1 or layer_bits != 1:
        return None
    if bitrate_index in (0, 15) or sample_rate_index == 3:
        return None

    if version_bits == 3:
        version = "MPEG-1"
        table = "mpeg1"
        divisor = 1
    elif version_bits == 2:
        version = "MPEG-2"
        table = "mpeg2"
        divisor = 2
    else:
        version = "MPEG-2.5"
        table = "mpeg2"
        divisor = 4

    bitrate_kbps = BITRATES_KBPS[table][bitrate_index]
    sample_rate_hz = SAMPLE_RATES_HZ[sample_rate_index] // divisor
    coefficient = 144_000 if version_bits == 3 else 72_000
    frame_length = coefficient * bitrate_kbps // sample_rate_hz + padding

    return {
        "mpeg_version": version,
        "layer": "Layer III",
        "bitrate_kbps": bitrate_kbps,
        "sample_rate_hz": sample_rate_hz,
        "channel_mode": CHANNEL_MODES[channel_mode_index],
        "frame_length_bytes": frame_length,
        "samples_per_frame": 1_152 if version_bits == 3 else 576,
    }


def detect_seek_header(data: bytes, frame_offset: int, frame_length: int) -> tuple[str, int | None]:
    """Find the standard first-frame VBR seek marker without decoding audio."""
    frame_end = min(len(data), frame_offset + frame_length)
    frame = data[frame_offset:frame_end]
    matches: list[tuple[int, str]] = []
    for marker in (b"Xing", b"Info", b"VBRI"):
        relative = frame.find(marker)
        if relative >= 0:
            matches.append((relative, marker.decode("ascii")))
    if not matches:
        return "none", None
    relative, name = min(matches)
    return name, frame_offset + relative


def inspect(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise FileNotFoundError(path)

    data = path.read_bytes()
    offset = id3v2_size(data[:10])
    first_frame: dict[str, object] | None = None
    first_frame_offset = 0
    bitrates: set[int] = set()
    sample_rates: set[int] = set()
    frame_count = 0
    duration_seconds = 0.0

    cursor = offset
    while cursor + 4 <= len(data):
        frame = parse_frame_header(data[cursor : cursor + 4])
        if frame is None:
            cursor += 1
            continue
        frame_length = int(frame["frame_length_bytes"])
        if frame_length < 4 or cursor + frame_length > len(data):
            cursor += 1
            continue
        if first_frame is None:
            first_frame = frame
            first_frame_offset = cursor
        bitrates.add(int(frame["bitrate_kbps"]))
        sample_rate = int(frame["sample_rate_hz"])
        sample_rates.add(sample_rate)
        duration_seconds += int(frame["samples_per_frame"]) / sample_rate
        frame_count += 1
        cursor += frame_length

    if first_frame is not None:
        seek_header, seek_header_offset = detect_seek_header(
            data, first_frame_offset, int(first_frame["frame_length_bytes"])
        )
        return {
            "path": str(path.resolve()),
            "size_bytes": path.stat().st_size,
            "sha256": sha256_file(path),
            "first_frame_offset": first_frame_offset,
            **first_frame,
            "frame_count": frame_count,
            "bitrate_mode": "vbr" if len(bitrates) > 1 else "cbr",
            "bitrates_kbps": sorted(bitrates),
            "sample_rates_hz": sorted(sample_rates),
            "duration_seconds_from_frames": round(duration_seconds, 6),
            "seek_header": seek_header,
            "seek_header_offset": seek_header_offset,
        }
    raise ValueError("no valid MPEG Layer III frame found")


def self_test() -> None:
    # MPEG-1 Layer III, 320 kbps, 44.1 kHz, stereo.
    frame = parse_frame_header(bytes.fromhex("FFFB E000"))
    assert frame is not None
    assert frame["bitrate_kbps"] == 320
    assert frame["sample_rate_hz"] == 44_100
    assert frame["channel_mode"] == "stereo"
    assert frame["frame_length_bytes"] == 1_044
    sample = bytes.fromhex("FF FB E0 00") + (b"\x00" * 32) + b"Xing"
    assert detect_seek_header(sample, 0, len(sample)) == ("Xing", 36)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        self_test()
        print("self-test: PASS")
        return 0
    if args.path is None:
        parser.error("path is required unless --self-test is used")

    try:
        print(json.dumps(inspect(args.path), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as error:
        print(f"inspect_mp3: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
