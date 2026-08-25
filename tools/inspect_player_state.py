#!/usr/bin/env python3
"""Read-only validator for ADV Walkman schema-v1 Queue/Session slots."""

from __future__ import annotations

import argparse
import binascii
import json
from pathlib import Path
import struct
import sys


HEADER = struct.Struct("<IHHIII")
QUEUE_MAGIC = 0x31515741  # AWQ1
SESSION_MAGIC = 0x31535741  # AWS1
SCHEMA_VERSION = 1
MAX_TRACKS = 1024
MAX_PATH_BYTES = 511
MAX_QUEUE_PAYLOAD = 256 * 1024
MAX_HISTORY = 32


def parse_record_bytes(data: bytes) -> dict[str, object]:
    if len(data) < HEADER.size:
        raise ValueError("truncated record header")
    magic, version, header_size, generation, payload_length, payload_crc = (
        HEADER.unpack_from(data)
    )
    if version != SCHEMA_VERSION:
        raise ValueError(f"unsupported schema version: {version}")
    if header_size != HEADER.size or generation == 0:
        raise ValueError("invalid header size or generation")
    if len(data) != header_size + payload_length:
        raise ValueError("record length mismatch")
    payload = data[header_size:]
    calculated_crc = binascii.crc32(payload) & 0xFFFFFFFF
    if calculated_crc != payload_crc:
        raise ValueError(
            f"CRC32 mismatch: stored={payload_crc:08x} calculated={calculated_crc:08x}"
        )

    common: dict[str, object] = {
        "generation": generation,
        "payload_length": payload_length,
        "crc32": f"{payload_crc:08x}",
    }
    if magic == QUEUE_MAGIC:
        common.update(parse_queue_payload(payload))
        common["kind"] = "queue"
    elif magic == SESSION_MAGIC:
        common.update(parse_session_payload(payload))
        common["kind"] = "session"
    else:
        raise ValueError(f"unknown magic: 0x{magic:08x}")
    return common


def parse_queue_payload(payload: bytes) -> dict[str, object]:
    if len(payload) < 2 or len(payload) > MAX_QUEUE_PAYLOAD:
        raise ValueError("invalid queue payload size")
    count = struct.unpack_from("<H", payload)[0]
    if count > MAX_TRACKS:
        raise ValueError("queue track count exceeds 1024")
    cursor = 2
    paths: list[str] = []
    for _ in range(count):
        if cursor + 2 > len(payload):
            raise ValueError("truncated queue path length")
        length = struct.unpack_from("<H", payload, cursor)[0]
        cursor += 2
        if length == 0 or length > MAX_PATH_BYTES or cursor + length > len(payload):
            raise ValueError("invalid or truncated queue path")
        try:
            paths.append(payload[cursor : cursor + length].decode("utf-8"))
        except UnicodeDecodeError as error:
            raise ValueError("queue path is not valid UTF-8") from error
        cursor += length
    if cursor != len(payload):
        raise ValueError("queue payload has trailing bytes")
    return {"track_count": count, "paths": paths}


def parse_session_payload(payload: bytes) -> dict[str, object]:
    if len(payload) < 24:
        raise ValueError("truncated session payload")
    queue_generation, current_index = struct.unpack_from("<IH", payload)
    repeat_mode = payload[6]
    shuffle_enabled = bool(payload[7])
    position_ms, source_offset = struct.unpack_from("<II", payload, 8)
    order_count, order_cursor = struct.unpack_from("<HH", payload, 16)
    history_count = payload[20]
    expected = 24 + order_count * 2 + history_count * 2
    if (
        repeat_mode > 2
        or order_count > MAX_TRACKS
        or order_cursor > order_count
        or history_count > MAX_HISTORY
        or expected != len(payload)
    ):
        raise ValueError("invalid session dimensions")
    order = list(struct.unpack_from(f"<{order_count}H", payload, 24))
    history_offset = 24 + order_count * 2
    history = list(
        struct.unpack_from(f"<{history_count}H", payload, history_offset)
    )
    if any(value >= MAX_TRACKS for value in order + history):
        raise ValueError("session index exceeds 1023")
    return {
        "queue_generation": queue_generation,
        "current_index": current_index,
        "position_ms": position_ms,
        "source_offset": source_offset,
        "repeat_mode": ["off", "all", "one"][repeat_mode],
        "shuffle_enabled": shuffle_enabled,
        "order_cursor": order_cursor,
        "order": order,
        "history": history,
    }


def inspect_file(path: Path) -> dict[str, object]:
    result = parse_record_bytes(path.read_bytes())
    result["path"] = str(path.resolve())
    result["size_bytes"] = path.stat().st_size
    return result


def self_test() -> None:
    queue_payload = struct.pack("<H", 2) + struct.pack("<H", 6) + b"/a.mp3"
    queue_payload += struct.pack("<H", 6) + b"/b.mp3"
    header = HEADER.pack(
        QUEUE_MAGIC,
        SCHEMA_VERSION,
        HEADER.size,
        7,
        len(queue_payload),
        binascii.crc32(queue_payload) & 0xFFFFFFFF,
    )
    parsed = parse_record_bytes(header + queue_payload)
    assert parsed["track_count"] == 2
    assert parsed["paths"] == ["/a.mp3", "/b.mp3"]

    session_payload = bytearray(24)
    struct.pack_into("<IH", session_payload, 0, 7, 1)
    session_payload[6] = 2
    session_payload[7] = 1
    struct.pack_into("<IIHH", session_payload, 8, 4000, 12345, 2, 1)
    session_payload[20] = 1
    session_payload.extend(struct.pack("<HHH", 0, 1, 0))
    header = HEADER.pack(
        SESSION_MAGIC,
        SCHEMA_VERSION,
        HEADER.size,
        8,
        len(session_payload),
        binascii.crc32(session_payload) & 0xFFFFFFFF,
    )
    parsed = parse_record_bytes(header + session_payload)
    assert parsed["position_ms"] == 4000
    assert parsed["repeat_mode"] == "one"
    assert parsed["order"] == [0, 1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("self-test: PASS")
        return 0
    if not args.paths:
        parser.error("at least one state slot path is required")

    reports: list[dict[str, object]] = []
    failed = False
    for path in args.paths:
        try:
            reports.append(inspect_file(path))
        except (OSError, ValueError) as error:
            failed = True
            reports.append({"path": str(path), "error": str(error)})
    print(json.dumps(reports, ensure_ascii=False, indent=2))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
