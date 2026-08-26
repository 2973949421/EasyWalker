#!/usr/bin/env python3
"""Prepare the ignored P2 library fixture and optionally mirror it to SD.

Only a target containing this tool's marker may be rebuilt or removed.
The generated MP3 payload is derived from the existing copyright-free P1
fixture; no user music is copied into the repository.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


MARKER_NAME = ".adv-walkman-p2-fixture.json"
FIXTURE_VERSION = 1
FIXTURE_OWNER = "adv-walkman-p2-fixture"
BENCHMARK_RELATIVE_PATH = Path("Music/ADVWalkmanBenchmark/benchmark.mp3")
BENCHMARK_SIZE = 11_972_484
BENCHMARK_SHA256 = (
    "4003b057b19ca95bae78e66b3536557e342d1105315c2a6217f4475c0db51d63"
)
LONG_SORT_NAMES = (
    "common-prefix-abcdefghijklmnopqrst-track-2.mp3",
    "common-prefix-abcdefghijklmnopqrst-track-10.mp3",
    "common-prefix-abcdefghijklmnopqrst-track-100.mp3",
)


def syncsafe(value: int) -> bytes:
    return bytes(((value >> 21) & 0x7F, (value >> 14) & 0x7F,
                  (value >> 7) & 0x7F, value & 0x7F))


def strip_id3v2(data: bytes) -> bytes:
    if len(data) < 10 or data[:3] != b"ID3":
        return data
    size = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | \
           ((data[8] & 0x7F) << 7) | (data[9] & 0x7F)
    offset = 10 + size + (10 if data[5] & 0x10 else 0)
    if offset > len(data):
        raise ValueError("P1 source contains a truncated ID3v2 tag")
    return data[offset:]


def frame_v23(frame_id: str, text: str) -> bytes:
    payload = b"\x01\xff\xfe" + text.encode("utf-16le") + b"\x00\x00"
    return frame_id.encode("ascii") + len(payload).to_bytes(4, "big") + \
           b"\x00\x00" + payload


def frame_v24(frame_id: str, text: str) -> bytes:
    payload = b"\x03" + text.encode("utf-8") + b"\x00"
    return frame_id.encode("ascii") + syncsafe(len(payload)) + \
           b"\x00\x00" + payload


def frame_v23_payload(frame_id: str, payload: bytes) -> bytes:
    return frame_id.encode("ascii") + len(payload).to_bytes(4, "big") + \
           b"\x00\x00" + payload


def frame_v24_payload(frame_id: str, payload: bytes) -> bytes:
    return frame_id.encode("ascii") + syncsafe(len(payload)) + \
           b"\x00\x00" + payload


def unsynchronise(data: bytes) -> bytes:
    output = bytearray()
    for index, value in enumerate(data):
        output.append(value)
        next_value = data[index + 1] if index + 1 < len(data) else None
        if value == 0xFF and (next_value is None or next_value == 0x00 or
                              next_value >= 0xE0):
            output.append(0x00)
    return bytes(output)


def custom_tagged_mp3(audio: bytes, major: int, flags: int,
                      logical_body: bytes, apply_unsync: bool = False) -> bytes:
    stored_body = unsynchronise(logical_body) if apply_unsync else logical_body
    return b"ID3" + bytes((major, 0, flags)) + syncsafe(len(stored_body)) + \
           stored_body + audio


def tagged_mp3(audio: bytes, major: int, fields: dict[str, str]) -> bytes:
    frame_builder = frame_v23 if major == 3 else frame_v24
    body = b"".join(frame_builder(frame_id, value)
                    for frame_id, value in fields.items())
    return b"ID3" + bytes((major, 0, 0)) + syncsafe(len(body)) + body + audio


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_owned_fixture(path: Path) -> dict:
    """Return a validated ownership marker or refuse destructive work."""

    marker = path / MARKER_NAME
    if not marker.is_file():
        raise RuntimeError(
            f"Refusing to modify unowned fixture directory: {path}")
    try:
        data = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(
            f"Refusing to modify fixture with invalid marker: {path}: "
            f"{error}") from error
    if not isinstance(data, dict):
        raise RuntimeError(
            f"Refusing to modify fixture with non-object marker: {path}")
    if data.get("owner") != FIXTURE_OWNER:
        raise RuntimeError(
            f"Refusing to modify fixture with unexpected owner: {path}")
    schema = data.get("schema")
    if type(schema) is not int or schema != FIXTURE_VERSION:
        raise RuntimeError(
            f"Refusing to modify fixture with unexpected schema: {path}")
    return data


def ensure_owned_or_absent(path: Path) -> None:
    if not path.exists():
        return
    validate_owned_fixture(path)
    shutil.rmtree(path)


def write_fixture(source_mp3: Path, output: Path) -> dict:
    if not source_mp3.is_file():
        raise FileNotFoundError(
            f"P1 copyright-free source fixture is missing: {source_mp3}")
    ensure_owned_or_absent(output)
    output.mkdir(parents=True)

    audio = strip_id3v2(source_mp3.read_bytes())
    if len(audio) < 4096:
        raise ValueError("P1 source audio payload is unexpectedly small")

    (output / "Album 2").mkdir()
    (output / "Album 10").mkdir()
    (output / "Empty").mkdir()
    (output / "Metadata").mkdir()
    deep = output / "中文目录" / "日本語" / "三级"
    deep.mkdir(parents=True)
    hidden = output / ".hidden-dir"
    hidden.mkdir()
    large = output / "Large"
    large.mkdir()
    long_sort = output / "LongSort"
    long_sort.mkdir()

    (output / "playback.MP3").write_bytes(audio)
    (output / "song 2.mp3").write_bytes(audio)
    (output / "song 10.mp3").write_bytes(audio)
    (deep / "深层歌曲.mp3").write_bytes(audio)
    (output / ".hidden.mp3").write_bytes(audio[:64])
    (hidden / "secret.mp3").write_bytes(audio[:64])

    for name in ("readme.txt", "cover.jpg", "source.m4a", "song.lrc"):
        (output / name).write_bytes(b"P2 filter fixture\n")

    v23_fields = {
        "TIT2": "星の歌",
        "TPE1": "测试歌手",
        "TALB": "蓝色专辑",
        "TRCK": "2/12",
    }
    v24_fields = {
        "TIT2": "夜の図書館",
        "TPE1": "東京テスト",
        "TALB": "四季",
        "TRCK": "10",
    }
    metadata = output / "Metadata"
    (metadata / "id3-v23-utf16.mp3").write_bytes(
        tagged_mp3(audio, 3, v23_fields))
    (metadata / "id3-v24-utf8.mp3").write_bytes(
        tagged_mp3(audio, 4, v24_fields))

    latin1_title = "Cafe ÿ Track"
    latin1_body = b"".join((
        frame_v23_payload(
            "TIT2", b"\x00" + latin1_title.encode("latin-1") + b"\x00"),
        frame_v23_payload("TPE1", b"\x00Latin Artist\x00"),
        frame_v23_payload("TALB", b"\x00Latin Album\x00"),
        frame_v23_payload("TRCK", b"\x001/9\x00"),
    ))
    (metadata / "id3-v23-latin1.mp3").write_bytes(
        custom_tagged_mp3(audio, 3, 0, latin1_body))

    utf16be_title = "夜空BE"
    utf16be_body = b"".join((
        frame_v24_payload(
            "TIT2", b"\x02" + utf16be_title.encode("utf-16be") + b"\x00\x00"),
        frame_v24_payload(
            "TPE1", b"\x02" + "北京".encode("utf-16be") + b"\x00\x00"),
        frame_v24_payload(
            "TALB", b"\x02" + "星河".encode("utf-16be") + b"\x00\x00"),
        frame_v24_payload("TRCK", b"\x02\x00\x07\x00\x00"),
    ))
    (metadata / "id3-v24-utf16be.mp3").write_bytes(
        custom_tagged_mp3(audio, 4, 0, utf16be_body))

    # v2.3 tag-level unsynchronisation covers frame headers as well. A
    # 255-byte payload forces insertion after the final 0xFF size byte; a
    # second frame crosses the reader's 512-byte service boundary.
    unsync_title = b"\x00Unsync " + (b"A" * 180) + b"\xff\xe0" + (b"B" * 64)
    unsync_title = (unsync_title + b"C" * 255)[:255]
    unsync_body = b"".join((
        frame_v23_payload("TIT2", unsync_title),
        frame_v23_payload("TPE1", b"\x00Unsync Artist\x00"),
        frame_v23_payload("TALB", b"\x00Unsync Album\x00"),
        frame_v23_payload("TRCK", b"\x005/8\x00"),
        frame_v23_payload("TXXX", b"\x00" + b"P" * 520 + b"\xff\xe1END"),
    ))
    (metadata / "id3-v23-unsync.mp3").write_bytes(
        custom_tagged_mp3(audio, 3, 0x80, unsync_body, apply_unsync=True))

    v23_extended = (6).to_bytes(4, "big") + b"\x00\x00\x00\x00\x00\x00"
    (metadata / "id3-v23-extended.mp3").write_bytes(
        custom_tagged_mp3(
            audio, 3, 0x40, v23_extended +
            frame_v23_payload("TIT2", b"\x00Extended 23\x00")))
    v24_extended = syncsafe(6) + b"\x01\x00"
    (metadata / "id3-v24-extended.mp3").write_bytes(
        custom_tagged_mp3(
            audio, 4, 0x40, v24_extended +
            frame_v24_payload("TIT2", b"\x03Extended 24\x00")))

    long_title = "长" * 260
    (metadata / "id3-v24-long-title.mp3").write_bytes(
        tagged_mp3(audio, 4, {
            "TIT2": long_title,
            "TPE1": "Long Field Artist",
            "TALB": "Long Field Album",
            "TRCK": "11",
        }))
    (metadata / "无标签中文歌曲.mp3").write_bytes(audio)
    (metadata / "损坏标签回退.mp3").write_bytes(
        b"ID3\x04\x00\x00" + syncsafe(4096) + b"broken")

    for index in range(1, 1001):
        (large / f"track-{index:04d}.mp3").write_bytes(b"")

    # Every distinguishing digit lies beyond the firmware's 24-byte cached
    # sort prefix.  The device Gate therefore proves that the exact full-name
    # fallback preserves natural ordering without relying on equal-key scan
    # order, which can differ between NTFS and FAT.
    for name in LONG_SORT_NAMES:
        (long_sort / name).write_bytes(b"")

    files = []
    for path in sorted(item for item in output.rglob("*") if item.is_file()):
        if path.name == MARKER_NAME:
            continue
        files.append({
            "path": path.relative_to(output).as_posix(),
            "size": path.stat().st_size,
            "sha256": sha256(path),
        })

    manifest = {
        "schema": FIXTURE_VERSION,
        "root": "/Music/ADVWalkmanP2Test",
        "file_count": len(files),
        "large_track_count": 1000,
        "expected": {
            "root_directories_natural": [
                "Album 2", "Album 10", "Empty", "Large", "LongSort",
                "Metadata", "中文目录",
            ],
            "root_tracks_natural": [
                "playback.MP3", "song 2.mp3", "song 10.mp3",
            ],
            "v23": v23_fields,
            "v24": v24_fields,
            "latin1_title": latin1_title,
            "utf16be_title": utf16be_title,
            "unsync_title_prefix": "Unsync ",
            "v23_extended_title": "Extended 23",
            "v24_extended_title": "Extended 24",
            "long_title_truncated": True,
            "fallback_title": "无标签中文歌曲",
            "long_sort_natural": list(LONG_SORT_NAMES),
        },
        "files": files,
    }
    manifest_path = output / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    marker = {
        "owner": FIXTURE_OWNER,
        "schema": FIXTURE_VERSION,
        "manifest_sha256": sha256(manifest_path),
    }
    (output / MARKER_NAME).write_text(
        json.dumps(marker, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return marker


def sd_target(sd_root: Path) -> Path:
    return sd_root / "Music" / "ADVWalkmanP2Test"


def mirror_to_sd(local: Path, sd_root: Path) -> Path:
    if not sd_root.exists():
        raise FileNotFoundError(f"SD root does not exist: {sd_root}")
    target = sd_target(sd_root)
    ensure_owned_or_absent(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(local, target)
    if sha256(local / "manifest.json") != sha256(target / "manifest.json"):
        raise RuntimeError("SD fixture manifest verification failed")
    return target


def validate_benchmark(sd_root: Path) -> Path:
    """Verify the existing user benchmark without copying or modifying it."""
    benchmark = sd_root / BENCHMARK_RELATIVE_PATH
    if not benchmark.is_file():
        raise FileNotFoundError(
            f"Required long benchmark is missing: {benchmark}")
    actual_size = benchmark.stat().st_size
    if actual_size != BENCHMARK_SIZE:
        raise RuntimeError(
            f"Benchmark size mismatch: expected {BENCHMARK_SIZE}, "
            f"got {actual_size}")
    actual_sha = sha256(benchmark)
    if actual_sha != BENCHMARK_SHA256:
        raise RuntimeError(
            f"Benchmark SHA-256 mismatch: expected {BENCHMARK_SHA256}, "
            f"got {actual_sha}")
    return benchmark


def cleanup_sd(sd_root: Path) -> None:
    target = sd_target(sd_root)
    if not target.exists():
        return
    validate_owned_fixture(target)
    shutil.rmtree(target)


def main() -> int:
    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--source-mp3", type=Path,
        default=project / "test-data/local/p1-fixtures/cbr320-44100.mp3")
    parser.add_argument(
        "--output", type=Path,
        default=project / "test-data/local/p2-library-fixture")
    parser.add_argument("--sd-root", type=Path)
    parser.add_argument("--cleanup-sd", action="store_true")
    args = parser.parse_args()

    if args.cleanup_sd:
        if args.sd_root is None:
            parser.error("--cleanup-sd requires --sd-root")
        cleanup_sd(args.sd_root)
        print(f"P2_SD_FIXTURE_REMOVED={sd_target(args.sd_root)}")
        return 0

    marker = write_fixture(args.source_mp3, args.output)
    print(f"P2_FIXTURE={args.output}")
    print(f"P2_MANIFEST_SHA256={marker['manifest_sha256']}")
    if args.sd_root is not None:
        benchmark = validate_benchmark(args.sd_root)
        print(f"P2_BENCHMARK_VERIFIED={benchmark}")
        print(f"P2_BENCHMARK_SIZE={BENCHMARK_SIZE}")
        print(f"P2_BENCHMARK_SHA256={BENCHMARK_SHA256}")
        target = mirror_to_sd(args.output, args.sd_root)
        print(f"P2_SD_FIXTURE={target}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:  # concise CLI failure for PowerShell/Codex
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
