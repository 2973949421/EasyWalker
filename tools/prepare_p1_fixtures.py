#!/usr/bin/env python3
"""Generate deterministic, non-copyrighted MP3 fixtures for ADV Walkman P1."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from inspect_mp3 import inspect as inspect_mp3


DEFAULT_FFMPEG = Path(
    r"B:\Tools\FFmpeg\ffmpeg-9.0.1-essentials_build\bin\ffmpeg.exe"
)
DEFAULT_FFPROBE = DEFAULT_FFMPEG.with_name("ffprobe.exe")


FIXTURES = (
    {
        "name": "cbr320-44100.mp3",
        "sample_rate": 44_100,
        "left_hz": 440,
        "right_hz": 660,
        "codec_args": ["-b:a", "320k"],
        "expected_mode": "cbr",
    },
    {
        "name": "vbr-v0-44100.mp3",
        "sample_rate": 44_100,
        "left_hz": 523,
        "right_hz": 784,
        "codec_args": ["-q:a", "0"],
        "expected_mode": "vbr",
    },
    {
        "name": "cbr320-48000.mp3",
        "sample_rate": 48_000,
        "left_hz": 659,
        "right_hz": 988,
        "codec_args": ["-b:a", "320k"],
        "expected_mode": "cbr",
    },
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run(command: list[str]) -> None:
    completed = subprocess.run(command, text=True, capture_output=True)
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "command failed")


def probe(ffprobe: Path, path: Path) -> dict[str, object]:
    completed = subprocess.run(
        [
            str(ffprobe),
            "-v",
            "error",
            "-select_streams",
            "a:0",
            "-show_entries",
            "stream=codec_name,sample_rate,channels,bit_rate,duration",
            "-show_entries",
            "format=duration,size,bit_rate",
            "-of",
            "json",
            str(path),
        ],
        text=True,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"ffprobe failed: {path}")
    return json.loads(completed.stdout)


def generate_fixture(ffmpeg: Path, output: Path, spec: dict[str, object]) -> None:
    rate = int(spec["sample_rate"])
    duration = 12
    command = [
        str(ffmpeg),
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-f",
        "lavfi",
        "-i",
        f"sine=frequency={spec['left_hz']}:duration={duration}:sample_rate={rate}",
        "-f",
        "lavfi",
        "-i",
        f"sine=frequency={spec['right_hz']}:duration={duration}:sample_rate={rate}",
        "-filter_complex",
        "[0:a]volume=0.08[l];[1:a]volume=0.08[r];"
        "[l][r]join=inputs=2:channel_layout=stereo[a]",
        "-map",
        "[a]",
        "-c:a",
        "libmp3lame",
        *[str(value) for value in spec["codec_args"]],
        str(output),
    ]
    run(command)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("test-data/local/p1-fixtures"),
    )
    parser.add_argument("--sd-root", type=Path)
    parser.add_argument("--ffmpeg", type=Path, default=DEFAULT_FFMPEG)
    parser.add_argument("--ffprobe", type=Path, default=DEFAULT_FFPROBE)
    args = parser.parse_args()

    if not args.ffmpeg.is_file() or not args.ffprobe.is_file():
        print("prepare_p1_fixtures: FFmpeg/ffprobe not found", file=sys.stderr)
        return 2

    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, object] = {"format": "adv_walkman_p1_fixtures_v1", "files": []}

    for spec in FIXTURES:
        path = output_dir / str(spec["name"])
        generate_fixture(args.ffmpeg, path, spec)
        manifest["files"].append(
            {
                "name": path.name,
                "expected_mode": spec["expected_mode"],
                "expected_sample_rate": spec["sample_rate"],
                "size_bytes": path.stat().st_size,
                "sha256": sha256_file(path),
                "probe": probe(args.ffprobe, path),
                "mp3_scan": inspect_mp3(path),
            }
        )

    # Keep several complete MPEG frames, then cut through the next one. This
    # is a real truncated stream: the probe must accept its valid beginning,
    # while playback must report Decoder/File Error instead of TrackEnded.
    cbr_path = output_dir / "cbr320-44100.mp3"
    cbr_scan = inspect_mp3(cbr_path)
    first_offset = int(cbr_scan["first_frame_offset"])
    frame_length = int(cbr_scan["frame_length_bytes"])
    truncate_at = first_offset + frame_length * 5 + frame_length // 2
    corrupt = output_dir / "corrupt-truncated.mp3"
    corrupt.write_bytes(cbr_path.read_bytes()[:truncate_at])
    manifest["files"].append(
        {
            "name": corrupt.name,
            "expected": "decoder_error_not_track_ended",
            "size_bytes": corrupt.stat().st_size,
            "sha256": sha256_file(corrupt),
            "mp3_scan": inspect_mp3(corrupt),
        }
    )

    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )

    if args.sd_root is not None:
        sd_root = args.sd_root.resolve(strict=True)
        destination = sd_root / "Music" / "ADVWalkmanTest"
        destination.mkdir(parents=True, exist_ok=True)
        for item in manifest["files"]:
            name = str(item["name"])
            source = output_dir / name
            target = destination / name
            shutil.copyfile(source, target)
            if target.stat().st_size != source.stat().st_size:
                raise RuntimeError(f"size mismatch after SD copy: {name}")
            if sha256_file(target) != sha256_file(source):
                raise RuntimeError(f"SHA-256 mismatch after SD copy: {name}")
        shutil.copyfile(manifest_path, destination / manifest_path.name)
        print(f"SD_FIXTURES={destination}")

    print(f"FIXTURE_MANIFEST={manifest_path}")
    for item in manifest["files"]:
        print(
            f"FIXTURE={item['name']} SIZE={item['size_bytes']} "
            f"SHA256={item['sha256']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
