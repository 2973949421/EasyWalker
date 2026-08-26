#!/usr/bin/env python3
"""Read-only host validation for the ADV Walkman P2 library gate.

The default run validates the ignored local P2 fixture.  With ``--sd-root``
it additionally checks the four P2 gate logs and the test Recent A/B slots.
No file is created, changed, or removed.
"""

from __future__ import annotations

import argparse
import binascii
from functools import cmp_to_key
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import sys
from typing import Any


MARKER_NAME = ".adv-walkman-p2-fixture.json"
MANIFEST_NAME = "manifest.json"
FIXTURE_OWNER = "adv-walkman-p2-fixture"
FIXTURE_SCHEMA = 1

RECORD_HEADER = struct.Struct("<IHHIII")
RECENT_MAGIC = 0x31525741  # AWR1 on disk.
RECENT_SCHEMA = 1
RECENT_MAX_TRACKS = 32
TRACK_PATH_MAX_BYTES = 511
RECENT_MAX_PAYLOAD = 2 + RECENT_MAX_TRACKS * (2 + TRACK_PATH_MAX_BYTES)

P2_LOG_NAMES = tuple(f"p2-0{index}-last.txt" for index in range(1, 5))


class ValidationError(ValueError):
    """A deterministic validation failure suitable for concise CLI output."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def load_json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValidationError(f"cannot read JSON {path}: {error}") from error
    require(isinstance(value, dict), f"JSON root is not an object: {path}")
    return value


def validate_relative_manifest_path(value: object) -> str:
    require(isinstance(value, str) and value != "", "invalid manifest path")
    require("\\" not in value, f"manifest path uses backslash: {value}")
    pure = PurePosixPath(value)
    require(not pure.is_absolute(), f"manifest path is absolute: {value}")
    require(
        all(part not in ("", ".", "..") for part in pure.parts),
        f"manifest path is not canonical: {value}",
    )
    require(pure.as_posix() == value, f"manifest path is not normalized: {value}")
    return pure.as_posix()


def fold_ascii(value: int) -> int:
    return value + 0x20 if 0x41 <= value <= 0x5A else value


def natural_name_compare(left: str, right: str) -> int:
    """Compare UTF-8 names using the P2 ASCII-folded natural order."""

    a = left.encode("utf-8")
    b = right.encode("utf-8")
    ai = bi = 0
    while ai < len(a) and bi < len(b):
        if 0x30 <= a[ai] <= 0x39 and 0x30 <= b[bi] <= 0x39:
            ae = ai
            be = bi
            while ae < len(a) and 0x30 <= a[ae] <= 0x39:
                ae += 1
            while be < len(b) and 0x30 <= b[be] <= 0x39:
                be += 1
            arun = a[ai:ae]
            brun = b[bi:be]
            asignificant = arun.lstrip(b"0") or b"0"
            bsignificant = brun.lstrip(b"0") or b"0"
            if len(asignificant) != len(bsignificant):
                return -1 if len(asignificant) < len(bsignificant) else 1
            if asignificant != bsignificant:
                return -1 if asignificant < bsignificant else 1
            # Equal numeric value: fewer leading zeroes is the stable tie-break.
            if len(arun) != len(brun):
                return -1 if len(arun) < len(brun) else 1
            ai, bi = ae, be
            continue

        afolded = fold_ascii(a[ai])
        bfolded = fold_ascii(b[bi])
        if afolded != bfolded:
            return -1 if afolded < bfolded else 1
        ai += 1
        bi += 1

    if ai == len(a) and bi == len(b):
        return 0
    return -1 if ai == len(a) else 1


def sorted_natural(names: list[str]) -> list[str]:
    return sorted(names, key=cmp_to_key(natural_name_compare))


def syncsafe32(data: bytes) -> int:
    require(len(data) == 4, "syncsafe integer must contain four bytes")
    require(not any(value & 0x80 for value in data), "invalid syncsafe integer")
    return (data[0] << 21) | (data[1] << 14) | (data[2] << 7) | data[3]


def deunsynchronise(data: bytes) -> bytes:
    output = bytearray()
    index = 0
    while index < len(data):
        value = data[index]
        output.append(value)
        index += 1
        if value == 0xFF and index < len(data) and data[index] == 0:
            index += 1
    return bytes(output)


def trim_utf16_terminator(data: bytes) -> bytes:
    usable = len(data) - (len(data) % 2)
    data = data[:usable]
    for index in range(0, len(data) - 1, 2):
        if data[index:index + 2] == b"\x00\x00":
            return data[:index]
    return data


def decode_text_frame(payload: bytes) -> str:
    require(payload, "empty ID3 text frame")
    encoding = payload[0]
    data = payload[1:]
    try:
        if encoding == 0:
            text = data.split(b"\x00", 1)[0].decode("latin-1")
        elif encoding == 3:
            text = data.split(b"\x00", 1)[0].decode("utf-8")
        elif encoding == 1:
            require(len(data) >= 2, "UTF-16 text is missing its BOM")
            if data[:2] == b"\xff\xfe":
                codec = "utf-16le"
            elif data[:2] == b"\xfe\xff":
                codec = "utf-16be"
            else:
                raise ValidationError("UTF-16 text is missing its BOM")
            text = trim_utf16_terminator(data[2:]).decode(codec)
        elif encoding == 2:
            text = trim_utf16_terminator(data).decode("utf-16be")
        else:
            raise ValidationError(f"unsupported ID3 text encoding: {encoding}")
    except UnicodeDecodeError as error:
        raise ValidationError(f"invalid ID3 text encoding: {error}") from error
    return text.replace("\ufeff", "").strip("\x00")


def parse_id3_text_fields(path: Path) -> dict[str, str]:
    data = path.read_bytes()
    if len(data) < 10 or data[:3] != b"ID3":
        return {}
    major = data[3]
    require(major in (3, 4), f"unsupported ID3v2 version in {path.name}: {major}")
    tag_size = syncsafe32(data[6:10])
    require(tag_size <= len(data) - 10, f"truncated ID3 tag in {path.name}")
    body = data[10:10 + tag_size]
    tag_flags = data[5]

    # In v2.3 tag-level unsynchronisation applies to the complete stored tag,
    # including extended/frame headers.  v2.4 applies it to frame payloads.
    if major == 3 and tag_flags & 0x80:
        body = deunsynchronise(body)

    offset = 0
    if tag_flags & 0x40:
        require(len(body) >= 4, f"truncated extended header in {path.name}")
        if major == 3:
            extended_size = int.from_bytes(body[:4], "big")
            offset = 4 + extended_size
        else:
            extended_size = syncsafe32(body[:4])
            offset = extended_size
        require(
            4 <= offset <= len(body),
            f"invalid extended header size in {path.name}",
        )

    fields: dict[str, str] = {}
    frames_seen = 0
    while offset + 10 <= len(body):
        header = body[offset:offset + 10]
        if header[:4] == b"\x00\x00\x00\x00":
            break
        try:
            frame_id = header[:4].decode("ascii")
        except UnicodeDecodeError as error:
            raise ValidationError(f"invalid frame id in {path.name}") from error
        require(
            len(frame_id) == 4 and all(c.isupper() or c.isdigit() for c in frame_id),
            f"invalid frame id in {path.name}: {frame_id!r}",
        )
        frame_size = (
            int.from_bytes(header[4:8], "big")
            if major == 3
            else syncsafe32(header[4:8])
        )
        offset += 10
        require(frame_size <= len(body) - offset, f"truncated {frame_id} in {path.name}")
        payload = body[offset:offset + frame_size]
        offset += frame_size
        frames_seen += 1
        require(frames_seen <= 256, f"too many ID3 frames in {path.name}")

        format_flags = header[9]
        if major == 3:
            if format_flags & (0x80 | 0x40):  # compressed or encrypted
                continue
            if format_flags & 0x20:  # grouping identity
                require(payload, f"truncated grouped frame in {path.name}")
                payload = payload[1:]
        else:
            if format_flags & (0x08 | 0x04):  # compressed or encrypted
                continue
            if tag_flags & 0x80 or format_flags & 0x02:
                payload = deunsynchronise(payload)
            if format_flags & 0x40:  # grouping identity
                require(payload, f"truncated grouped frame in {path.name}")
                payload = payload[1:]
            if format_flags & 0x01:  # data length indicator
                require(len(payload) >= 4, f"truncated DLI in {path.name}")
                syncsafe32(payload[:4])
                payload = payload[4:]

        if frame_id in ("TIT2", "TPE1", "TALB", "TRCK") and frame_id not in fields:
            fields[frame_id] = decode_text_frame(payload)
    return fields


def metadata_with_fallback(path: Path) -> tuple[dict[str, str], bool]:
    malformed = False
    try:
        fields = parse_id3_text_fields(path)
    except (OSError, ValidationError):
        fields = {}
        malformed = True
    if not fields.get("TIT2"):
        fields["TIT2"] = path.stem
    return fields, malformed


def validate_metadata(fixture: Path, expected: dict[str, Any]) -> int:
    metadata = fixture / "Metadata"
    v23, _ = metadata_with_fallback(metadata / "id3-v23-utf16.mp3")
    v24, _ = metadata_with_fallback(metadata / "id3-v24-utf8.mp3")
    require(v23 == expected.get("v23"), "ID3v2.3 UTF-16 fields mismatch")
    require(v24 == expected.get("v24"), "ID3v2.4 UTF-8 fields mismatch")

    latin1, _ = metadata_with_fallback(metadata / "id3-v23-latin1.mp3")
    require(latin1["TIT2"] == expected.get("latin1_title"), "Latin-1 title mismatch")
    utf16be, _ = metadata_with_fallback(metadata / "id3-v24-utf16be.mp3")
    require(utf16be["TIT2"] == expected.get("utf16be_title"), "UTF-16BE title mismatch")

    unsync, unsync_malformed = metadata_with_fallback(
        metadata / "id3-v23-unsync.mp3"
    )
    require(not unsync_malformed, "v2.3 unsynchronised tag did not parse")
    require(
        unsync["TIT2"].startswith(str(expected.get("unsync_title_prefix", ""))),
        "v2.3 unsynchronised title mismatch",
    )
    v23_extended, _ = metadata_with_fallback(metadata / "id3-v23-extended.mp3")
    v24_extended, _ = metadata_with_fallback(metadata / "id3-v24-extended.mp3")
    require(
        v23_extended["TIT2"] == expected.get("v23_extended_title"),
        "v2.3 extended-header title mismatch",
    )
    require(
        v24_extended["TIT2"] == expected.get("v24_extended_title"),
        "v2.4 extended-header title mismatch",
    )

    long_title, _ = metadata_with_fallback(metadata / "id3-v24-long-title.mp3")
    require(len(long_title["TIT2"]) > 255, "long-title fixture is not long")
    fallback, fallback_malformed = metadata_with_fallback(
        metadata / "无标签中文歌曲.mp3"
    )
    require(not fallback_malformed, "untagged fallback fixture was treated as malformed")
    require(fallback["TIT2"] == expected.get("fallback_title"), "title fallback mismatch")
    malformed, was_malformed = metadata_with_fallback(
        metadata / "损坏标签回退.mp3"
    )
    require(was_malformed, "malformed tag was not detected")
    require(malformed["TIT2"] == "损坏标签回退", "malformed title fallback mismatch")
    return 10


def validate_fixture(fixture: Path) -> dict[str, Any]:
    require(fixture.is_dir(), f"fixture directory not found: {fixture}")
    marker_path = fixture / MARKER_NAME
    manifest_path = fixture / MANIFEST_NAME
    marker = load_json_object(marker_path)
    manifest = load_json_object(manifest_path)
    require(marker.get("owner") == FIXTURE_OWNER, "unexpected fixture marker owner")
    require(marker.get("schema") == FIXTURE_SCHEMA, "unsupported fixture marker schema")
    manifest_sha = sha256_file(manifest_path)
    require(marker.get("manifest_sha256") == manifest_sha, "manifest SHA-256 mismatch")
    require(manifest.get("schema") == FIXTURE_SCHEMA, "unsupported manifest schema")
    require(manifest.get("root") == "/Music/ADVWalkmanP2Test", "unexpected fixture root")

    file_records = manifest.get("files")
    require(isinstance(file_records, list), "manifest files is not an array")
    expected_files: dict[str, tuple[int, str]] = {}
    for record in file_records:
        require(isinstance(record, dict), "manifest file record is not an object")
        relative = validate_relative_manifest_path(record.get("path"))
        require(relative not in expected_files, f"duplicate manifest path: {relative}")
        size = record.get("size")
        digest = record.get("sha256")
        require(isinstance(size, int) and size >= 0, f"invalid size for {relative}")
        require(
            isinstance(digest, str)
            and len(digest) == 64
            and all(c in "0123456789abcdef" for c in digest),
            f"invalid SHA-256 for {relative}",
        )
        expected_files[relative] = (size, digest)

    require(manifest.get("file_count") == len(expected_files), "manifest file count mismatch")
    actual_files = {
        path.relative_to(fixture).as_posix()
        for path in fixture.rglob("*")
        if path.is_file()
        and path.relative_to(fixture).as_posix() not in (MARKER_NAME, MANIFEST_NAME)
    }
    require(actual_files == set(expected_files), "fixture file set does not match manifest")
    for relative, (expected_size, expected_sha) in expected_files.items():
        path = fixture.joinpath(*PurePosixPath(relative).parts)
        require(path.stat().st_size == expected_size, f"size mismatch: {relative}")
        require(sha256_file(path) == expected_sha, f"SHA-256 mismatch: {relative}")

    root_directories: list[str] = []
    root_tracks: list[str] = []
    for path in fixture.iterdir():
        if path.name.startswith("."):
            continue
        if path.is_dir():
            root_directories.append(path.name)
        elif path.is_file() and path.suffix.lower() == ".mp3":
            root_tracks.append(path.name)
    expected = manifest.get("expected")
    require(isinstance(expected, dict), "manifest expected section is not an object")
    require(
        sorted_natural(root_directories) == expected.get("root_directories_natural"),
        "root directory natural order/filter mismatch",
    )
    require(
        sorted_natural(root_tracks) == expected.get("root_tracks_natural"),
        "root track natural order/filter mismatch",
    )
    require((fixture / ".hidden-dir").is_dir(), "hidden directory fixture missing")
    require((fixture / ".hidden.mp3").is_file(), "hidden track fixture missing")
    for name in ("readme.txt", "cover.jpg", "source.m4a", "song.lrc"):
        require((fixture / name).is_file(), f"filter fixture missing: {name}")
    require(
        (fixture / "中文目录" / "日本語" / "三级" / "深层歌曲.mp3").is_file(),
        "deep UTF-8 path fixture missing",
    )

    large_count = sum(
        1 for path in (fixture / "Large").iterdir()
        if path.is_file() and path.suffix.lower() == ".mp3"
    )
    require(large_count == manifest.get("large_track_count"), "Large track count mismatch")
    metadata_cases = validate_metadata(fixture, expected)
    return {
        "path": str(fixture.resolve()),
        "manifest_sha256": manifest_sha,
        "file_count": len(expected_files),
        "large_track_count": large_count,
        "metadata_cases": metadata_cases,
    }


def parse_key_value_log(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except (OSError, UnicodeError) as error:
        raise ValidationError(f"cannot read log {path}: {error}") from error
    nonempty = [line.strip() for line in lines if line.strip()]
    require(nonempty, f"empty gate log: {path}")
    require(
        nonempty[0] in ("status=PASS", "status=FAIL"),
        f"invalid gate status line: {path.name}",
    )
    fields: dict[str, str] = {}
    for line in nonempty:
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        require(key != "", f"empty key in gate log: {path.name}")
        if key in fields and fields[key] != value.strip():
            raise ValidationError(f"conflicting {key} values in {path.name}")
        fields[key] = value.strip()
    require(
        fields.get("status") in ("PASS", "FAIL"),
        f"missing gate status: {path.name}",
    )
    require(fields.get("log_complete") == "1", f"incomplete gate log: {path.name}")
    return fields


def validate_gate_logs(
    sd_root: Path, manifest_sha: str
) -> tuple[dict[str, str], list[str]]:
    logs_dir = sd_root / "ADVWalkman" / "logs"
    results: dict[str, str] = {}
    failures: list[str] = []
    hash_keys = (
        "fixture_manifest_sha256",
        "manifest_sha256",
        "fixture_sha256",
        "fixture_hash",
    )
    timing_keys = (
        "player_service_start_gap_over_100ms",
        "player_service_start_gap_max_us",
        "player_gap_previous_player_runtime_us",
        "player_gap_previous_library_runtime_us",
        "player_gap_previous_gate_us",
        "player_gap_previous_loop_body_us",
        "player_gap_current_input_us",
        "speaker_channel_empty_at_service_start",
        "unexpected_playback_state_over_100ms",
        "input_update_max_us",
        "player_runtime_service_max_us",
        "player_engine_service_max_us",
        "library_runtime_service_max_us",
        "gate_service_max_us",
        "loop_body_max_us",
    )
    timing_values: dict[str, int] | None = None
    gap_from_phase = "none"
    gap_to_phase = "none"
    for index, name in enumerate(P2_LOG_NAMES, start=1):
        fields = parse_key_value_log(logs_dir / name)
        require(
            fields.get("task") == f"P2-0{index}",
            f"unexpected task id in {name}",
        )
        current_timings: dict[str, int] = {}
        for key in timing_keys:
            raw = fields.get(key)
            try:
                value = int(raw) if raw is not None else -1
            except ValueError as error:
                raise ValidationError(
                    f"invalid {key} in {name}: {raw}"
                ) from error
            require(value >= 0, f"missing or negative {key} in {name}")
            current_timings[key] = value
        if timing_values is None:
            timing_values = current_timings
        else:
            require(
                current_timings == timing_values,
                f"mixed timing snapshots across P2 logs: {name}",
            )
        current_gap_from_phase = fields.get(
            "player_service_start_gap_max_from_phase", ""
        )
        current_gap_to_phase = fields.get(
            "player_service_start_gap_max_to_phase", ""
        )
        require(current_gap_from_phase != "", f"missing gap from-phase in {name}")
        require(current_gap_to_phase != "", f"missing gap to-phase in {name}")
        if index == 1:
            gap_from_phase = current_gap_from_phase
            gap_to_phase = current_gap_to_phase
        else:
            require(
                current_gap_from_phase == gap_from_phase,
                f"mixed gap from-phases across P2 logs: {name}",
            )
            require(
                current_gap_to_phase == gap_to_phase,
                f"mixed gap to-phases across P2 logs: {name}",
            )
        require(
            fields.get("fixture_manifest_sha256", "").lower() == manifest_sha,
            f"missing or mismatched canonical fixture hash in {name}",
        )
        for key in hash_keys:
            if key in fields:
                require(
                    fields[key].lower() == manifest_sha,
                    f"fixture hash mismatch in {name}",
                )
        results[f"p2_0{index}_status"] = fields["status"]
        semantic_checks = (
            (fields["status"] == "PASS", f"gate log is not PASS: {name}"),
            (fields.get("player_state") == "PLAYING", f"player not PLAYING in {name}"),
            (fields.get("player_error") == "NONE", f"player error in {name}"),
            (fields.get("audio_error") == "none", f"audio error in {name}"),
            (fields.get("sample_rate") == "44100", f"unexpected sample rate in {name}"),
            (fields.get("backpressure") == "0", f"backpressure in {name}"),
            (fields.get("audio_error_events") == "0", f"audio error events in {name}"),
            (
                fields.get("unexpected_playback_state_over_100ms") == "0",
                f"unexpected playback state in {name}",
            ),
            (
                fields.get("speaker_starvation_over_100ms") == "0",
                f"speaker starvation in {name}",
            ),
        )
        failures.extend(message for passed, message in semantic_checks if not passed)
    require(timing_values is not None, "no P2 timing snapshot was parsed")
    for key in (
        "input_update_max_us",
        "player_runtime_service_max_us",
        "player_engine_service_max_us",
        "library_runtime_service_max_us",
        "gate_service_max_us",
        "loop_body_max_us",
    ):
        if timing_values[key] <= 0:
            failures.append(f"timing metric was not sampled: {key}")
    if timing_values["player_service_start_gap_max_us"] > 0:
        if gap_from_phase == "none" or gap_to_phase == "none":
            failures.append("player service gap phase transition was not captured")
    for key, value in timing_values.items():
        results[key] = str(value)
    results["player_service_start_gap_max_from_phase"] = gap_from_phase
    results["player_service_start_gap_max_to_phase"] = gap_to_phase
    return results, failures


def canonical_recent_path(path: str) -> bool:
    if not path.startswith("/") or path.endswith("/") or "\\" in path:
        return False
    pure = PurePosixPath(path)
    return (
        pure.as_posix() == path
        and all(part not in ("", ".", "..") for part in pure.parts[1:])
    )


def parse_recent_slot(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    require(len(data) >= RECORD_HEADER.size, f"truncated Recent header: {path}")
    magic, schema, header_size, generation, payload_size, stored_crc = (
        RECORD_HEADER.unpack_from(data)
    )
    require(magic == RECENT_MAGIC, f"invalid Recent magic: {path.name}")
    require(schema == RECENT_SCHEMA, f"unsupported Recent schema: {path.name}")
    require(header_size == RECORD_HEADER.size, f"invalid Recent header size: {path.name}")
    require(generation != 0, f"zero Recent generation: {path.name}")
    require(2 <= payload_size <= RECENT_MAX_PAYLOAD, f"invalid Recent payload size: {path.name}")
    require(len(data) == header_size + payload_size, f"Recent record length mismatch: {path.name}")
    payload = data[header_size:]
    calculated_crc = binascii.crc32(payload) & 0xFFFFFFFF
    require(calculated_crc == stored_crc, f"Recent CRC32 mismatch: {path.name}")

    count = struct.unpack_from("<H", payload)[0]
    require(count <= RECENT_MAX_TRACKS, f"Recent count exceeds 32: {path.name}")
    cursor = 2
    paths: list[str] = []
    folded_paths: set[bytes] = set()
    for _ in range(count):
        require(cursor + 2 <= len(payload), f"truncated Recent path length: {path.name}")
        length = struct.unpack_from("<H", payload, cursor)[0]
        cursor += 2
        require(
            0 < length <= TRACK_PATH_MAX_BYTES and cursor + length <= len(payload),
            f"invalid Recent path length: {path.name}",
        )
        encoded = payload[cursor:cursor + length]
        cursor += length
        try:
            value = encoded.decode("utf-8")
        except UnicodeDecodeError as error:
            raise ValidationError(f"invalid UTF-8 Recent path: {path.name}") from error
        require("\x00" not in value and canonical_recent_path(value), f"non-canonical Recent path: {value}")
        folded = bytes(fold_ascii(byte) for byte in encoded)
        require(folded not in folded_paths, f"duplicate Recent path: {value}")
        folded_paths.add(folded)
        paths.append(value)
    require(cursor == len(payload), f"Recent payload has trailing bytes: {path.name}")
    return {
        "generation": generation,
        "count": count,
        "crc32": f"{stored_crc:08x}",
        "paths": paths,
    }


def recent_generation_newer(candidate: int, reference: int) -> bool:
    difference = (candidate - reference) & 0xFFFFFFFF
    return 0 < difference < 0x80000000


def resolve_recent_directory(sd_root: Path, requested: Path | None) -> Path:
    if requested is not None:
        raw = str(requested)
        candidate = (
            sd_root / raw.lstrip("/\\")
            if raw.startswith(("/", "\\")) or not requested.is_absolute()
            else requested
        )
        return candidate

    adv_root = sd_root / "ADVWalkman"
    require(adv_root.is_dir(), f"ADVWalkman directory not found: {adv_root}")
    parents_a = {path.parent for path in adv_root.rglob("recent-a.bin")}
    parents_b = {path.parent for path in adv_root.rglob("recent-b.bin")}
    complete = parents_a & parents_b
    require(complete, "no complete Recent A/B pair found on SD")

    def score(path: Path) -> tuple[int, int]:
        lowered = path.as_posix().lower()
        return (int("p2" in lowered) + int("test" in lowered), -len(path.parts))

    best_score = max(score(path) for path in complete)
    best = sorted(path for path in complete if score(path) == best_score)
    require(len(best) == 1, "multiple equally likely Recent A/B test directories")
    return best[0]


def validate_recent_slots(sd_root: Path, requested: Path | None) -> dict[str, Any]:
    directory = resolve_recent_directory(sd_root, requested)
    slot_a = parse_recent_slot(directory / "recent-a.bin")
    slot_b = parse_recent_slot(directory / "recent-b.bin")
    require(
        slot_a["generation"] != slot_b["generation"],
        "Recent A/B slots have the same generation",
    )
    newest = "A" if recent_generation_newer(
        int(slot_a["generation"]), int(slot_b["generation"])
    ) else "B"
    return {
        "directory": str(directory.resolve()),
        "a_generation": slot_a["generation"],
        "a_count": slot_a["count"],
        "a_crc32": slot_a["crc32"],
        "b_generation": slot_b["generation"],
        "b_count": slot_b["count"],
        "b_crc32": slot_b["crc32"],
        "newest": newest,
    }


def printable(value: object) -> str:
    return str(value).replace("\r", " ").replace("\n", " ")


def emit(key: str, value: object) -> None:
    print(f"{key}={printable(value)}")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")

    project = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--fixture",
        type=Path,
        default=project / "test-data/local/p2-library-fixture",
        help="local marker-owned P2 fixture directory",
    )
    parser.add_argument(
        "--sd-root",
        type=Path,
        help="optional mounted microSD root; read-only checks only",
    )
    parser.add_argument(
        "--recent-dir",
        type=Path,
        help="optional Recent A/B directory, absolute or relative to --sd-root",
    )
    args = parser.parse_args()
    if args.recent_dir is not None and args.sd_root is None:
        parser.error("--recent-dir requires --sd-root")

    failed = False
    fixture_result: dict[str, Any] | None = None
    try:
        fixture_result = validate_fixture(args.fixture)
        emit("fixture_status", "PASS")
        emit("fixture_path", fixture_result["path"])
        emit("fixture_manifest_sha256", fixture_result["manifest_sha256"])
        emit("fixture_file_count", fixture_result["file_count"])
        emit("fixture_large_track_count", fixture_result["large_track_count"])
        emit("fixture_metadata_cases", fixture_result["metadata_cases"])
    except (OSError, ValidationError) as error:
        failed = True
        emit("fixture_status", "FAIL")
        emit("fixture_error", error)

    if args.sd_root is not None:
        if not args.sd_root.is_dir():
            failed = True
            emit("sd_status", "FAIL")
            emit("sd_error", f"SD root not found: {args.sd_root}")
        else:
            try:
                require(fixture_result is not None, "local fixture validation failed")
                log_results, log_failures = validate_gate_logs(
                    args.sd_root, str(fixture_result["manifest_sha256"])
                )
                if log_failures:
                    failed = True
                emit("p2_logs_status", "FAIL" if log_failures else "PASS")
                for key, value in log_results.items():
                    emit(key, value)
                if log_failures:
                    emit("p2_logs_error", "; ".join(log_failures))
            except (OSError, ValidationError) as error:
                failed = True
                emit("p2_logs_status", "FAIL")
                emit("p2_logs_error", error)

            try:
                recent = validate_recent_slots(args.sd_root, args.recent_dir)
                emit("recent_slots_status", "PASS")
                for key, value in recent.items():
                    emit(f"recent_{key}", value)
            except (OSError, ValidationError) as error:
                failed = True
                emit("recent_slots_status", "FAIL")
                emit("recent_slots_error", error)

    emit("overall_status", "FAIL" if failed else "PASS")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
