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

    return {
        "mpeg_version": version,
        "layer": "Layer III",
        "bitrate_kbps": BITRATES_KBPS[table][bitrate_index],
        "sample_rate_hz": SAMPLE_RATES_HZ[sample_rate_index] // divisor,
        "channel_mode": CHANNEL_MODES[channel_mode_index],
    }


def inspect(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise FileNotFoundError(path)

    with path.open("rb") as stream:
        prefix = stream.read(10)
        offset = id3v2_size(prefix)
        stream.seek(offset)
        scan = stream.read(1024 * 1024)

    for index in range(max(0, len(scan) - 3)):
        frame = parse_frame_header(scan[index : index + 4])
        if frame is not None:
            return {
                "path": str(path.resolve()),
                "size_bytes": path.stat().st_size,
                "sha256": sha256_file(path),
                "first_frame_offset": offset + index,
                **frame,
            }
    raise ValueError("no valid MPEG Layer III frame found in the first 1 MiB")


def self_test() -> None:
    # MPEG-1 Layer III, 320 kbps, 44.1 kHz, stereo.
    frame = parse_frame_header(bytes.fromhex("FFFB E000"))
    assert frame is not None
    assert frame["bitrate_kbps"] == 320
    assert frame["sample_rate_hz"] == 44_100
    assert frame["channel_mode"] == "stereo"


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
